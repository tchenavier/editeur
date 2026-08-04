// RoadmapApi.cpp
// Les commandes roadmap.*. Voir RoadmapApi.h.

#include "RoadmapApi.h"

#include "PathUtf8.h"
#include "Roadmap.h"
#include "Utf8.h"
#include "Uuid.h"

#include <stdexcept>

namespace bridge {

namespace {

const char* dueStateName(io::DueState state) {
    switch (state) {
        case io::DueState::Far:   return "loin";
        case io::DueState::Soon:  return "bientot";
        case io::DueState::Today: return "aujourdhui";
        case io::DueState::Late:  return "retard";
        case io::DueState::None:  break;
    }
    return "aucune";
}

std::filesystem::path manuscriptOf(const json& payload) {
    const std::string chemin = requireString(payload, "path");
    if (chemin.empty()) {
        throw std::runtime_error(
            "Enregistrez d'abord le manuscrit : la feuille de route se range à côté "
            "du fichier, pour le suivre partout.");
    }
    return utils::pathFromUtf8(chemin);
}

// Nombre de mots d'un bloc, avec la MÊME règle que le compteur du
// document : deux comptages différents du même texte seraient pires que
// pas de comptage du tout.
size_t wordsIn(const core::TextBlock& block) {
    size_t count = 0;
    bool inWord = false;
    for (const char32_t ch : block.text()) {
        if (core::WordBoundary::isWordSeparator(ch)) inWord = false;
        else if (!inWord) { inWord = true; ++count; }
    }
    return count;
}

// L'avancement réel d'un chapitre : les mots écrits sous ce titre, du
// titre jusqu'au suivant de même niveau ou plus haut.
//
// On cherche par TITRE et non par indice : les chapitres se réorganisent
// par glisser-déposer, et un indice désignerait le mauvais dès le premier
// déplacement. Retourne -1 si le chapitre n'existe pas (ou plus) — le
// dire est plus utile que d'afficher zéro, qui ferait croire à un
// chapitre vide.
int64_t chapterWords(const core::Document& document, const std::string& title) {
    if (title.empty()) return -1;
    const std::u32string cible = utils::utf8ToU32(title);

    const std::vector<core::TextBlock>& blocs = document.blocks();
    for (size_t i = 0; i < blocs.size(); ++i) {
        if (blocs[i].type() == core::BlockType::Paragraph) continue;
        if (blocs[i].text() != cible) continue;

        const core::BlockType niveau = blocs[i].type();
        size_t total = 0;
        for (size_t j = i + 1; j < blocs.size(); ++j) {
            const core::BlockType type = blocs[j].type();
            // Un titre de niveau égal ou supérieur ferme le chapitre.
            if (type != core::BlockType::Paragraph && type <= niveau) break;
            total += wordsIn(blocs[j]);
        }
        return static_cast<int64_t>(total);
    }
    return -1;
}

json partJson(const core::Document& document, const io::RoadmapPart& part,
              int64_t now, uint32_t lead) {
    json fiches = json::array();
    for (const std::string& fiche : part.cards) fiches.push_back(fiche);

    const io::DueState due = io::dueStateOf(part, now, lead);
    return {{"id", part.id},
            {"title", part.title},
            {"summary", part.summary},
            {"state", io::partStateName(part.state)},
            {"deadline", part.deadline},
            {"chapter", part.chapter},
            {"cards", fiches},
            {"extraLead", part.extraLeadMinutes},
            {"notified", part.notified},
            {"extraNotified", part.extraNotified},
            {"due", dueStateName(due)},
            // Vrai quand il faut prévenir MAINTENANT et qu'on ne l'a pas
            // déjà fait : c'est l'interface qui décide de l'affichage,
            // mais c'est ici qu'on décide s'il y a lieu de le faire.
            {"remind", (due == io::DueState::Soon || due == io::DueState::Today ||
                        due == io::DueState::Late) && !part.notified},
            {"remindExtra", io::extraReminderDue(part, now) && !part.extraNotified},
            {"chapterWords", chapterWords(document, part.chapter)}};
}

io::RoadmapPart partFromJson(const json& entry) {
    io::RoadmapPart part;
    part.id = optString(entry, "id");
    part.title = optString(entry, "title");
    part.summary = optString(entry, "summary");
    if (!io::partStateFromName(optString(entry, "state"), part.state)) {
        part.state = io::PartState::Planned;
    }
    part.deadline = optString(entry, "deadline");
    part.chapter = optString(entry, "chapter");
    part.extraLeadMinutes = static_cast<uint32_t>(optIndex(entry, "extraLead", 0));
    part.notified = optBool(entry, "notified", false);
    part.extraNotified = optBool(entry, "extraNotified", false);

    const auto fiches = entry.find("cards");
    if (fiches != entry.end() && fiches->is_array()) {
        for (const auto& fiche : *fiches) {
            if (fiche.is_string()) part.cards.push_back(fiche.get<std::string>());
        }
    }
    return part;
}

void ecrire(const io::Roadmap& plan, const std::filesystem::path& manuscrit) {
    std::string erreur;
    if (!io::saveRoadmap(plan, manuscrit, &erreur)) throw std::runtime_error(erreur);
}

} // namespace

RoadmapApi::RoadmapApi(core::Document& document) : m_document(document) {}

bool RoadmapApi::registerOn(Bridge& bridge) {
    bool ok = bridge.registerCommand("roadmap.get", [this](const json& p) { return get(p); });
    ok &= bridge.registerCommand("roadmap.save", [this](const json& p) { return save(p); });
    ok &= bridge.registerCommand("roadmap.add", [this](const json& p) { return add(p); });
    ok &= bridge.registerCommand("roadmap.remove", [this](const json& p) { return remove(p); });
    ok &= bridge.registerCommand("roadmap.move", [this](const json& p) { return move(p); });
    ok &= bridge.registerCommand("roadmap.notified",
                                 [this](const json& p) { return notified(p); });
    return ok;
}

json RoadmapApi::get(const json& payload) {
    const std::filesystem::path manuscrit = manuscriptOf(payload);
    const io::Roadmap plan = io::loadRoadmap(manuscrit);

    // L'instant vient de l'interface : elle connaît le fuseau de l'auteur,
    // ce que le C++ n'a aucune raison de deviner. Zéro = « ne calcule
    // aucune échéance », ce qui sert à relire la feuille sans déclencher
    // de rappel.
    const int64_t now = static_cast<int64_t>(optIndex(payload, "now", 0));
    const uint32_t lead =
        static_cast<uint32_t>(optIndex(payload, "lead", kDefaultLeadMinutes));

    json parties = json::array();
    size_t aRappeler = 0;
    for (const io::RoadmapPart& part : plan.parts) {
        const json entree = partJson(m_document, part, now, lead);
        if (entree["remind"] == true || entree["remindExtra"] == true) ++aRappeler;
        parties.push_back(entree);
    }

    return {{"parts", parties},
            {"count", plan.parts.size()},
            {"toRemind", aRappeler},
            {"lead", lead},
            {"path", utils::pathToUtf8(io::roadmapPathFor(manuscrit))}};
}

json RoadmapApi::save(const json& payload) {
    const std::filesystem::path manuscrit = manuscriptOf(payload);

    const auto parties = payload.find("parts");
    if (parties == payload.end() || !parties->is_array()) {
        throw std::runtime_error("Feuille de route illisible : liste de parties attendue.");
    }

    io::Roadmap plan;
    for (const auto& entree : *parties) {
        if (!entree.is_object()) continue;
        io::RoadmapPart part = partFromJson(entree);
        // Une partie sans identifiant ne peut être désignée par rien :
        // on lui en donne un plutôt que de la perdre en silence.
        if (part.id.empty()) part.id = utils::newUuid();
        plan.parts.push_back(std::move(part));
    }

    ecrire(plan, manuscrit);
    return {{"count", plan.parts.size()}};
}

json RoadmapApi::add(const json& payload) {
    const std::filesystem::path manuscrit = manuscriptOf(payload);
    io::Roadmap plan = io::loadRoadmap(manuscrit);

    io::RoadmapPart part;
    part.id = utils::newUuid();
    part.title = optString(payload, "title", "Nouvelle partie");
    part.summary = optString(payload, "summary");
    part.deadline = optString(payload, "deadline");
    plan.parts.push_back(part);

    ecrire(plan, manuscrit);
    return {{"id", part.id}, {"count", plan.parts.size()}};
}

json RoadmapApi::remove(const json& payload) {
    const std::filesystem::path manuscrit = manuscriptOf(payload);
    const std::string id = requireString(payload, "id");

    io::Roadmap plan = io::loadRoadmap(manuscrit);
    const size_t avant = plan.parts.size();
    for (size_t i = 0; i < plan.parts.size(); ++i) {
        if (plan.parts[i].id == id) {
            plan.parts.erase(plan.parts.begin() + static_cast<long>(i));
            break;
        }
    }
    const bool retiree = plan.parts.size() != avant;
    if (retiree) ecrire(plan, manuscrit);
    return {{"removed", retiree}, {"count", plan.parts.size()}};
}

json RoadmapApi::move(const json& payload) {
    const std::filesystem::path manuscrit = manuscriptOf(payload);
    const std::string id = requireString(payload, "id");
    const int delta = (optString(payload, "direction") == "down") ? 1 : -1;

    io::Roadmap plan = io::loadRoadmap(manuscrit);
    const bool bouge = plan.move(id, delta);
    if (bouge) ecrire(plan, manuscrit);
    return {{"moved", bouge}};
}

json RoadmapApi::notified(const json& payload) {
    const std::filesystem::path manuscrit = manuscriptOf(payload);
    const std::string id = requireString(payload, "id");
    const bool extra = optBool(payload, "extra", false);

    io::Roadmap plan = io::loadRoadmap(manuscrit);
    io::RoadmapPart* part = plan.find(id);
    if (part == nullptr) return {{"noted", false}};

    if (extra) part->extraNotified = true;
    else part->notified = true;

    ecrire(plan, manuscrit);
    return {{"noted", true}};
}

} // namespace bridge
