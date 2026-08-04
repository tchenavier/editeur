// RoadmapApi.h
// -----------------------------------------------------------------------
// Enregistre les commandes "roadmap.*" : la feuille de route du roman —
// ce qui est prévu, avec quels personnages, pour quand.
//
// Commandes exposées :
//   roadmap.get     -> { path, now?, lead? }
//       La feuille de route, chaque partie accompagnée de l'état de son
//       échéance (loin, bientôt, aujourd'hui, en retard) et, si elle est
//       rattachée à un chapitre, de l'avancement réel de ce chapitre.
//   roadmap.save    -> { path, parts }
//       Enregistre la feuille entière. L'interface envoie ce qu'elle
//       affiche : pas de modification partielle, donc pas d'état
//       intermédiaire à réconcilier.
//   roadmap.add     -> { path, title? } ajoute une partie et rend son identifiant
//   roadmap.remove  -> { path, id }
//   roadmap.move    -> { path, id, delta }
//   roadmap.notified-> { path, id, extra? } note qu'un rappel a été donné
//
// L'AVANCEMENT VIENT DU MANUSCRIT, pas d'un pourcentage saisi à la main.
// Une partie rattachée à un chapitre affiche le nombre de mots réellement
// écrits dans ce chapitre. Un chiffre qu'on saisit soi-même est faux le
// lendemain ; celui-là ne peut pas mentir.
//
// LE TEMPS EST UN ARGUMENT, jamais lu ici. L'interface envoie l'instant
// courant (secondes depuis l'époque, en heure LOCALE), ce qui évite
// d'avoir à décider d'un fuseau côté C++ — le navigateur connaît déjà
// celui de l'auteur — et rend tout le comportement d'échéance testable
// sans attendre.
// -----------------------------------------------------------------------

#pragma once

#include "Bridge.h"
#include "TextModel.h"

namespace bridge {

class RoadmapApi {
public:
    // 'document' doit rester valide aussi longtemps que le Bridge : il
    // sert à calculer l'avancement des chapitres rattachés.
    explicit RoadmapApi(core::Document& document);

    bool registerOn(Bridge& bridge);

    // Préavis par défaut, en minutes, quand l'interface n'en donne pas.
    // Une heure : c'est le réglage demandé, et il est paramétrable.
    static constexpr uint32_t kDefaultLeadMinutes = 60;

private:
    core::Document& m_document;

    json get(const json& payload);
    json save(const json& payload);
    json add(const json& payload);
    json remove(const json& payload);
    json move(const json& payload);
    json notified(const json& payload);
};

} // namespace bridge
