// Phonetique.cpp
// La clé sonore d'un mot français. Voir Phonetique.h.
//
// Le mot est d'abord réduit à des lettres nues, puis parcouru de gauche à
// droite en cherchant à chaque position le plus long groupe connu.
// L'ORDRE DU TABLEAU COMPTE : « eau » doit être essayé avant « au », et
// « ain » avant « ai ».

#include "Phonetique.h"

#include "Utf8.h"

#include <cstring>

namespace core {

namespace {

// Lettres nues : minuscules, sans accent, ajoutées à la suite. On AJOUTE
// plutôt que de retourner, parce que certains signes se déplient en DEUX
// lettres — « œuvre » commence par « oe » — et qu'un tampon statique
// partagé pour rendre un pointeur serait un piège pour rien.
void appendBare(std::string& out, char32_t c) {
    if (c >= U'A' && c <= U'Z') c = static_cast<char32_t>(c + 32);
    if (c >= U'a' && c <= U'z') {
        out += static_cast<char>(c);
        return;
    }
    out += [c]() -> const char* {
    switch (c) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
            return "a";
        case 0x00C7: case 0x00E7: return "c";
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
            return "e";
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
            return "i";
        case 0x00D1: case 0x00F1: return "n";
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
            return "o";
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
            return "u";
        case 0x00DD: case 0x00FD: case 0x0178: case 0x00FF: return "y";
        case 0x00C6: case 0x00E6: return "ae"; // æ
        case 0x0152: case 0x0153: return "oe"; // œ
        default: return "";
    }
    }();
}

bool isVowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

struct Rule {
    const char* group;
    size_t length; // écrite d'avance : voir la note ci-dessous
    const char* sound;
};

// La LONGUEUR EST STOCKÉE, et ce n'est pas un détail. La version d'origine
// appelait strlen sur chaque règle à chaque position de chaque mot : sur
// les 454 000 formes du dictionnaire, cela représentait plus d'une seconde
// à elle seule — davantage que le développement du dictionnaire entier.
// La calculer une fois à la compilation ramène ce coût à rien.
constexpr size_t len(const char* s) {
    return std::char_traits<char>::length(s);
}

#define REGLE(groupe, son) { groupe, len(groupe), son }

// Du plus long au plus court : la recherche s'arrête au premier trouvé.
const Rule kRules[] = {
    REGLE("eaux", "o"), REGLE("eau", "o"),
    REGLE("aient", "e"),                             // terminaison verbale muette
    REGLE("ain", "1"), REGLE("aim", "1"), REGLE("ein", "1"), REGLE("eim", "1"),
    REGLE("oin", "w1"),
    REGLE("ien", "i1"),
    REGLE("an", "3"), REGLE("am", "3"), REGLE("en", "3"), REGLE("em", "3"),
    REGLE("on", "2"), REGLE("om", "2"),
    REGLE("in", "1"), REGLE("im", "1"), REGLE("yn", "1"), REGLE("ym", "1"),
    REGLE("un", "4"), REGLE("um", "4"),
    REGLE("au", "o"), REGLE("ou", "u"), REGLE("oe", "e"), REGLE("eu", "e"),
    REGLE("ai", "e"), REGLE("ei", "e"), REGLE("ay", "ey"),
    REGLE("oi", "wa"), REGLE("oy", "way"),
    REGLE("sch", "C"), REGLE("ph", "f"), REGLE("ch", "C"), REGLE("sh", "C"),
    REGLE("gn", "N"),
    REGLE("th", "t"), REGLE("cqu", "k"), REGLE("qu", "k"),
    REGLE("cc", "k"), REGLE("ck", "k"),
    REGLE("ss", "s"), REGLE("sc", "s"),
    REGLE("ti", "ti"),                               // garde « ti » avant la règle du c
};

#undef REGLE

} // namespace

std::string phoneticKey(const std::u32string& word) {
    // 1. Lettres nues.
    std::string plain;
    plain.reserve(word.size() + 2);
    for (const char32_t c : word) appendBare(plain, c);
    if (plain.empty()) return {};

    // 2. Groupes de lettres -> sons.
    std::string key;
    key.reserve(plain.size());
    size_t i = 0;
    while (i < plain.size()) {
        bool matched = false;
        for (const Rule& rule : kRules) {
            // Écarter d'abord sur la PREMIÈRE lettre : la plupart des
            // positions ne commencent aucun groupe, et cette comparaison
            // évite une comparaison de chaîne quarante fois par caractère.
            if (rule.group[0] != plain[i]) continue;
            if (i + rule.length > plain.size()) continue;
            if (std::memcmp(plain.data() + i, rule.group, rule.length) != 0) continue;

            // « an » dans « année » se prononce « a » puis « n » : une
            // nasale a besoin d'une consonne ou d'une fin après elle.
            if (rule.sound[0] >= '1' && rule.sound[0] <= '4') {
                const char next = (i + rule.length < plain.size())
                                      ? plain[i + rule.length] : '\0';
                if (isVowel(next) || next == plain[i + rule.length - 1]) continue;
            }
            key += rule.sound;
            i += rule.length;
            matched = true;
            break;
        }
        if (matched) continue;

        const char c = plain[i];
        const char next = (i + 1 < plain.size()) ? plain[i + 1] : '\0';
        const char previous = (i > 0) ? plain[i - 1] : '\0';

        switch (c) {
            case 'c':
                key += (next == 'e' || next == 'i' || next == 'y') ? 's' : 'k';
                break;
            case 'g':
                key += (next == 'e' || next == 'i' || next == 'y') ? 'j' : 'g';
                break;
            case 's':
                // Entre deux voyelles, « s » sonne « z » : rose, base.
                key += (isVowel(previous) && isVowel(next)) ? 'z' : 's';
                break;
            case 'x':
                key += "ks";
                break;
            case 'y':
                key += 'i';
                break;
            case 'h':
                break; // toujours muet
            case 'w':
                key += 'v';
                break;
            default:
                // Une consonne double ne s'entend pas deux fois.
                if (key.empty() || key.back() != c) key += c;
                break;
        }
        ++i;
    }

    // 3. Le « r » d'un infinitif ne s'entend pas : « parler » sonne comme
    //    « parlé ». C'est un AJOUT au portage, et le plus rentable de
    //    tous — la confusion « -er / -é / -ait / -aient » est la faute
    //    française la plus répandue, et sans cette ligne la famille
    //    resterait coupée en deux (« parlé », « parlait » et « parlaient »
    //    se rejoignaient déjà, mais pas l'infinitif).
    //
    //    Contrepartie assumée : « mer » et « hiver », où le « r » se
    //    prononce, rejoignent « mé » et « hivé ». Cela n'a de conséquence
    //    que sur des SUGGESTIONS, offertes pour un mot déjà reconnu
    //    inconnu — au pire une proposition de plus, jamais une faute
    //    signalée à tort.
    if (key.size() > 2 && key.back() == 'r' && key[key.size() - 2] == 'e') {
        key.pop_back();
    }

    // 4. Lettres finales muettes. On ne rogne jamais une clé d'une seule
    //    lettre : « a » et « e » doivent rester distincts de rien.
    while (key.size() > 1 && std::strchr("edpstxz", key.back()) != nullptr) {
        key.pop_back();
        // « ées » -> « e » : on continue tant qu'un « e » muet apparaît.
        if (!key.empty() && key.back() == 'e') continue;
        break;
    }
    while (key.size() > 1 && key.back() == 'e') key.pop_back();

    return key;
}

std::string phoneticKey(const std::string& word) {
    return phoneticKey(utils::utf8ToU32(word));
}

uint64_t phoneticHash(const std::string& key) {
    uint64_t h = 14695981039346656037ull; // FNV-1a, comme ailleurs dans le projet
    for (const unsigned char c : key) {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace core
