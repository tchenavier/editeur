# Contrat HTTP de synchronisation

Ce document est **la référence commune** du client (cet éditeur) et du serveur
(la machine Debian). Aucun des deux ne doit supposer quoi que ce soit de
l'autre en dehors de ce qui est écrit ici : c'est ce qui permet de réécrire le
serveur dans un autre langage sans toucher au client, et l'inverse.

Version du contrat : **1**.

---

## Principes

**Le fichier local est la vérité.** Le serveur est un miroir. Aucune commande
de ce protocole n'est nécessaire pour écrire un roman ; l'éditeur fonctionne
entièrement sans lui.

**Rien ne part sans appairage.** Le client n'émet aucune requête pour un
projet tant que l'auteur ne l'a pas appairé explicitement (jalon C).

**Le serveur lit le texte.** Décision du 31 juillet 2026 : pas de chiffrement
de bout en bout. Le contenu circule en TLS et repose sur une machine que
l'auteur contrôle. C'est ce qui rendra possibles, en Phase 6, la recherche et
la publication côté serveur. Conséquence à assumer : un serveur compromis voit
les manuscrits qu'il héberge.

**Le code d'état HTTP porte l'information machine, le corps porte
l'explication humaine.** Pas de format d'erreur à analyser, donc pas d'analyseur
supplémentaire à écrire, à tester et à durcir. Un corps d'erreur est du texte
brut destiné à être montré tel quel à l'auteur.

**Aucune réponse n'est présumée bien formée.** Le serveur est une entrée non
fiable au même titre qu'un fichier ouvert : réponse tronquée, type de contenu
faux, corps démesuré, redirection en boucle. Le client borne tout ce qu'il lit
et refuse plutôt que de deviner.

---

## Forme générale

- Base : `https://<hôte>/api/`
- Toutes les requêtes portent `Authorization: Bearer <jeton>` **sauf**
  `GET /api/version`, où le client n'envoie **délibérément aucun jeton** :
  c'est le premier contact avec un serveur dont il ne sait encore rien, et le
  contrat n'y demande pas d'authentification. Les jetons sont délivrés au
  jalon C ; d'ici là le client envoie l'en-tête s'il en a un et le serveur de
  développement l'ignore.
- L'identifiant de projet est celui scellé dans l'en-tête YAML du manuscrit
  (`project:`), un UUID v4. Il apparaît tel quel dans les chemins.
- Les chemins de fichier sont **relatifs au dossier du projet**, séparateur
  `/`, encodés pour l'URL. Ils ne peuvent contenir ni `..`, ni `\`, ni
  caractère de contrôle : le serveur rejette (`400`) sans même regarder le
  reste.

### Codes d'état utilisés

| Code | Sens dans ce contrat |
|---|---|
| `200` | Succès, corps attendu |
| `204` | Succès, aucun corps (suppression) |
| `400` | Requête mal formée (chemin invalide, en-tête manquant) |
| `401` | Jeton absent, invalide ou expiré |
| `403` | Jeton valide mais droits insuffisants (projet en lecture seule) |
| `404` | Projet ou fichier inconnu |
| `409` | **Conflit** : l'empreinte attendue ne correspond plus (voir plus bas) |
| `413` | Corps trop volumineux |
| `429` | Trop de requêtes ; le client attend et réessaie |
| `500` | Panne serveur ; le client réessaie plus tard, sans perdre son état |

Tout autre code est traité par le client comme une panne : il abandonne
proprement l'opération en cours et laisse le projet local intact.

---

## Points d'entrée

### `POST /api/session`

Ouvre une session et délivre un jeton. **C'est la première requête que
l'application émet de toute son existence** : rien n'est envoyé, ni même
préparé, avant que l'auteur ne demande explicitement à se connecter.

```
POST /api/session
Content-Type: application/x-www-form-urlencoded

user=elise&secret=…
```

Le secret voyage dans le CORPS, jamais dans l'URL : une URL se retrouve dans
les journaux du serveur, dans l'historique des intermédiaires, dans les
rapports d'erreur. Un corps, non.

```
200 OK
Content-Type: text/plain; charset=utf-8

token 9f3c…
user Élise
```

`401` si les identifiants sont refusés. Le message renvoyé ne doit **pas**
distinguer « compte inconnu » de « mot de passe faux » : cette différence
permettrait d'énumérer les comptes existants.

### `DELETE /api/session`

Invalide le jeton courant. L'application appelle ce point d'entrée à la
déconnexion, mais **ne dépend pas de sa réussite** : elle oublie le jeton de
toute façon, et libère la connexion.

### `GET /api/version`

Poignée de main, sans authentification. Permet au client de refuser un serveur
incompatible **avant** d'envoyer quoi que ce soit.

```
200 OK
Content-Type: text/plain; charset=utf-8

api 1
server editeur-sync 0.1
```

Format : une clé, une espace, une valeur, une ligne par entrée. Les clés
inconnues sont ignorées ; seule `api` est obligatoire. Si `api` diffère de la
version attendue, le client s'arrête là et le dit à l'auteur.

### `GET /api/projects/{projectId}/index`

L'inventaire du projet tel que le serveur le connaît.

```
200 OK
Content-Type: text/plain; charset=utf-8

<le format d'inventaire de io/ProjectIndex>
```

Le corps est **exactement** le format produit par `io::serializeIndex` et lu
par `io::parseIndex` : celui qui est déjà écrit sur le disque de l'auteur.
Un seul format pour le disque et pour le réseau, donc un seul analyseur — déjà
testé, déjà passé au fuzzer, déjà tolérant aux lignes abîmées.

`404` si le projet n'est pas appairé côté serveur.

### `GET /api/projects/{projectId}/files/{chemin}`

Le contenu d'un fichier, tel quel.

```
200 OK
Content-Type: application/octet-stream
ETag: "<sha256 hexadécimal>"

<octets>
```

L'`ETag` est l'empreinte SHA-256 du corps. **Le client la vérifie** : un corps
qui ne correspond pas à son empreinte est refusé, quelle qu'en soit la cause
(transfert coupé, intermédiaire bavard, serveur fautif).

### `PUT /api/projects/{projectId}/files/{chemin}`

Dépose un fichier.

```
PUT /api/projects/…/files/roman.md
If-Match: "<sha256 attendu côté serveur>"
Content-Type: application/octet-stream

<octets>
```

`If-Match` porte l'empreinte que le client **croit** être celle du serveur —
celle de sa dernière synchronisation. Deux cas particuliers :

- `If-Match: *` pour un fichier que le client croit **absent** du serveur ;
- absence d'`If-Match` : refusée (`400`). Écraser sans savoir ce qu'on écrase
  est précisément ce que ce protocole doit rendre impossible.

Réponses : `200` avec le nouvel `ETag`, ou **`409`** si l'empreinte ne
correspond plus. Le `409` n'est pas une erreur : c'est l'information qu'un
autre poste a modifié le fichier entre-temps. Le client le remonte comme un
conflit et laisse l'auteur trancher, exactement comme pour une modification
extérieure détectée sur le disque.

### `DELETE /api/projects/{projectId}/files/{chemin}`

Même règle d'`If-Match`, mêmes réponses. `204` en cas de succès.

---

## Reprise après coupure

Aucune opération n'est supposée aboutir. Le client applique son plan de
synchronisation **fichier par fichier**, et note dans le journal local
(`io/ChangeLog`) ce qui a réellement abouti. Une coupure laisse donc un projet
partiellement synchronisé, ce qui est un état parfaitement décrit : le plan
suivant reprend là où celui-ci s'est arrêté, puisqu'il se recalcule à partir
des inventaires et non d'une file d'attente.

C'est la raison pour laquelle le plan est **recalculé** à chaque fois plutôt
que mémorisé : un état recalculable ne peut pas devenir faux.

---

## Quand la première requête part

Jamais avant que l'auteur ne clique sur « Se connecter ». Ce n'est pas une
politesse d'interface mais une propriété du code : tant qu'aucune connexion
n'est ouverte, **il n'existe aucun objet capable d'émettre une requête** — pas
de session, pas de poignée système, pas de fil d'exécution (voir
`net/Connection.h`). Un test monte l'application entière et compte les
transports créés : zéro.

Ensuite, l'ordre est toujours le même :

1. `GET /api/version` — sans jeton, puisqu'on ne connaît pas encore ce serveur.
2. `POST /api/session` — les identifiants, dans le corps.
3. Rien d'autre, tant qu'aucun projet n'est **appairé**. Être connecté ne
   suffit pas : l'appairage est une décision distincte, prise projet par
   projet, et « jamais pour ce projet » est définitif.

La déconnexion appelle `DELETE /api/session`, oublie le jeton et **détruit le
transport** : les poignées système sont rendues, on revient exactement à
l'état d'avant la connexion.

## Ce que le protocole ne fait pas encore

- **Pas de création de compte** : les comptes sont créés côté serveur au
  jalon D. Le client sait s'y connecter, pas s'y inscrire.
- **Pas de session persistante** : le jeton vit en mémoire et meurt avec
  l'application. Un jeton écrit sur le disque demanderait d'être protégé, et
  une protection approximative vaut moins que pas de fichier du tout. À
  reconsidérer avec un vrai magasin de secrets du système.
- **Pas de transfert par morceaux** : un fichier voyage entier. Un manuscrit
  pèse quelques centaines de kilo-octets ; découper compliquerait tout pour un
  gain nul. À revoir le jour où les illustrations compteront vraiment.
- **Pas de notification** : le client demande, le serveur répond. Rien ne
  pousse vers le client.
- **Pas d'édition à plusieurs** : le partage en lecture seule vient d'abord,
  conformément à la décision du 26 juillet.
