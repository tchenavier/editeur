// CardIO.h
// -----------------------------------------------------------------------
// Lecture et écriture des fiches de worldbuilding (Phase 2).
//
// FORMAT : une fiche = un fichier Markdown, en-tête YAML + corps libre.
// Même philosophie que le manuscrit (cf. cadrage : « format standard et
// ouvert, lisible par n'importe quel éditeur, pas d'enfermement
// propriétaire ») : la fiche reste consultable et modifiable dans
// n'importe quel éditeur de texte, même sans notre application.
//
//   ---
//   id: "aria"
//   type: "personnage"
//   titre: "Aria de Val"
//   tags:
//     - "noblesse"
//   age: "24 ans"
//   description_physique: |
//     Grande, cheveux sombres.
//     Une cicatrice à la joue.
//   relations:
//     - "bran"
//   ---
//
//   Notes libres, en Markdown.
//
// Les clés « id », « type », « titre » et « tags » sont RÉSERVÉES ; toutes
// les autres sont des champs de fiche, tels que définis par le schéma de
// son type. Rien n'est perdu si un champ ne figure pas dans le schéma :
// il est conservé (cf. CardModel.h, tolérance aux évolutions).
//
// Le corps du fichier est libre et se retrouve dans le champ « notes ».
//
// RANGEMENT : un dossier par type, un fichier par fiche —
// « <racine>/<type>/<id>.md » — pour que l'arborescence reste navigable
// à la main. L'écriture est atomique (comme pour le manuscrit).
// -----------------------------------------------------------------------

#pragma once

#include "CardModel.h"

#include <filesystem>
#include <string>

namespace io {

// Nom du champ recevant le corps libre du fichier.
extern const char* const kCardNotesField;

// --- Sérialisation en mémoire ----------------------------------------
// 'registry' est facultatif : s'il est fourni, les champs que le schéma
// déclare comme des LISTES gardent la forme « liste à tirets » même
// lorsqu'ils n'ont qu'une valeur. La structure du fichier ne dépend donc
// pas du nombre de valeurs, et ajouter un élément produit une différence
// minimale d'une sauvegarde à l'autre.
std::string serializeCard(const core::Card& card,
                          const core::SchemaRegistry* registry = nullptr);

// Analyse une fiche. Retourne false si le texte n'a pas d'en-tête
// exploitable (pas d'identifiant) : on préfère refuser explicitement
// plutôt que de fabriquer une fiche à moitié valide.
bool parseCard(const std::string& text, core::Card& out);

// --- Fichiers ---------------------------------------------------------
// Chemin de la fiche sous 'root' : <root>/<type>/<id>.md
std::filesystem::path cardPath(const std::filesystem::path& root, const core::Card& card);

// Écrit la fiche (création des dossiers au besoin, écriture atomique).
// 'registry' facultatif : voir serializeCard.
bool saveCard(const core::Card& card, const std::filesystem::path& root,
              std::string* errorMessage = nullptr,
              const core::SchemaRegistry* registry = nullptr);

// Supprime le fichier d'une fiche. Retourne false si le fichier
// n'existait pas ou n'a pas pu être supprimé.
bool removeCardFile(const core::Card& card, const std::filesystem::path& root,
                    std::string* errorMessage = nullptr);

// Charge toutes les fiches trouvées sous 'root' (parcours récursif des
// fichiers .md). Les fichiers illisibles sont IGNORÉS : un fichier
// abîmé ne doit pas empêcher d'ouvrir toute la bible narrative.
// 'skipped' (si non nul) reçoit le nombre de fichiers ignorés.
bool loadLibrary(core::CardLibrary& library, const std::filesystem::path& root,
                 size_t* skipped = nullptr, std::string* errorMessage = nullptr);

// Écrit toute la bibliothèque sous 'root'.
bool saveLibrary(const core::CardLibrary& library, const std::filesystem::path& root,
                 std::string* errorMessage = nullptr,
                 const core::SchemaRegistry* registry = nullptr);

// Fabrique un identifiant de fichier lisible à partir d'un titre
// (« Aria de Val » -> « aria-de-val »), en retirant les accents et tout
// caractère problématique pour un nom de fichier. Retourne « fiche » si
// le titre ne donne rien d'exploitable.
std::string slugify(const std::string& title);

} // namespace io
