// EditorApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "editor.*" sur le Bridge et les câble au
// modèle de données (core::Document) et à l'historique (core::UndoManager).
//
// C'est la seule pièce qui connaît À LA FOIS le pont et le modèle :
//   JS  <->  Bridge (transport)  <->  EditorApi (traduction)  <->  core (métier)
//
// Commandes exposées :
//   editor.getDocument   -> état complet du document (pour le rendu JS)
//   editor.insertText    -> { block, pos, text }
//   editor.removeText    -> { block, pos, count }
//   editor.applyStyle    -> { block, pos, count, style }
//   editor.removeStyle   -> { block, pos, count, style }
//   editor.setFontSize   -> { block, pos, count, size }  (size = % de la
//                           taille normale, 100 = normal ; remplace la
//                           taille de la sélection, style conservé)
//   editor.setColor      -> { block, pos, count, color }  (color = "#rrggbb"
//                           ou "" pour revenir au défaut ; remplace la couleur
//                           de la sélection, style et taille conservés)
//   editor.toggleWordStyle -> { block, pos, style }  (ergonomie "par mot" :
//                             applique/retire le style sur le mot entier
//                             sous le curseur, cf. WordBoundary)
//   editor.wordCount     -> { total }
//   editor.undo          -> annule la dernière étape (groupée par mot)
//   editor.redo          -> rétablit la dernière étape annulée
//   editor.historyState  -> { canUndo, canRedo } (état des boutons)
//   editor.breakUndoGroup-> clôt le groupe de frappe (à appeler quand le
//                           curseur bouge : clic, flèches...)
//   editor.setBlockType  -> { block, type } (paragraphe <-> titre)
//   editor.splitBlock    -> { block, pos }  (touche Entrée : coupe le bloc)
//   editor.mergeBlock    -> { block }       (Retour arrière en début de bloc :
//                           fusionne dans le bloc précédent)
//   editor.moveBlocks    -> { start, count, before }  (réorganisation des
//                           chapitres par glisser-déposer)
//   editor.find          -> { query, rangeStart, rangeEnd, fromBlock, fromPos,
//                             caseSensitive, wholeWord, backward, wrap }
//                           -> occurrence trouvée + total (Ctrl+F)
//   editor.replaceRange  -> { block, start, count, text }  (remplace une
//                           occurrence : une seule étape d'annulation)
//   editor.replaceAll    -> { query, replacement, rangeStart, rangeEnd,
//                             caseSensitive, wholeWord } -> { count, blocks }
//
// Toute modification passe par ici : c'est donc ici, et seulement ici,
// que l'historique est alimenté (le modèle n'en sait rien).
//
// Les commandes de fichier (sauvegarde/ouverture Markdown) sont dans
// leur propre module : voir ManuscriptApi.h.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "TextModel.h"
#include "UndoManager.h"

namespace bridge {

class EditorApi {
public:
    // 'document' et 'undo' doivent rester valides toute la durée de vie
    // de l'API (le Bridge conserve des lambdas qui pointent dessus).
    EditorApi(core::Document& document, core::UndoManager& undo);

    // Enregistre toutes les commandes editor.* sur le pont donné.
    // Retourne false si un nom de commande était déjà pris.
    bool registerOn(Bridge& bridge);

private:
    core::Document& m_document;
    core::UndoManager& m_undo;

    // --- Gestionnaires (un par commande) ---
    json getDocument(const json& payload);
    json insertText(const json& payload);
    json removeText(const json& payload);
    json applyStyle(const json& payload);
    json removeStyle(const json& payload);
    json setFontSize(const json& payload);
    json setColor(const json& payload);
    json toggleWordStyle(const json& payload);
    json wordCount(const json& payload);
    json undoCmd(const json& payload);
    json redoCmd(const json& payload);
    json historyState(const json& payload);
    json breakUndoGroup(const json& payload);
    json setBlockType(const json& payload);
    json splitBlock(const json& payload);
    json mergeBlock(const json& payload);
    json moveBlocks(const json& payload);
    json find(const json& payload);
    json replaceRange(const json& payload);
    json replaceAll(const json& payload);

    // Sérialise tout le document ({ blocks: [...] }). Utilisé par
    // getDocument et par les commandes structurelles (les indices de
    // blocs changent : le JS re-rend tout).
    json documentJson();

    // --- Aides de validation ---
    // Récupère le bloc désigné par payload["block"], en vérifiant
    // existence et bornes. Lance std::runtime_error avec un message
    // clair sinon (le Bridge la transformera en réponse d'erreur).
    // (La lecture des champs eux-mêmes — requireIndex, requireString... —
    // est commune à tous les modules *Api : voir Bridge.h.)
    core::TextBlock& requireBlock(const json& payload);

    // Convertit un nom de style JS ("bold", "italic"...) vers TextStyle.
    // Lance si le nom est inconnu : on préfère un rejet explicite à un
    // style silencieusement ignoré.
    static core::TextStyle styleFromName(const std::string& name);
};

// Sérialise un bloc vers le JSON attendu par le rendu JS. Partagé avec
// ManuscriptApi (qui renvoie le document après un chargement).
json serializeBlock(const core::TextBlock& block);

// Nom du type de bloc tel que le voit l'interface ("p", "h1"... "h6").
// Exposé pour les modules qui n'ont pas besoin du bloc entier — la
// comparaison de versions, par exemple.
const char* blockTypeName(core::BlockType type);

} // namespace bridge
