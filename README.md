# Éditeur de texte et conception d'histoire

Un éditeur de texte pour **auteurs de roman**, qui réunit trois choses que les
traitements de texte séparent : écrire le manuscrit, tenir la bible narrative
(personnages, lieux, langues, religions, créatures, magie), et lier les deux
pour repérer les incohérences.

Puis en faire un livre : export PDF et EPUB.

Tout fonctionne **hors ligne**. Aucun compte n'est requis, et aucune donnée ne
quitte la machine.

---

## Ce qu'il sait faire

**Écrire.** Texte riche (gras, italique, souligné, barré, exposant, indice) avec
le formatage stocké *par caractère* — indispensable en français pour « 1ᵉʳ » ou
« 2ᵈᵉ ». Titres H1 à H6, plan du manuscrit réorganisable par glisser-déposer,
compteur de mots par chapitre, trois thèmes dont un crème moins agressif pour
les yeux, raccourcis clavier reconfigurables, alertes de fin de session
d'écriture et rappels par date.

**Ne rien perdre.** Sauvegarde en **Markdown** avec métadonnées YAML — un format
ouvert, lisible dans n'importe quel éditeur, sans enfermement propriétaire.
Écriture atomique (fichier temporaire puis renommage) pour survivre à une
coupure, sauvegardes horodatées automatiques, alerte si le fichier a changé en
dehors de l'éditeur, et confirmation avant de quitter sans enregistrer.

**Corriger.** Correcteur orthographique français hors ligne : les mots inconnus
se soulignent pendant la frappe, un clic droit propose les corrections et les
synonymes. Un dictionnaire personnel accompagne le manuscrit — les noms inventés
d'un roman ne figureront jamais dans un dictionnaire général.

**Construire un monde.** Six types de fiches fournis, et un modèle **piloté par
schéma** : ajouter un type de fiche ne demande aucune modification du reste du
code, pas même de l'interface, qui génère ses formulaires à partir du schéma.
Tags transversaux, relations entre fiches, illustrations.

**Tenir la cohérence.** Les noms de fiches présents dans le texte sont détectés
et soulignés ; Ctrl+clic ouvre la fiche. Recherche globale texte *et* fiches.
Analyse de cohérence : liens cassés, fiches jamais mentionnées, et
**personnages morts réapparus** — toute mention après le chapitre de décès passe
en rouge.

**Relire.** Comparaison de deux versions côte à côte, qui ignore le style et
absorbe les décalages : insérer un mot ne fait pas passer tout le reste du
paragraphe pour modifié. Et si un correcteur extérieur a rendu le texte **d'un
seul tenant**, paragraphes perdus, l'éditeur le voit, compare alors phrase par
phrase, et sait **rendre au manuscrit le découpage de la version précédente**,
titres compris, sans toucher au texte corrigé.

**Publier.** Export PDF par le moteur de rendu du système (page de titre,
couverture, table des matières, saut de page par chapitre) et export EPUB
construit intégralement. La bible narrative s'exporte aussi, seule.

---

## Compiler et lancer

Deux dépendances de code, **uniquement des en-têtes**, plus les dictionnaires.
Tout se récupère automatiquement. Rien à installer pour l'utilisateur final :
le moteur de rendu est celui déjà présent sur le système.

```bat
scripts\fetch_deps.bat      REM une seule fois : dépendances et dictionnaires
scripts\build_app.bat       REM compile build\editeur.exe
build\editeur.exe           REM à lancer depuis la RACINE du projet
```

Prérequis : Windows et Visual Studio (C++17). Les couches métier ne dépendent
d'aucune bibliothèque graphique et se compilent partout ; seule la fenêtre est
spécifique à la plateforme.

Voir [`scripts/README.md`](scripts/README.md) pour les détails, les pièges
rencontrés et la procédure d'audit.

---

## Choix techniques

**C++17 avec [webview](https://github.com/webview/webview) et une interface
HTML/CSS/JS maison.** Pas de Qt : pour ne pas dépendre d'un tiers unique dont
un changement de licence ou une rupture de compatibilité pourrait arrêter le
projet. Pas de bibliothèque JavaScript d'édition (ProseMirror, TipTap) : le
moteur d'édition est écrit ici, pour garder la main sur le cœur de l'outil.

**Le moins de dépendances possible, quitte à écrire plus.** L'archive ZIP de
l'EPUB, l'empreinte SHA-256, le lecteur de thésaurus et le moteur de
conjugaison du correcteur sont écrits dans le projet. À chaque fois pour la même
raison : quelques centaines de lignes vérifiables, couvertes par nos tests et
notre audit, valent mieux qu'une dépendance à suivre pendant des années.

**Découpage en couches, vérifié par les tests.**

| Couche | Rôle |
|---|---|
| `core/` | Logique métier : modèle de texte, historique, recherche, fiches, correcteur, comparaison. Aucune dépendance graphique. |
| `io/` | Sérialisation : Markdown, YAML, fiches, XHTML, archives, EPUB, dictionnaires, inventaire et plan de synchronisation. |
| `utils/` | Transverse : UTF-8, chemins, empreintes. |
| `net/` | Réseau : le contrat HTTP, le transport isolé derrière une interface, le client de protocole. Rien au-dessus ne connaît WinHTTP. |
| `Bridge/` | Le pont C++ ↔ JS. Ne connaît pas webview, donc testable sans interface. |
| `ui/` | Interface auto-contenue, sans dépendance externe. N'est qu'une vue. |

Chaque suite de tests unitaires ne compile que sa couche : c'est ce qui *prouve*
que `core/` tient sans `io/`, et `io/` sans le pont.

---

## Qualité

**19 suites, 181 cas de tests** sans framework externe, et un audit de sécurité
reproductible en six étapes : compilation stricte (avertissements traités comme
des erreurs), détecteur d'erreurs mémoire, fuzzing, fuites mémoire sur toutes
les couches, analyse statique. Tout au vert.

```bat
scripts\build_tests.bat        REM les 19 suites
scripts\security_audit.bat     REM les 6 vérifications
scripts\fuzz_long.bat          REM campagne de fuzzing longue
```

L'exécutable est compilé avec les protections d'exécution de la plateforme
(Control Flow Guard, ASLR, DEP). L'interface n'utilise **aucune** construction
de page à partir de chaînes ni évaluation dynamique de code : le texte de
l'auteur ne peut jamais y être interprété comme du code.

---

## Feuille de route

| Phase | Contenu | État |
|---|---|---|
| 1 | Éditeur de texte riche, Markdown, plan, recherche, correcteur | **Réalisée** |
| 2 | Fiches de worldbuilding, modèle extensible | **Réalisée** |
| 3 | Lien texte ↔ fiches, détection d'incohérences | **Réalisée** |
| 4 | Export PDF / EPUB, bible narrative | **Réalisée** |
| 5 | Comptes, cloud, synchronisation | Amorcée — jalons A, B et C |
| 6 | Site web : publication, forum, messagerie | À venir |

La Phase 5 suit un principe non négociable : **la connexion ne sera jamais
obligatoire.** L'application reste complète sans compte et sans réseau ; les
fichiers locaux restent la référence, le cloud n'en sera qu'un miroir, et rien
ne partira sans accord explicite, projet par projet. Ce n'est pas une
intention mais **un test** : une suite vérifie que tout un parcours d'écriture
— frappe, styles, enregistrement, fiches, cohérence, export — réussit sans
réseau ni compte. Si un appel réseau se glissait un jour dans une commande
d'édition, elle tomberait.

**Rien de réseau n'existe tant qu'on ne se connecte pas.** Pas seulement « rien
n'est envoyé » : aucune session, aucune poignée système, aucun fil d'exécution
n'est créé avant que l'auteur ne le demande, et tout est libéré à la
déconnexion. Un test monte l'application entière et compte les transports
créés : zéro. Écrire hors ligne ne coûte donc rien — ni en mémoire, ni au
démarrage.

Être connecté ne suffit pas non plus : chaque projet est **appairé
séparément**, avec trois réponses possibles — appairer, plus tard, ou jamais
pour celui-ci. Le refus est retenu et voyage avec le projet.

Les jalons A (identité de projet, inventaire, journal), B (contrat HTTP
documenté, transport isolé, client réel, moteur de synchronisation) et C
(comptes et appairage) sont faits. Reste le serveur.

---

## Composants tiers

Récupérés par `fetch_deps.bat`, épinglés sur une version précise, non versionnés
ici :

| Composant | Rôle | Licence |
|---|---|---|
| [webview](https://github.com/webview/webview) | Fenêtre et moteur de rendu | MIT |
| SDK WebView2 (en-têtes) | Interface du moteur sous Windows | Microsoft |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON du pont (`Bridge/json.hpp`) | MIT |
| Dictionnaire français (Grammalecte) | Correcteur orthographique | MPL 2.0 |
| Thésaurus français (F. Labbé) | Synonymes | LGPL 2.1+ |

## Licence

Non encore choisie. En l'absence de licence, le code reste sous droit d'auteur
exclusif : personne ne peut légalement le réutiliser. À trancher avant toute
diffusion réelle.
