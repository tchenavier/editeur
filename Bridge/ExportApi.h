// ExportApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "export.*" : la Phase 4 (export et
// présentation) vue depuis l'interface.
//
// Commandes exposées :
//   export.epub  -> { path, kind?, base?, titlePage?, toc? }
//                   Écrit un fichier .epub. 'kind' vaut "manuscript"
//                   (défaut) ou "bible" (les fiches seules, cf. cadrage
//                   « export de la bible narrative »). 'base' est le
//                   dossier qui sert à résoudre les chemins d'images
//                   RELATIFS (dossier du manuscrit, ou dossier des
//                   fiches) : l'interface le connaît, le C++ n'a donc
//                   pas à deviner. Retourne { path, bytes, chapters,
//                   images }.
//   export.image -> { path, base? }
//                   Lit une image et la renvoie en « data URI ».
//                   INDISPENSABLE : l'interface est chargée dans le
//                   webview par set_html, sans origine de fichier ; elle
//                   ne peut donc PAS afficher une image par un chemin
//                   file://. C'est ainsi que la couverture apparaît dans
//                   l'aperçu et sur la page de titre du PDF.
//
// L'export PDF n'est PAS ici : il utilise le moteur de rendu du webview
// (cf. cadrage) et vit donc dans main_webview.cpp, seule pièce qui
// connaît webview. Ce module reste testable sans interface.
//
// Les métadonnées (titre, auteur, couverture, résumé) ne sont pas
// dupliquées ici : ce sont celles du document, éditées par
// file.setMetadata et enregistrées dans l'en-tête YAML du manuscrit.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "CardModel.h"
#include "TextModel.h"

namespace bridge {

class ExportApi {
public:
    // Les trois références doivent rester valides aussi longtemps que le
    // Bridge (qui conserve des lambdas pointant dessus).
    ExportApi(core::Document& document,
              core::CardLibrary& library,
              core::SchemaRegistry& registry);

    bool registerOn(Bridge& bridge);

    // Taille maximale d'une image renvoyée en data URI. Plus basse que la
    // limite d'embarquement dans un livre (Book::kMaxImageSize) car la
    // chaîne traverse le pont JSON puis vit dans le DOM : une couverture
    // de plusieurs dizaines de Mio ferait ramer l'interface sans rien
    // apporter à l'affichage.
    static constexpr size_t kMaxDataUriSize = 8u * 1024u * 1024u; // 8 Mio

private:
    core::Document& m_document;
    core::CardLibrary& m_library;
    core::SchemaRegistry& m_registry;

    json epub(const json& payload);
    json image(const json& payload);
};

// Encode en base64 (exposé pour les tests).
std::string base64Encode(const std::string& data);

} // namespace bridge
