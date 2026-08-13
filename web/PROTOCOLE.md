# Contrat du site web

Le second serveur : celui qui est **exposé à Internet**. Il porte la
publication des textes, et plus tard le forum et la messagerie (Phase 6).

Version du contrat : **1**.

---

## Pourquoi deux serveurs, et ce que cela protège

Le serveur de synchronisation garde les **manuscrits en cours** — le travail
d'années. Celui-ci porte ce qui est **publié**, c'est-à-dire ce que l'auteur a
délibérément rendu lisible.

Les séparer signifie qu'une faille de la machine exposée ne donne pas accès aux
manuscrits. Encore faut-il ne pas recoller ce qu'on vient de séparer :

**Aucun mot de passe ne circule entre les deux serveurs, ni en clair ni sous
forme d'empreinte.** Chaque service a le sien. Une fuite de la base de ce
serveur-ci ne permet donc pas de se connecter à la synchronisation.

**Ce serveur ne parle jamais au serveur de synchronisation.** La circulation va
dans un seul sens : le serveur privé pousse un identifiant, ce serveur le
reçoit. Il n'a aucune adresse, aucun jeton, aucun moyen d'aller chercher quoi
que ce soit de l'autre côté.

**Ce serveur n'a pas accès aux manuscrits.** Les textes publiés lui sont
envoyés par l'éditeur, à l'initiative de l'auteur, morceau par morceau. Il ne
voit que ce qui lui a été donné à voir.

---

## Principes

**Le serveur n'accepte jamais de HTML.** Un texte publié arrive sous forme de
blocs — un type et du texte brut — et c'est le serveur qui fabrique la page.
Tout est échappé, et seuls les éléments d'une liste fermée sont produits. C'est
la même règle que pour l'export EPUB de l'éditeur, et pour la même raison : le
texte d'un auteur ne doit jamais pouvoir être interprété comme du code, sur une
page que des inconnus vont ouvrir.

**Le code d'état porte l'information machine, le corps la phrase humaine.**
Comme pour le serveur de synchronisation : aucun format d'erreur à analyser.

**Rien n'est public par défaut.** Une publication naît en brouillon. Elle
devient lisible sur lien privé, puis publique, par deux gestes distincts de
l'auteur.

---

## Forme générale

- Base : `https://<hôte>/`
- Les requêtes d'auteur portent `Authorization: Bearer <jeton>`, obtenu par
  `POST /api/session` (mot de passe **propre à ce site**).
- Les pages de lecture n'exigent rien : ce sont des pages web ordinaires.

### Codes d'état

| Code | Sens |
|---|---|
| `200` / `201` | Succès |
| `204` | Succès sans corps |
| `400` | Requête mal formée |
| `401` | Jeton absent, invalide ou expiré |
| `403` | Droits insuffisants (ce texte n'est pas le vôtre) |
| `404` | Inconnu — y compris « existe mais ne vous regarde pas » |
| `409` | Conflit (adresse déjà prise) |
| `413` | Trop volumineux |
| `429` | Trop de requêtes |
| `500` | Panne serveur |

---

## Comptes

Les comptes **ne se créent pas ici**. Ils naissent sur le serveur de
synchronisation, qui pousse l'identifiant — et rien d'autre.

### `POST /api/interne/compte`

Réservé au serveur de synchronisation. Authentifié par un secret partagé entre
les deux machines (`Authorization: Bearer <secret>`), et refusé si l'appelant
n'est pas sur le réseau local.

```
POST /api/interne/compte
Content-Type: application/x-www-form-urlencoded

user=elise
```

```
201 Created
Content-Type: text/plain; charset=utf-8

code 8F3K-2M9Q-7T4B
```

Le compte est créé **sans mot de passe**, dans l'état « à activer ». Le code
retourné est à usage unique et à remettre à l'auteur ; il expire au bout de
sept jours. Le serveur de synchronisation l'affiche, ne le conserve pas.

Si le compte existe déjà, `200` et un **nouveau** code : c'est ainsi qu'on
répare une activation perdue, sans porte dérobée.

### `POST /api/activation`

L'auteur choisit son mot de passe **de ce site**.

```
user=elise&code=8F3K-2M9Q-7T4B&secret=…
```

`400` si le code est faux ou périmé, avec le même message dans les deux cas.

### `POST /api/session` et `DELETE /api/session`

Identiques au contrat de synchronisation : identifiants dans le corps, jeton
retourné en clair, empreinte seule conservée.

---

## Publications

### `POST /api/textes`

Dépose un texte. C'est **l'éditeur** qui appelle, à la demande de l'auteur.

```
POST /api/textes
Content-Type: text/plain; charset=utf-8

titre Le Crépuscule de Regina
adresse crepuscule-de-regina
resume Une reine, un chasseur, un miroir.
etat brouillon
--
h1 Acte I : Le Destin de Cendres
p Le vent d'hiver hurlait contre les remparts.
p À seulement dix-sept ans, elle portait déjà le deuil.
```

En-tête « clé valeur » (le même format que partout ailleurs), puis une ligne
`--`, puis **un bloc par ligne** : un type, une espace, le texte brut.

Types acceptés : `p`, `h1` à `h6`. Tout autre type fait rejeter la ligne — on
ne devine pas.

Le texte est **brut** : ni balise, ni Markdown. Les retours à la ligne
séparent les blocs, un point c'est tout.

`201` avec l'adresse retenue, ou `409` si l'adresse est déjà prise par
quelqu'un d'autre.

### `PUT /api/textes/{adresse}`

Remplace un texte existant. Réservé à son auteur (`403` sinon, car il sait
déjà qu'il existe).

### `POST /api/textes/{adresse}/etat`

```
etat=publie
```

Trois états, et il faut deux gestes pour passer de l'un au public :

| État | Qui peut lire |
|---|---|
| `brouillon` | l'auteur seul |
| `lien` | qui connaît l'adresse — pas listé, pas indexé |
| `publie` | tout le monde, listé sur la page d'accueil |

### `DELETE /api/textes/{adresse}`

### `GET /api/textes`

La liste des textes de l'auteur connecté, tous états confondus.

---

## Lecture

### `GET /`

Les textes `publie`, du plus récent au plus ancien.

### `GET /t/{adresse}`

La page de lecture. Accessible pour `publie` et `lien` ; `404` pour un
brouillon, y compris à son auteur non connecté — un brouillon n'existe pas
pour le web.

### `GET /auteur/{nom}`

Les textes publics d'un auteur.

---

## Ce que le protocole ne fait pas encore

- **Pas de forum, pas de messagerie, pas de commentaires.** Ils viendront ;
  ils demandent une modération, et une modération demande d'y avoir réfléchi
  avant d'ouvrir la porte.
- **Pas d'inscription libre.** Les comptes viennent du serveur privé.
- **Pas de TLS** : nginx ou Caddy devant, comme pour l'autre serveur.
