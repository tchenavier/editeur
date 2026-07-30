// CardApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "cards.*" : les fiches de worldbuilding
// (Phase 2) vues depuis l'interface.
//
// Commandes exposées :
//   cards.schemas -> { schemas: [...] }  description des types de fiches
//                    et de leurs champs. C'EST LA COMMANDE CLÉ : l'UI
//                    construit ses formulaires à partir de ce descriptif,
//                    donc ajouter un type de fiche ne demande AUCUNE
//                    modification de l'interface (exigence du cadrage).
//   cards.open    -> { path }   fixe le dossier et CHARGE les fiches qui
//                    s'y trouvent (remplace celles en mémoire). À l'ouverture
//                    d'un manuscrit : on lit la bible qui l'accompagne.
//   cards.setRoot -> { path }   fixe le dossier et y ÉCRIT les fiches déjà
//                    en mémoire (sans rien charger). À l'enregistrement du
//                    manuscrit : les fiches sont rangées à côté du texte.
//   cards.state   -> {}         { root, count }
//   cards.list    -> { type?, tag?, query? }  résumés triés par titre
//   cards.get     -> { id }     une fiche complète
//   cards.save    -> { id?, type, title?, tags?, fields? }
//                    crée ou met à jour ; l'identifiant est déduit du
//                    titre s'il est absent. Écrit le fichier si un
//                    dossier a été fixé.
//   cards.remove  -> { id }     supprime la fiche, son fichier, et les
//                    références vers elle dans les autres fiches
//   cards.tags    -> {}         tous les tags utilisés
//   cards.saveAll -> {}         réécrit toutes les fiches sur disque
//   cards.search  -> { query, caseSensitive?, wholeWord? }  occurrences de
//                    'query' dans les titres et les champs des fiches
//                    (recherche globale de la Phase 3)
//   cards.replaceAll -> { query, replacement, ... }  remplace dans toutes
//                    les fiches (titre + champs de texte), retourne { count }
//   cards.checkRefs -> {}  références de fiches cassées (une relation qui
//                    pointe vers une fiche inexistante) : { broken: [...] }
//
// LIMITE ASSUMÉE : l'annulation (Ctrl+Z) ne couvre pas les fiches.
// L'historique du projet est bâti sur des instantanés de blocs de texte
// (core::UndoManager) et ne s'applique pas à ce modèle. L'édition d'une
// fiche se fait par formulaire avec enregistrement explicite, ce qui
// limite le risque ; un historique dédié pourra être ajouté plus tard.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "CardModel.h"

#include <filesystem>
#include <map>
#include <string>

namespace bridge {

class CardApi {
public:
    // 'library' et 'registry' doivent rester valides aussi longtemps que
    // le Bridge (celui-ci conserve des lambdas qui pointent dessus).
    CardApi(core::CardLibrary& library, core::SchemaRegistry& registry);

    bool registerOn(Bridge& bridge);

    // Dossier des fiches, s'il a été fixé (vide sinon).
    const std::filesystem::path& root() const { return m_root; }

private:
    core::CardLibrary& m_library;
    core::SchemaRegistry& m_registry;
    std::filesystem::path m_root; // vide tant que cards.open n'a pas été appelé
    // Date connue du fichier de chaque fiche (id -> jeton), pour détecter
    // une modification externe avant de l'écraser.
    std::map<std::string, std::string> m_cardTokens;

    json schemas(const json& payload);
    json open(const json& payload);
    json setRoot(const json& payload);
    json state(const json& payload);
    json list(const json& payload);
    json get(const json& payload);
    json save(const json& payload);
    json remove(const json& payload);
    json tags(const json& payload);
    json saveAll(const json& payload);
    json search(const json& payload);
    json replaceAll(const json& payload);
    json checkRefs(const json& payload);

    // Écrit la fiche sur disque si un dossier a été fixé. Sans dossier,
    // la modification reste en mémoire (l'UI peut appeler cards.saveAll
    // plus tard) : on ne considère pas cela comme une erreur.
    void persist(const core::Card& card);

    // (Re)note la date de chaque fiche présente sur disque (après un
    // chargement ou une écriture globale). Sans dossier : table vidée.
    void recordAllTokens();

    // Fabrique un identifiant unique à partir d'un titre.
    std::string makeUniqueId(const std::string& title) const;
};

// Sérialisations partagées (également utiles aux tests).
json cardToJson(const core::Card& card);
json cardSummaryToJson(const core::Card& card);
json schemaToJson(const core::CardSchema& schema);

} // namespace bridge
