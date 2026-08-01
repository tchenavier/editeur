// ProjectApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "project.*" : l'identité du projet, son
// inventaire et son journal de modifications (Phase 5, jalon A).
//
// Commandes exposées :
//   project.state    -> { path }  identifiant du projet, nombre de
//                       fichiers inventoriés, et changements CONSTATÉS
//                       depuis le dernier instantané (sans rien écrire).
//   project.snapshot -> { path }  recalcule l'inventaire, journalise les
//                       différences, enregistre le nouvel inventaire.
//                       Appelé après un enregistrement réussi.
//   project.history  -> { path, limit? }  dernières entrées du journal,
//                       de la plus récente à la plus ancienne.
//
// CE QUE CE MODULE NE FAIT PAS, et c'est volontaire : rien de tout cela
// ne parle au réseau, ne demande de compte, ni ne suppose qu'il en
// existera un. L'inventaire et le journal sont utiles seuls (savoir ce
// qui a bougé dans son projet) ; ils serviront de fondation à la
// synchronisation quand elle viendra, sans que l'auteur ait jamais à
// créer de compte pour en profiter.
//
// L'identifiant de projet, lui, est attribué à la première sauvegarde
// par ManuscriptApi (voir ManuscriptApi.h) : c'est là que le fichier est écrit, donc
// là que l'identité doit être scellée.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "TextModel.h"

namespace bridge {

class ProjectApi {
public:
    // 'document' doit rester valide aussi longtemps que le Bridge.
    explicit ProjectApi(core::Document& document);

    bool registerOn(Bridge& bridge);

private:
    core::Document& m_document;

    json state(const json& payload);
    json snapshot(const json& payload);
    json history(const json& payload);
};

} // namespace bridge
