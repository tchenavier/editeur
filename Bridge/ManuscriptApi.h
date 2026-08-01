// ManuscriptApi.h
// -----------------------------------------------------------------------
// Commandes "file.*" : sérialisation Markdown, ouverture et sauvegarde
// (écriture atomique via io/MarkdownIO), et métadonnées YAML.
//
// Module séparé d'EditorApi, comme prévu dans son en-tête : les
// commandes de fichier ne touchent pas à l'édition, elles orchestrent
// la couche io/.
//
// Commandes exposées :
//   file.save        -> { path }  sauvegarde atomique vers le chemin donné
//   file.load        -> { path }  remplace le document ; l'historique
//                       d'annulation est vidé (il n'a plus de sens).
//                       Le JS rappelle ensuite editor.getDocument pour
//                       re-rendre le nouveau contenu.
//   file.serialize   -> {}        le Markdown complet, sans toucher au
//                       disque (aperçu, tests, export futur)
//   file.getMetadata -> {}        { title, author, cover, summary }
//   file.setMetadata -> { title?, author?, cover?, summary? }
//                       (seuls les champs présents sont modifiés)
//   file.chooseOpen  -> { kind?, title? }  ouvre le dialogue natif
//                       « Ouvrir » et renvoie { cancelled } ou
//                       { cancelled:false, path }. 'kind' vaut
//                       "markdown" (défaut), "epub", "pdf" ou "image"
//                       (choix d'une couverture) : il fixe le filtre.
//   file.chooseSave  -> { kind?, title?, name? }  idem pour
//                       « Enregistrer sous » ('name' = nom proposé).
//   file.backup      -> { path, keep? }  écrit une sauvegarde horodatée
//                       à côté du fichier et purge les plus anciennes
//   file.setState    -> { dirty, path }  l'UI tient le C++ informé de
//                       l'état du document (modifié ? quel fichier ?), afin
//                       que la fermeture de la fenêtre puisse prévenir
//                       l'utilisateur et enregistrer si besoin
//
// Sécurité : les chemins viennent du JS embarqué (notre propre UI, via
// la boîte de dialogue native). Quand la version navigateur arrivera
// (Phase 5), ce module devra restreindre les chemins accessibles.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "TextModel.h"
#include "UndoManager.h"

#include <map>
#include <string>

namespace bridge {

class ManuscriptApi {
public:
    // Mêmes règles de durée de vie qu'EditorApi : 'document' et 'undo'
    // doivent survivre au Bridge.
    ManuscriptApi(core::Document& document, core::UndoManager& undo);

    // Enregistre toutes les commandes file.* sur le pont donné.
    // Retourne false si un nom de commande était déjà pris.
    bool registerOn(Bridge& bridge);

    // Handle de la fenêtre native (HWND sur Windows), pour rendre les
    // dialogues modaux. Renseigné par main_webview une fois la fenêtre
    // créée. Nul tant qu'il ne l'est pas (dialogues sans parent).
    void setOwnerWindow(void* owner) { m_ownerWindow = owner; }

    // --- Utilisés à la fermeture de la fenêtre (main_webview) ------------
    // L'UI signale l'état via la commande file.setState ; ces accesseurs
    // permettent au code natif de décider quoi faire avant de quitter.
    bool hasUnsavedChanges() const { return m_dirty; }
    const std::string& currentPath() const { return m_currentPath; }

    // Enregistre le document dans le fichier courant. Si aucun chemin n'est
    // connu, ouvre le dialogue natif « Enregistrer sous ». Retourne false
    // si l'utilisateur annule ou si l'écriture échoue (dans ce cas, on ne
    // doit PAS fermer l'application).
    //
    // Détection de modification externe : si le fichier a changé sur le
    // disque depuis qu'on l'a lu et que 'force' est faux, l'écriture est
    // refusée, *conflict (si non nul) passe à true, et on retourne false.
    // L'appelant peut alors demander confirmation puis rappeler avec force.
    bool saveCurrent(std::string* errorMessage = nullptr, bool force = false,
                     bool* conflict = nullptr);

private:
    core::Document& m_document;
    core::UndoManager& m_undo;
    void* m_ownerWindow = nullptr;
    bool m_dirty = false;
    std::string m_currentPath;
    // Dernière date connue de chaque fichier (chemin UTF-8 -> jeton), pour
    // détecter une modification survenue en dehors de l'éditeur.
    std::map<std::string, std::string> m_fileTokens;

    json save(const json& payload);
    json load(const json& payload);
    json serializeCmd(const json& payload);
    json getMetadata(const json& payload);
    json setMetadata(const json& payload);
    json chooseOpen(const json& payload);
    json chooseSave(const json& payload);
    json chooseFolder(const json& payload);
    json backup(const json& payload);
    json setState(const json& payload);
};

} // namespace bridge
