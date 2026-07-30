// MarkdownIO.h
// -----------------------------------------------------------------------
// Sérialisation du document vers Markdown (.md) et retour.
//
// Format (cf. cadrage : format standard et ouvert, pas d'enfermement
// propriétaire) :
//   - métadonnées en en-tête YAML délimité par "---" (titre, auteur,
//     couverture, résumé) ;
//   - un bloc = une ligne, séparés par une ligne vide ; titres "#" à
//     "######" ;
//   - styles en ligne : **gras**, *italique*, ~~barré~~, et balises
//     HTML (autorisées en Markdown) pour ce qui n'a pas d'équivalent :
//     <u>souligné</u>, <sup>exposant</sup>, <sub>indice</sub>.
//     L'exposant permet le cas "1<sup>er</sup>" : style sur une PARTIE
//     d'un mot, que le modèle par caractère sait représenter.
//
// Limites assumées de cette première version (documentées plutôt que
// silencieuses) :
//   - un saut de ligne DANS un bloc est sérialisé comme une espace
//     (l'éditeur crée un nouveau bloc à chaque Entrée, ce cas ne doit
//     pas se produire en usage normal) ;
//   - un paragraphe entièrement vide n'a pas de représentation Markdown
//     et disparaît à la sauvegarde.
//
// Robustesse : parse() accepte n'importe quelle entrée (fichier écrit à
// la main, corrompu, vide...) et produit toujours un document
// exploitable, sans jamais lancer d'exception. Les marqueurs non fermés
// s'arrêtent simplement en fin de bloc.
// -----------------------------------------------------------------------

#pragma once

#include "TextModel.h"

#include <filesystem>
#include <string>

namespace io {

// Sérialise le document complet : en-tête YAML (si au moins une
// métadonnée est renseignée) puis les blocs. Retourne de l'UTF-8.
std::string serialize(const core::Document& doc);

// Construit un document depuis un contenu Markdown complet (UTF-8,
// BOM tolérée). Garantit au moins un bloc (paragraphe vide si l'entrée
// est vide) pour que l'éditeur ait toujours un bloc 0 où écrire.
core::Document parse(const std::string& markdown);

// Sauvegarde ATOMIQUE (cf. cadrage) : écrit dans un fichier temporaire
// à côté de la destination, puis renomme. En cas de crash ou de coupure
// pendant l'écriture, le fichier d'origine reste intact.
// Retourne false et renseigne *errorMessage (si non nul) en cas d'échec.
bool saveToFile(const core::Document& doc,
                const std::filesystem::path& path,
                std::string* errorMessage = nullptr);

// Chargement. Ne modifie 'doc' qu'en cas de succès complet.
bool loadFromFile(core::Document& doc,
                  const std::filesystem::path& path,
                  std::string* errorMessage = nullptr);

// Écrit une sauvegarde HORODATÉE du document (cf. cadrage : « sauvegarde
// automatique + backups horodatés »). Les sauvegardes sont rangées dans
// un sous-dossier « <nom>_backups/ » à côté de 'mainPath', nommées
// « <nom>_AAAA-MM-JJ_HH-MM-SS.md ». Écriture atomique. On ne conserve que
// les 'keep' sauvegardes les plus récentes (les plus anciennes sont
// supprimées) ; 'keep' == 0 désactive la purge.
// Retourne false et renseigne *errorMessage en cas d'échec ; en cas de
// succès, *outBackupPath (si non nul) reçoit le chemin de la sauvegarde.
bool writeBackup(const core::Document& doc,
                 const std::filesystem::path& mainPath,
                 size_t keep,
                 std::string* outBackupPath = nullptr,
                 std::string* errorMessage = nullptr);

} // namespace io
