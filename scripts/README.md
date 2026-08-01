# Compiler et lancer l'éditeur

## En bref

```bat
scripts\fetch_deps.bat      REM une seule fois : récupère les dépendances
scripts\build_app.bat       REM compile build\editeur.exe
build\editeur.exe           REM à lancer depuis la RACINE du projet
```

Pour les tests (aucune dépendance externe requise) : `scripts\build_tests.bat`.

---

## Les dépendances, et pourquoi elles sont si légères

L'application n'embarque pas de moteur de rendu : elle utilise celui déjà
installé sur le système (WebView2 sur Windows). C'est le choix du cadrage —
binaire léger, rien à installer pour l'auteur. Résultat : deux dépendances,
**uniquement des en-têtes**, aucune bibliothèque à distribuer.

| Dépendance | Version épinglée | Rôle |
|---|---|---|
| [webview/webview](https://github.com/webview/webview) (MIT) | `0.12.0` | Header C++ autonome qui crée la fenêtre et pilote le moteur de rendu |
| SDK WebView2 (Microsoft) | `1.0.4078.44` | En-têtes `WebView2.h` inclus par webview.h |

**Le runtime WebView2 n'est pas à installer** : il est fourni avec Windows 11
et avec Microsoft Edge.

Les versions sont **épinglées** dans `fetch_deps.bat` : une mise à jour amont
ne peut pas casser la compilation sans qu'on l'ait décidé. Pour monter de
version, changez `WV_TAG` / `WV2_VER` et recompilez.

### Récupération manuelle (si le script échoue)

1. `webview.h` →
   `https://raw.githubusercontent.com/webview/webview/0.12.0/core/include/webview/webview.h`
   à placer dans `third_party\webview\webview.h`
   *(depuis la 0.11, le header vit sous `core/include/webview/` et non plus
   à la racine du dépôt : c'est le piège classique.)*
2. SDK WebView2 → `https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4078.44`
   Un paquet NuGet est une archive ZIP : renommez-la en `.zip`, extrayez, et
   copiez `build\native\include\*.h` dans `third_party\webview2\`.

Aucune bibliothèque (`.lib`) n'est nécessaire : `webview.h` déclare lui-même
celles dont il a besoin (`#pragma comment(lib, ...)`) et charge
`WebView2Loader.dll` dynamiquement au démarrage.

---

## Points d'attention rencontrés

### 1. Lancer l'application depuis la racine du projet

`main_webview.cpp` charge l'interface depuis `ui/index.html` par un chemin
**relatif au répertoire courant**. Lancée depuis ailleurs, l'application
démarre quand même mais affiche une page de démonstration minimale au lieu
de l'éditeur. À terme, l'interface sera embarquée dans le binaire (prévu au
cadrage) et ce problème disparaîtra.

### 2. `<windows.h>` qui ne compile pas : ce n'était pas le SDK

Pendant longtemps ce projet a cru à un bug du SDK Windows 10.0.26100 :
`<windows.h>` refusait de compiler, `winbase.h` utilisant des types
(`STREAM_INFO_LEVELS`, `CREATEFILE2_EXTENDED_PARAMETERS`) avant qu'ils soient
définis. Le contournement était de cibler `_WIN32_WINNT 0x0501` avant tout
`#include`, ce qui désactivait les blocs concernés.

**Le vrai coupable était nous.** `Bridge/FileApi.h` masquait le `fileapi.h`
du SDK : les chemins `/I` sont fouillés **avant** les chemins système, et les
noms de fichiers sont insensibles à la casse sous Windows. Tout
`#include <fileapi.h>` fait depuis `winbase.h` atterrissait donc dans notre
en-tête, qui ne définit évidemment aucun de ces types.

Vérification en trois lignes, le jour où le doute revient :

```bat
echo #include ^<windows.h^> > essai.cpp && echo int main(){return 0;} >> essai.cpp
cl /nologo /c essai.cpp              REM compile
cl /nologo /c /IBridge essai.cpp     REM echoue si un en-tete masque le SDK
```

Le module a donc été renommé **`ManuscriptApi`** (fichiers
`Bridge/ManuscriptApi.{h,cpp}`, classe `bridge::ManuscriptApi`), et
`<windows.h>` compile désormais sans aucune macro de cible.

**Règle qui en découle** : un en-tête de ce projet ne doit jamais porter le
nom d'un en-tête système, même avec une casse différente. Les noms à éviter
sont ceux du SDK Windows (`fileapi.h`, `winbase.h`, `search.h`, `io.h`,
`memory.h`, `time.h`…) et ceux de la bibliothèque standard. `net/Transport.h`
est à surveiller si un en-tête système du même nom apparaissait un jour.

Les définitions `_WIN32_WINNT 0x0501` de `main_webview.cpp` et
`NativeDialog.cpp` sont restées : elles ne gênent rien et ces deux fichiers
n'utilisent aucune API plus récente. Elles ne sont simplement plus
nécessaires.

### 3. Export PDF : impression asynchrone

L'export PDF (Phase 4) passe par `ICoreWebView2_7::PrintToPdf`, c'est-à-dire
le moteur de rendu du webview lui-même (choix du cadrage). **Cette API est
asynchrone** : attendre sa fin dans le gestionnaire de commande bloquerait
la boucle de messages — donc l'impression elle-même, donc un interblocage.

La commande `export.pdf` rend donc la main immédiatement (`{ started:true }`)
et la fin est signalée au JS par un appel à `window.__exportPdfDone(...)`.
Toute modification de ce chemin doit préserver ce découpage.

Si le runtime WebView2 installé est trop ancien pour `ICoreWebView2_7`, la
commande échoue proprement et l'interface se rabat sur la boîte
d'impression du système (« Enregistrer au format PDF »).

### 4. Sous-système console

L'exécutable ouvre une console à côté de la fenêtre : voulu pendant le
développement (on y lit les messages du moteur). Pour une version
distribuée, ajouter au script de compilation :

```
/link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
```

---

## Ce qui reste non couvert par les tests

`scripts\build_tests.bat` couvre les couches `core/`, `io/`, `utils/` et le
pont. Restent vérifiables **uniquement à la main, en lançant l'application** :

- l'assemblage de `main_webview.cpp` (liaison `cppInvoke`, chargement de
  l'interface, interception de la fermeture de fenêtre, impression PDF) ;
- les dialogues natifs de `NativeDialog.cpp`, qui ouvrent de vraies fenêtres
  et ne peuvent donc pas être appelés depuis une suite automatisée ;
- l'interface `ui/index.html` elle-même.

Le fichier EPUB produit, lui, **est** vérifié automatiquement (`test_epub`) :
structure de l'archive, ordre des entrées, contenu du fichier de package et
échappement. Sa lecture par un vrai lecteur ZIP et la validité XML de tous
ses fichiers ont été contrôlées à part, avec `System.IO.Compression` et
`XmlDocument` (PowerShell) sur un livre de démonstration.

---

## L'interface : trois fichiers, une page

`ui/` contient trois fichiers voisins — `index.html` (structure), `style.css` et
`app.js` — parce qu'un fichier unique de 220 Ko est pénible à parcourir.

**Mais l'application ne charge pas ces fichiers séparément.** Sa page lui est
fournie par `set_html`, c'est-à-dire **sans origine** : un
`<link href="style.css">` ou un `<script src="app.js">` n'y désignerait aucun
emplacement atteignable et ne chargerait rien. `loadUiHtml`
(`Bridge/main_webview.cpp`) remplace donc ces deux balises par le contenu des
fichiers avant de livrer la page. L'interface reste ainsi parfaitement
auto-contenue à l'exécution, comme l'exige le cadrage, tout en restant découpée
à l'écriture.

Deux conséquences à connaître :

- **Les deux balises sont des repères, pas de la décoration.** Les modifier dans
  `index.html` sans modifier `loadUiHtml` fait basculer l'application sur sa page
  de repli. Le code refuse d'ailleurs de livrer une interface amputée : si un des
  trois fichiers manque, il rend la page de repli plutôt qu'un éditeur sans style.
- **Ouvrir `ui/index.html` dans un navigateur fonctionne toujours** : là, les
  balises sont résolues normalement. C'est ainsi qu'on teste l'interface sans
  lancer l'application (le backend simulé prend le relais).

Modifier l'interface ne demande **pas** de recompiler : les trois fichiers sont
relus à chaque lancement.

## Comparer deux versions

Bouton « ⇄ Comparer » : on choisit un fichier (une sauvegarde horodatée, une
version corrigée) et les deux textes s'ouvrent côte à côte, différences en
évidence. Le même écran s'ouvre depuis le dialogue de **conflit de fichier**,
où « écraser ou renoncer » était jusqu'ici un choix aveugle.

Deux propriétés, et comment elles sont obtenues (`core/Diff`) :

- **Le style est ignoré** — gratuitement. Le modèle range le texte d'un côté et
  les plages de gras/italique de l'autre : comparer les textes ignore le
  formatage par construction, sans rien filtrer.
- **Le décalage est absorbé** — c'est tout le travail. Insérer un mot décale la
  suite du paragraphe ; une comparaison position par position signalerait tout
  le reste comme modifié. L'alignement se fait donc à **deux étages** : les
  blocs d'abord (un paragraphe ajouté ne contamine pas ses voisins), puis les
  mots dans les blocs qui diffèrent.

Méthode d'alignement : retrait des débuts et fins communs (une correction est
presque toujours locale), puis appui sur les éléments **uniques de chaque côté**
— ils ne peuvent correspondre qu'à un seul vis-à-vis et découpent le problème en
morceaux sûrs — et enfin, pour le résidu, une plus longue sous-séquence commune
**bornée en mémoire** : au-delà, le segment est déclaré réécrit plutôt que de
faire enfler un calcul quadratique.

Ce qui compte comme une différence : **la casse et les accents** (« Le chat »
corrigé en « le chat » est une modification, et « eleve » n'est pas « élevé »).
Ce qui n'en est pas une : espace insécable, apostrophe courbe, espaces
multiples — les signaler serait du bruit constant.

**L'affichage n'utilise aucune synchronisation de défilement.** Les deux textes
sont posés dans **une seule grille à deux colonnes** : chaque ligne porte les
deux versions d'un même passage, donc elles restent alignées par construction,
sans dérive, et un paragraphe ajouté d'un côté laisse un vide hachuré en face au
lieu de décaler tout le reste. Mesuré : chaque paire de cellules partage
exactement le même sommet.

## Ajouter un module

Rien à déclarer nulle part. `scripts\sources.bat` **découvre** les sources :
tout `*.cpp` de `core/`, `io/`, `utils/` et `Bridge/` en fait partie, sauf les
`test_*.cpp` et trois fichiers à statut particulier (`fuzz_parsers.cpp`,
`leak_check.cpp`, `main_webview.cpp`). Il expose `SRC_CORE`, `SRC_IO`,
`SRC_UTILS`, `SRC_BRIDGE` et `SRC_ALL`, que tous les autres scripts utilisent.

Seule exception : **les suites de tests unitaires gardent une liste minimale**,
et c'est délibéré. C'est ce qui prouve le découpage en couches du cadrage —
que `core/` compile sans `io/`, et `io/` sans le pont. Leur donner `SRC_ALL`
ferait disparaître cette garantie. La suite d'intégration `test_bridge`, elle,
utilise `SRC_ALL` puisqu'elle assemble tout par nature.

Si vous ajoutez une suite de tests, il reste deux lignes à écrire dans
`build_tests.bat` : sa compilation et son `call :run`.

## Audit de sécurité

`scripts\security_audit.bat` — à relancer avant chaque jalon. Six vérifications,
toutes bloquantes :

| # | Vérification | Ce qu'elle attrape |
|---|---|---|
| 1 | Compilation stricte `/W4 /WX /permissive-` | Tout avertissement, traité comme une erreur — **et** les deux programmes d'audit, compilés d'emblée pour qu'un `#include` manquant tombe en 30 s et non après 8 minutes de sanitizer |
| 2 | AddressSanitizer sur la suite du pont | Débordements, use-after-free, accès hors limites |
| 3 | Fuzzing sous ASan (40 000 entrées) | Plantages sur fichiers malformés et noms d'entrées d'archive |
| 4 | AddressSanitizer sur l'export | Erreurs d'un octet dans les en-têtes ZIP / EPUB |
| 5 | Fuites mémoire (tas de débogage CRT) | Toutes les couches, **pont compris**, chemins d'erreur inclus |
| 6 | Analyse statique `/analyze` | Défauts prouvables sur des chemins que les tests n'exécutent pas |

Les 2, 3 et 4 n'observent que ce que le test exécute ; la 6 raisonne sur tous
les chemins. Les deux approches sont complémentaires — d'où leur présence
conjointe.

### Campagnes de fuzzing longues

L'audit se limite à 40 000 entrées pour rester lançable à chaque jalon. Pour
une passe de fond (avant une diffusion, par exemple) :

```bash
scripts\fuzz_long.bat 2000000
```

Compter ~2 ms par entrée sous AddressSanitizer, soit un peu plus d'une heure
pour deux millions — **chiffre mesuré, à remesurer dès qu'un analyseur rejoint
le fuzzer** : l'arrivée de la comparaison et du redécoupage a fait passer le
coût de 1,5 à 3,4 ms avant qu'on ne les limite à une itération sur quatre. La
**graine** est affichée au démarrage et rappelée en cas
d'incident : c'est elle qui rend un plantage reproductible
(`build\security\fuzz.exe <itérations> <graine>`, depuis un invite *x64 Native
Tools* — sinon la DLL d'ASan reste introuvable et le programme s'arrête
aussitôt avec `0xC0000135`).

Le fuzzer combine trois générateurs, dont un par **mutation d'entrées
valides** (bits retournés, octets insérés ou supprimés, morceaux dupliqués,
fichier tronqué). C'est de loin le plus efficace : mesuré sur 3 000 tirages,

| Générateur | Documents structurés | Métadonnées lues | Fiches valides |
|---|---|---|---|
| mutation | 1112 | 1137 | 773 |
| aléatoire | 20 | 0 | 0 |

Un tirage purement aléatoire est presque toujours rejeté dès les premières
lignes ; les entrées mutées, elles, franchissent les validations et vont
chercher le cœur des analyseurs. **Le corpus doit donc rester fidèle à ce que
l'éditeur écrit vraiment** — avec des clés YAML inventées, la mutation
n'atteignait jamais la lecture des métadonnées (piège rencontré et corrigé).

L'exécutable distribué est compilé avec `/guard:cf` (Control Flow Guard) et lié
avec `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`. À vérifier après un changement du
script de compilation :

```bash
dumpbin /headers build\editeur.exe
```

(doit mentionner *Control Flow Guard*, *Dynamic base*, *NX compatible* et
*High Entropy Virtual Addresses*).

**Pièges des scripts `.bat`**

*Parenthèses dans les messages.* Jamais de parenthèses dans un message à
l'intérieur d'un bloc `if ( ... )` — `cmd` prend la parenthèse fermante du
message pour la fin du bloc, et le script casse à l'analyse, même quand la
condition est fausse.

*Ne jamais modifier un `.bat` pendant qu'il tourne.* `cmd` ne charge pas le
script en mémoire : il relit le fichier **à chaque commande**, en repartant
d'une position mémorisée en octets. Modifier le fichier pendant l'exécution
décale cette position, et `cmd` reprend au milieu d'une ligne — il exécute
alors n'importe quoi et affiche des erreurs sans rapport. Rencontré pour de
vrai : une campagne de fuzzing de deux heures s'est terminée sur
`*** ECHEC de compilation ***` alors qu'elle avait parfaitement réussi, parce
que le commentaire d'en-tête du script avait été corrigé entre-temps. Le
résultat était bon, le compte rendu faux. **Attendre la fin, ou éditer une
copie.**

## Suites de tests

| Suite | Couvre |
|---|---|
| `test_model` | Modèle de texte : styles par caractère, insertion/suppression |
| `test_blocks` | Opérations structurelles : couper, fusionner, déplacer |
| `test_undo` | Historique groupé par mot, étapes structurelles |
| `test_search` | Recherche / remplacement (casse, mots entiers, portée) |
| `test_cards` | Modèle de fiches : extensibilité, tags, purge des références |
| `test_cardio` | Sérialisation des fiches (YAML), aller-retour disque |
| `test_markdown` | Sérialisation du manuscrit, écriture atomique, backups |
| `test_htmltext` | Rendu XHTML du texte : échappement, imbrication, tailles |
| `test_zip` | Écrivain ZIP : CRC, noms d'entrées refusés, structure binaire |
| `test_epub` | Structure « livre », EPUB (manuscrit et bible narrative) |
| `test_speller` | Règles d'affixes, formes fléchies, correction, synonymes |
| `test_utils` | Empreinte SHA-256 (vecteurs officiels) et identifiants de projet |
| `test_project` | Inventaire du projet et journal des modifications |
| `test_diff` | Alignement de deux versions, découpage en phrases |
| `test_reflow` | Redécoupage d'un texte aplati d'après une version de référence |
| `test_sync` | Moteur de synchronisation : plan à trois inventaires, conflits, reprise |
| `test_net` | Client de protocole contre un transport simulé, réponses hostiles |
| `test_http` | Transport réel, contre un serveur jouet lancé par le test |
| `test_runner` | Exécution d'un plan : panne qui arrête tout, refus isolé qui n'arrête rien |
| `test_bridge` | Le pont de bout en bout : toutes les commandes, dont « tout marche hors ligne » et « aucun réseau tant qu'on ne se connecte pas » |

20 suites, 189 cas, sans framework externe.

Le serveur, lui, a ses propres tests en JavaScript (voir
[`../serveur/README.md`](../serveur/README.md)) : ils ne demandent ni base de
données ni configuration, mais Node.js — donc ils tournent sur la Debian, pas
sur cette machine.

Deux tests du pont ne vérifient pas une fonctionnalité mais une **promesse
tenue**, et méritent d'être conservés tels quels :

- *Test 25* exécute un parcours d'écriture complet sans réseau ni compte. Si
  un appel réseau se glisse dans une commande d'édition, il tombe.
- *Test 26* compte les transports réellement fabriqués pendant qu'on écrit,
  enregistre et consulte l'état du compte : **zéro**, jusqu'à la connexion
  explicite. Si quelqu'un ajoute une initialisation réseau au démarrage, il
  tombe aussi.

`test_http` est la seule suite qui ouvre de vraies connexions : elle lance son
propre serveur sur `127.0.0.1`, sur un port attribué par le système. Rien ne
sort de la machine, aucun port n'est réservé, et deux exécutions simultanées
ne se gênent pas.

---

## Correcteur orthographique et synonymes

Deux jeux de **données** (pas de code) sont récupérés par `fetch_deps.bat`, dans
`third_party/dictionnaires/` :

| Fichier | Contenu | Licence |
|---|---|---|
| `fr.aff` + `fr.dic` | Orthographe, Grammalecte v7.7 | MPL 2.0 |
| `thes_fr.dat` | Synonymes, format MyThes | LGPL 2.1+ |

**L'application fonctionne sans eux** : le correcteur reste simplement muet.
Les fichiers sont épinglés sur un commit précis du dépôt LibreOffice.

Le `.dic` n'est **pas** une liste de mots : c'est une liste de lemmes plus des
règles d'affixes. `io/AffixDictionary` les développe au chargement —
**84 782 lemmes donnent 454 299 formes** en environ une seconde, d'où le
chargement différé (l'interface le déclenche une fois la fenêtre affichée ;
écrire ne dépend pas du correcteur).

Ce qui est géré du format Hunspell : `FLAG long`, classes `PFX`/`SFX` avec
conditions, `FULLSTRIP`, `NEEDAFFIX`, `FORBIDDENWORD`, `CIRCUMFIX`, `KEEPCASE`,
`NOSUGGEST`, `REP`. Ce qui ne l'est pas, faute d'être utilisé par ce
dictionnaire : les mots composés (`COMPOUND*`), les alias de drapeaux (`AF`) et
les champs morphologiques. **Si le dictionnaire est mis à jour, revérifier ces
directives** avant de conclure que l'expanseur est en cause.

Trois règles de découpage, chacune pour une bonne raison :

- **L'apostrophe sépare** (« l'arbre » → « l » + « arbre »), et une petite liste
  fermée reconnaît les particules élidées. Développer toutes les formes élidées
  du dictionnaire quadruplerait sa taille pour rien.
- **Le trait d'union se teste en entier d'abord** (« arc-en-ciel » est dans le
  dictionnaire), et seulement s'il est inconnu, morceau par morceau.
- **La casse est tolérée, jamais les accents.** « ÉLISE » passe si « élise »
  est connu ; « eleve » n'est pas « élevé » — c'est tout l'intérêt d'un
  correcteur français.

Le **dictionnaire personnel** (`<nom>_dictionnaire.txt`, à côté du manuscrit)
contient les noms inventés du roman. Il suit le projet, se lit à l'œil nu, et
entre dans l'inventaire du projet.

### Corriger le thésaurus

Le dictionnaire de synonymes est **associatif plus que strictement
synonymique** : il mêle de vrais synonymes, des mots apparentés, et quelques
erreurs franches — « gâteau » y figure comme synonyme de « noir ».

**Aucun filtre automatique ne peut les distinguer**, et ce n'est pas faute
d'avoir essayé. La réciprocité paraissait prometteuse (si B est synonyme de A,
A l'est de B), mais mesurée sur les 422 898 couples du fichier elle se révèle
inutilisable : 98,9 % des couples sont réciproques, les erreurs le sont *aussi*
(l'entrée « gâteau » cite « noir » en retour), et les 4 122 couples asymétriques
qu'un tel filtre supprimerait sont en majorité de bons synonymes
(*abattu → fatigué*, *abolir → rompre*). Le critère fait exactement l'inverse de
ce qu'on attend de lui.

La correction vient donc de l'auteur : la croix à côté d'un synonyme, dans le
menu contextuel, l'écarte définitivement. Les couples sont conservés dans
[`data/synonymes_exclus.txt`](../data/synonymes_exclus.txt) — **un fichier du
projet, versionné**, et non le fichier de dictionnaire lui-même, qui est
retéléchargeable : toute retouche y serait effacée au prochain `fetch_deps.bat`,
sans le moindre message.

Les exclusions valent **dans les deux sens** : déclarer que « gâteau » n'est pas
un synonyme de « noir » vaut aussi pour l'inverse. C'est la même relation
fausse.

Le menu n'affiche que **huit synonymes** : les scories arrivent en fin de liste
(sur « fenêtre », les huit premiers sont tous justes).

---

## Suivi de projet (Phase 5, jalon A)

Chaque enregistrement met à jour deux fichiers techniques, rangés dans un
dossier voisin du manuscrit — `<nom>_sync/`, à côté de `<nom>_backups/` et
`<nom>_fiches/`, selon la même convention :

| Fichier | Rôle |
|---|---|
| `inventaire.txt` | Un fichier par ligne : empreinte, taille, date, chemin |
| `journal.txt` | Un changement par ligne, en ajout seul, compacté au-delà de 5 000 |

Les deux sont **en clair et lisibles** : l'auteur peut les ouvrir et comprendre
ce qui s'est passé. Ils fonctionnent **sans compte et sans réseau** — ils
serviront de fondation à la synchronisation, mais sont utiles seuls.

Deux principes à préserver si ce code évolue :

- **C'est l'empreinte du contenu qui décide qu'un fichier a changé, pas sa
  date.** Réenregistrer un fichier identique change sa date, pas son contenu :
  ce n'est pas une modification à propager.
- **Le journal s'écrit par ajout, pas par réécriture.** Une coupure ne peut
  alors abîmer que la dernière ligne, que la lecture ignore. Réécrire tout le
  fichier à chaque fois remettrait l'historique entier en jeu — le seul moment
  où on le fait est le compactage, et celui-là est atomique.

L'identifiant de projet est attribué **à la première sauvegarde** et rangé dans
l'en-tête YAML du manuscrit (clé `project`), donc il voyage avec le fichier. Il
est exposé en lecture seule par `file.getMetadata` : `file.setMetadata` refuse
de le modifier — une identité modifiable à la main n'en serait plus une.
