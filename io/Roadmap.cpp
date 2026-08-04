// Roadmap.cpp
// La feuille de route du roman. Voir Roadmap.h.

#include "Roadmap.h"

#include "PathUtf8.h"
#include "Yaml.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace io {

namespace {

constexpr const char* kSuffix = "_suivi.md";

// Bornes de bon sens. Elles ne protègent pas d'un attaquant — ce fichier
// est celui de l'auteur — mais d'un fichier abîmé qui ferait enfler la
// mémoire ou produirait un affichage inutilisable.
constexpr size_t kMaxParts = 5000;
constexpr size_t kMaxTitle = 500;
constexpr size_t kMaxSummary = 100000;

std::string trimmed(const std::string& text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && (text[start] == ' ' || text[start] == '\t' ||
                           text[start] == '\r' || text[start] == '\n')) ++start;
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
    return text.substr(start, end - start);
}

// Jours écoulés depuis le 1er janvier 1970 pour une date civile.
// Algorithme classique, valable sur toute la plage qui nous intéresse.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= (m <= 2) ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    // « m + 9 » puis modulo 12 plutôt que « m - 3 » : le calcul reste en
    // arithmétique non signée sans jamais passer par un nombre négatif.
    const unsigned mp = (m + 9u) % 12u;
    const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool isLeap(int64_t y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

unsigned daysInMonth(int64_t y, unsigned m) {
    static const unsigned longueurs[12] = {31, 28, 31, 30, 31, 30,
                                           31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && isLeap(y)) return 29;
    return longueurs[m - 1];
}

bool digits(const std::string& text, size_t from, size_t count, int64_t& out) {
    if (from + count > text.size()) return false;
    int64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        const char c = text[from + i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

// L'instant que désigne une échéance : le moment précis s'il est donné,
// sinon le DÉBUT du jour concerné.
bool deadlineInstant(const RoadmapPart& part, int64_t& seconds) {
    bool hasTime = false;
    return parseDeadline(part.deadline, seconds, hasTime);
}

std::vector<std::string> splitCommas(const std::string& text) {
    std::vector<std::string> valeurs;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(',', start);
        if (end == std::string::npos) end = text.size();
        const std::string item = trimmed(text.substr(start, end - start));
        if (!item.empty()) valeurs.push_back(item);
        if (end == text.size()) break;
        start = end + 1;
    }
    return valeurs;
}

} // namespace

const char* partStateName(PartState state) {
    switch (state) {
        case PartState::InProgress: return "en-cours";
        case PartState::Done:       return "termine";
        case PartState::Planned:    break;
    }
    return "prevu";
}

bool partStateFromName(const std::string& name, PartState& out) {
    if (name == "prevu")    { out = PartState::Planned;    return true; }
    if (name == "en-cours") { out = PartState::InProgress; return true; }
    if (name == "termine")  { out = PartState::Done;       return true; }
    return false;
}

const RoadmapPart* Roadmap::find(const std::string& id) const {
    for (const RoadmapPart& part : parts) {
        if (part.id == id) return &part;
    }
    return nullptr;
}

RoadmapPart* Roadmap::find(const std::string& id) {
    for (RoadmapPart& part : parts) {
        if (part.id == id) return &part;
    }
    return nullptr;
}

bool Roadmap::move(const std::string& id, int delta) {
    if (delta == 0) return false;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].id != id) continue;
        const int64_t cible = static_cast<int64_t>(i) + delta;
        // Aux extrémités, on ne fait rien : réorganiser ne doit jamais
        // faire disparaître une partie ni en dupliquer une.
        if (cible < 0 || cible >= static_cast<int64_t>(parts.size())) return false;
        std::swap(parts[i], parts[static_cast<size_t>(cible)]);
        return true;
    }
    return false;
}

fs::path roadmapPathFor(const fs::path& manuscriptPath) {
    if (manuscriptPath.empty()) return {};
    fs::path file = manuscriptPath.parent_path() / manuscriptPath.stem();
    file += kSuffix;
    return file;
}

// ---------------------------------------------------------------------
// Sérialisation
// ---------------------------------------------------------------------

std::string serializeRoadmap(const Roadmap& roadmap) {
    std::ostringstream out;
    out << "# Feuille de route de ce roman.\n";
    out << "# Fichier lisible et modifiable a la main : une partie par bloc.\n";

    for (const RoadmapPart& part : roadmap.parts) {
        if (part.id.empty()) continue; // sans identifiant, rien ne peut la désigner

        YamlBlock bloc;
        bloc.setScalar("id", part.id);
        bloc.setScalar("titre", part.title);
        bloc.setScalar("etat", partStateName(part.state));
        if (!part.deadline.empty()) bloc.setScalar("echeance", part.deadline);
        if (!part.chapter.empty()) bloc.setScalar("chapitre", part.chapter);
        if (!part.cards.empty()) bloc.setList("fiches", part.cards, true);
        if (part.extraLeadMinutes != 0) {
            bloc.setScalar("rappel", std::to_string(part.extraLeadMinutes));
        }
        if (part.notified) bloc.setScalar("prevenu", "oui");
        if (part.extraNotified) bloc.setScalar("prevenu-extra", "oui");
        // Le résumé en dernier : c'est le champ multi-ligne, et le mettre
        // à la fin garde le bloc lisible quand il est long.
        if (!part.summary.empty()) bloc.setScalar("resume", part.summary);

        out << "\n---\n" << bloc.toString();
    }
    return out.str();
}

Roadmap parseRoadmap(const std::string& text) {
    Roadmap roadmap;

    // Découpage en blocs sur les lignes « --- », puis lecture de chaque
    // bloc par l'analyseur YAML déjà éprouvé du projet.
    const std::vector<std::string> lignes = splitLines(text);
    std::vector<std::string> courant;
    bool dansUnBloc = false;

    auto terminerBloc = [&roadmap, &courant]() {
        if (courant.empty()) return;
        if (roadmap.parts.size() >= kMaxParts) { courant.clear(); return; }

        const YamlBlock bloc = YamlBlock::parse(courant);
        courant.clear();

        RoadmapPart part;
        part.id = trimmed(bloc.scalar("id"));
        if (part.id.empty() || part.id.size() > 128) return; // bloc abîmé : ignoré

        part.title = bloc.scalar("titre").substr(0, kMaxTitle);
        part.summary = bloc.scalar("resume").substr(0, kMaxSummary);
        if (!partStateFromName(bloc.scalar("etat"), part.state)) {
            part.state = PartState::Planned; // état inconnu : on n'invente pas
        }
        part.deadline = trimmed(bloc.scalar("echeance"));
        part.chapter = bloc.scalar("chapitre");

        if (const std::vector<std::string>* fiches = bloc.get("fiches")) {
            for (const std::string& fiche : *fiches) {
                // La forme « liste » est celle qu'on écrit, mais un
                // fichier retouché à la main peut porter « a, b » : on
                // accepte les deux plutôt que de perdre les liens.
                for (const std::string& item : splitCommas(fiche)) {
                    part.cards.push_back(item);
                }
            }
        }

        const std::string rappel = bloc.scalar("rappel");
        if (!rappel.empty() && rappel.size() <= 9) {
            uint32_t minutes = 0;
            bool bon = true;
            for (const char c : rappel) {
                if (c < '0' || c > '9') { bon = false; break; }
                minutes = minutes * 10 + static_cast<uint32_t>(c - '0');
            }
            if (bon) part.extraLeadMinutes = minutes;
        }

        part.notified = (bloc.scalar("prevenu") == "oui");
        part.extraNotified = (bloc.scalar("prevenu-extra") == "oui");

        roadmap.parts.push_back(std::move(part));
    };

    for (const std::string& ligne : lignes) {
        if (trimmed(ligne) == "---") {
            if (dansUnBloc) terminerBloc();
            dansUnBloc = true;
            continue;
        }
        if (dansUnBloc) courant.push_back(ligne);
    }
    if (dansUnBloc) terminerBloc();

    return roadmap;
}

Roadmap loadRoadmap(const fs::path& manuscriptPath) {
    const fs::path file = roadmapPathFor(manuscriptPath);
    if (file.empty()) return {};

    std::ifstream in(file, std::ios::binary);
    if (!in) return {}; // absent : feuille de route vide, ce n'est pas une erreur

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseRoadmap(buffer.str());
}

bool saveRoadmap(const Roadmap& roadmap, const fs::path& manuscriptPath,
                 std::string* errorMessage) {
    const fs::path file = roadmapPathFor(manuscriptPath);
    if (file.empty()) {
        if (errorMessage) {
            *errorMessage = "Enregistrez d'abord le manuscrit : la feuille de route "
                            "se range à côté du fichier.";
        }
        return false;
    }

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);

    // Écriture atomique, comme toute écriture du projet.
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) *errorMessage = "Impossible d'écrire : " + utils::pathToUtf8(tmp);
            return false;
        }
        const std::string texte = serializeRoadmap(roadmap);
        out.write(texte.data(), static_cast<std::streamsize>(texte.size()));
        out.flush();
        if (!out) {
            fs::remove(tmp, ec);
            if (errorMessage) *errorMessage = "Échec d'écriture : " + utils::pathToUtf8(tmp);
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (errorMessage) *errorMessage = "Impossible de renommer vers : " + utils::pathToUtf8(file);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// Échéances
// ---------------------------------------------------------------------

bool parseDeadline(const std::string& text, int64_t& seconds, bool& hasTime) {
    seconds = 0;
    hasTime = false;
    if (text.size() != 10 && text.size() != 16) return false;
    if (text[4] != '-' || text[7] != '-') return false;

    int64_t annee = 0, mois = 0, jour = 0;
    if (!digits(text, 0, 4, annee) || !digits(text, 5, 2, mois) ||
        !digits(text, 8, 2, jour)) {
        return false;
    }
    if (annee < 1900 || annee > 3000) return false;
    if (mois < 1 || mois > 12) return false;
    const unsigned maxJour = daysInMonth(annee, static_cast<unsigned>(mois));
    if (jour < 1 || jour > static_cast<int64_t>(maxJour)) return false;

    int64_t heures = 0, minutes = 0;
    if (text.size() == 16) {
        if (text[10] != 'T' || text[13] != ':') return false;
        if (!digits(text, 11, 2, heures) || !digits(text, 14, 2, minutes)) return false;
        if (heures > 23 || minutes > 59) return false;
        hasTime = true;
    }

    const int64_t jours = daysFromCivil(annee, static_cast<unsigned>(mois),
                                        static_cast<unsigned>(jour));
    seconds = jours * 86400 + heures * 3600 + minutes * 60;
    return true;
}

DueState dueStateOf(const RoadmapPart& part, int64_t nowLocal, uint32_t leadMinutes) {
    if (!part.hasDeadline()) return DueState::None;
    // Une partie terminée ne relance plus personne : le travail est fait,
    // la date n'a plus rien à réclamer.
    if (part.isDone()) return DueState::None;

    int64_t echeance = 0;
    bool avecHeure = false;
    if (!parseDeadline(part.deadline, echeance, avecHeure)) return DueState::None;

    if (avecHeure) {
        if (nowLocal >= echeance) return DueState::Late;
        const int64_t preavis = static_cast<int64_t>(leadMinutes) * 60;
        if (nowLocal >= echeance - preavis) return DueState::Soon;
        return DueState::Far;
    }

    // Sans heure, l'échéance est un JOUR. « Une heure avant le 15 mars »
    // ne voudrait rien dire : on prévient le jour même, et on signale le
    // retard ensuite.
    if (nowLocal >= echeance + 86400) return DueState::Late;
    if (nowLocal >= echeance) return DueState::Today;
    return DueState::Far;
}

bool extraReminderDue(const RoadmapPart& part, int64_t nowLocal) {
    if (part.extraLeadMinutes == 0 || !part.hasDeadline() || part.isDone()) return false;

    int64_t echeance = 0;
    if (!deadlineInstant(part, echeance)) return false;
    return nowLocal >= echeance - static_cast<int64_t>(part.extraLeadMinutes) * 60;
}

} // namespace io
