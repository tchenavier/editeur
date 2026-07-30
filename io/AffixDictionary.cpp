// AffixDictionary.cpp
// Lecture et développement d'un dictionnaire Hunspell. Voir le .h pour ce
// qui est géré et ce qui ne l'est pas.

#include "AffixDictionary.h"

#include "Utf8.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace io {

namespace {

// --- Petit moteur de conditions --------------------------------------
// Les conditions Hunspell ne sont PAS des expressions régulières : ce
// sont des suites d'« atomes », chacun consommant exactement UN
// caractère. D'où ce moteur de vingt lignes plutôt qu'une dépendance.
struct Atom {
    char32_t literal = 0;
    bool any = false;        // « . »
    bool isClass = false;    // « [...] »
    bool negate = false;     // « [^...] »
    std::u32string set;
};

using Condition = std::vector<Atom>;

Condition compileCondition(const std::u32string& text) {
    Condition atoms;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == U'[') {
            Atom atom;
            atom.isClass = true;
            ++i;
            if (i < text.size() && text[i] == U'^') { atom.negate = true; ++i; }
            while (i < text.size() && text[i] != U']') { atom.set += text[i]; ++i; }
            if (i < text.size()) ++i; // ']'
            atoms.push_back(std::move(atom));
        } else if (text[i] == U'.') {
            Atom atom;
            atom.any = true;
            atoms.push_back(atom);
            ++i;
        } else {
            Atom atom;
            atom.literal = text[i];
            atoms.push_back(atom);
            ++i;
        }
    }
    return atoms;
}

bool atomMatches(const Atom& atom, char32_t c) {
    if (atom.any) return true;
    if (atom.isClass) {
        const bool inSet = atom.set.find(c) != std::u32string::npos;
        return atom.negate ? !inSet : inSet;
    }
    return atom.literal == c;
}

// Pour un suffixe la condition porte sur la FIN du mot, pour un préfixe
// sur son DÉBUT. Un mot plus court que la condition ne correspond pas.
bool matchesCompiled(const std::u32string& word, const Condition& condition, bool suffix) {
    if (condition.empty()) return true;
    if (word.size() < condition.size()) return false;
    const size_t offset = suffix ? word.size() - condition.size() : 0;
    for (size_t i = 0; i < condition.size(); ++i) {
        if (!atomMatches(condition[i], word[offset + i])) return false;
    }
    return true;
}

// --- Découpage de lignes ---------------------------------------------
std::vector<std::string> splitLinesLocal(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

std::vector<std::string> splitSpaces(const std::string& line) {
    std::vector<std::string> fields;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        const size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        fields.push_back(line.substr(start, i - start));
    }
    return fields;
}

// Un « 0 » seul signifie « rien » dans le format.
std::string zeroToEmpty(const std::string& value) {
    return (value == "0") ? std::string() : value;
}

// Version compilée d'une classe, préparée une fois avant l'expansion :
// convertir 5 000 conditions à chaque mot du dictionnaire coûterait cent
// fois plus cher que de les convertir une bonne fois.
struct CompiledRule {
    std::u32string strip;
    std::u32string append;
    Condition condition;
    std::vector<std::string> continuation;
};
struct CompiledClass {
    bool suffix = true;
    bool crossProduct = false;
    std::vector<CompiledRule> rules;
};

// Applique une règle. Retourne false si elle ne s'applique pas.
bool applyRule(const std::u32string& word, const CompiledRule& rule, bool suffix,
               bool fullStrip, std::u32string& out) {
    if (!matchesCompiled(word, rule.condition, suffix)) return false;
    if (rule.strip.size() > word.size()) return false;
    if (!fullStrip && rule.strip.size() == word.size()) return false;

    if (suffix) {
        if (!rule.strip.empty() &&
            word.compare(word.size() - rule.strip.size(), rule.strip.size(), rule.strip) != 0) {
            return false;
        }
        out = word.substr(0, word.size() - rule.strip.size()) + rule.append;
    } else {
        if (!rule.strip.empty() && word.compare(0, rule.strip.size(), rule.strip) != 0) {
            return false;
        }
        out = rule.append + word.substr(rule.strip.size());
    }
    return !out.empty();
}

bool hasApostrophe(const std::u32string& word) {
    return word.find(U'\'') != std::u32string::npos ||
           word.find(U'’') != std::u32string::npos;
}

} // namespace

std::vector<std::string> splitFlags(const std::string& flags, int flagLength) {
    std::vector<std::string> out;
    if (flagLength < 1) flagLength = 1;
    const size_t step = static_cast<size_t>(flagLength);
    for (size_t i = 0; i + step <= flags.size(); i += step) {
        out.push_back(flags.substr(i, step));
    }
    return out;
}

bool conditionMatches(const std::string& word, const std::string& condition, bool suffix) {
    return matchesCompiled(utils::utf8ToU32(word),
                           compileCondition(utils::utf8ToU32(condition)), suffix);
}

AffixData parseAff(const std::string& text) {
    AffixData data;
    const std::vector<std::string> lines = splitLinesLocal(text);

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> fields = splitSpaces(line);
        if (fields.empty()) continue;
        const std::string& key = fields[0];

        if (key == "FLAG" && fields.size() >= 2) {
            // Seul « long » (deux caractères) est utilisé par le
            // dictionnaire français ; tout autre mode reste à un
            // caractère, ce qui est le défaut du format.
            if (fields[1] == "long") data.flagLength = 2;
        } else if (key == "FULLSTRIP") {
            data.fullStrip = true;
        } else if (key == "NEEDAFFIX" && fields.size() >= 2) {
            data.needAffix = fields[1];
        } else if (key == "FORBIDDENWORD" && fields.size() >= 2) {
            data.forbiddenWord = fields[1];
        } else if (key == "CIRCUMFIX" && fields.size() >= 2) {
            data.circumfix = fields[1];
        } else if (key == "KEEPCASE" && fields.size() >= 2) {
            data.keepCase = fields[1];
        } else if (key == "NOSUGGEST" && fields.size() >= 2) {
            data.noSuggest = fields[1];
        } else if (key == "REP" && fields.size() >= 3) {
            // La première ligne REP annonce un nombre : on la reconnaît à
            // ce que son deuxième champ est entièrement numérique.
            bool isCount = !fields[1].empty();
            for (char c : fields[1]) {
                if (c < '0' || c > '9') { isCount = false; break; }
            }
            if (!isCount) data.replacements.emplace_back(fields[1], fields[2]);
        } else if ((key == "SFX" || key == "PFX") && fields.size() >= 4) {
            const bool suffix = (key == "SFX");
            const std::string& flag = fields[1];
            // En-tête de classe : « SFX <drapeau> <Y|N> <nombre> ».
            if (fields[2] == "Y" || fields[2] == "N") {
                AffixClass& cls = data.classes[flag];
                cls.flag = flag;
                cls.suffix = suffix;
                cls.crossProduct = (fields[2] == "Y");
                continue;
            }
            // Ligne de règle : « SFX <drapeau> <retrait> <ajout>[/<suite>] [<condition>] ».
            AffixClass& cls = data.classes[flag];
            if (cls.flag.empty()) { cls.flag = flag; cls.suffix = suffix; }

            AffixRule rule;
            rule.strip = zeroToEmpty(fields[2]);
            std::string append = fields[3];
            const size_t slash = append.find('/');
            if (slash != std::string::npos) {
                rule.continuation = splitFlags(append.substr(slash + 1), data.flagLength);
                append = append.substr(0, slash);
            }
            rule.append = zeroToEmpty(append);
            // Le champ suivant est la condition ; ce qui suit encore est
            // de l'analyse morphologique, dont la vérification
            // orthographique n'a que faire.
            rule.condition = (fields.size() >= 5 && fields[4] != ".") ? fields[4] : std::string();
            cls.rules.push_back(std::move(rule));
        }
    }
    return data;
}

std::vector<std::string> expandDictionary(const std::string& dicText,
                                          const AffixData& affix,
                                          const ExpandOptions& options) {
    // 1. Compilation des classes (conditions converties une seule fois).
    std::map<std::string, CompiledClass> classes;
    for (const auto& entry : affix.classes) {
        CompiledClass compiled;
        compiled.suffix = entry.second.suffix;
        compiled.crossProduct = entry.second.crossProduct;
        for (const AffixRule& rule : entry.second.rules) {
            CompiledRule compiledRule;
            compiledRule.strip = utils::utf8ToU32(rule.strip);
            compiledRule.append = utils::utf8ToU32(rule.append);
            compiledRule.condition = compileCondition(utils::utf8ToU32(rule.condition));
            compiledRule.continuation = rule.continuation;
            compiled.rules.push_back(std::move(compiledRule));
        }
        classes.emplace(entry.first, std::move(compiled));
    }

    std::vector<std::string> forms;
    std::vector<std::string> forbidden;
    bool full = false;

    auto emit = [&](const std::u32string& form) {
        if (full) return;
        if (options.dropApostrophes && hasApostrophe(form)) return;
        if (forms.size() >= options.maxForms) { full = true; return; }
        forms.push_back(utils::u32ToUtf8(form));
    };

    // 2. Parcours du dictionnaire. La première ligne est un simple
    //    décompte approximatif : elle ne contient pas de mot valide, mais
    //    la traiter comme tel serait sans conséquence (elle serait juste
    //    ajoutée), donc on ne s'embarrasse pas d'un cas particulier.
    const std::vector<std::string> lines = splitLinesLocal(dicText);
    for (const std::string& line : lines) {
        if (line.empty() || line[0] == '#' || line[0] == '\t') continue;

        // « mot/DRAPEAUX <champs morphologiques> » — la barre oblique
        // peut être échappée dans le mot lui-même.
        std::string word;
        std::string flagText;
        bool inFlags = false;
        for (size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (!inFlags && c == '\\' && i + 1 < line.size()) {
                word += line[i + 1];
                ++i;
                continue;
            }
            if (!inFlags && c == '/') { inFlags = true; continue; }
            if (c == ' ' || c == '\t') break; // champs morphologiques : ignorés
            if (inFlags) flagText += c;
            else word += c;
        }
        if (word.empty()) continue;

        const std::vector<std::string> flags = splitFlags(flagText, affix.flagLength);
        auto hasFlag = [&flags](const std::string& flag) {
            return !flag.empty() &&
                   std::find(flags.begin(), flags.end(), flag) != flags.end();
        };

        const std::u32string base = utils::utf8ToU32(word);
        if (base.empty()) continue;

        // Forme explicitement fautive : on la retiendra pour l'exclure,
        // même si une autre entrée la produit par ses règles.
        if (hasFlag(affix.forbiddenWord)) {
            forbidden.push_back(word);
            continue;
        }
        // NEEDAFFIX : le lemme seul n'est pas un mot (« -aine » par ex.).
        if (!hasFlag(affix.needAffix)) emit(base);

        // 3. Application des affixes portés par ce mot.
        std::vector<std::u32string> suffixed; // pour le produit croisé
        for (const std::string& flag : flags) {
            const auto it = classes.find(flag);
            if (it == classes.end()) continue;
            const CompiledClass& cls = it->second;

            for (const CompiledRule& rule : cls.rules) {
                std::u32string form;
                if (!applyRule(base, rule, cls.suffix, affix.fullStrip, form)) continue;
                emit(form);
                if (cls.suffix) suffixed.push_back(form);

                // Drapeaux de continuation : la forme produite peut à son
                // tour porter un affixe (un seul niveau supplémentaire —
                // c'est aussi la limite de Hunspell).
                for (const std::string& next : rule.continuation) {
                    const auto nextIt = classes.find(next);
                    if (nextIt == classes.end()) continue;
                    for (const CompiledRule& nextRule : nextIt->second.rules) {
                        std::u32string second;
                        if (applyRule(form, nextRule, nextIt->second.suffix,
                                      affix.fullStrip, second)) {
                            emit(second);
                        }
                    }
                }
            }
        }

        // 4. Produit croisé préfixe × suffixe, quand les deux classes
        //    l'autorisent (« Y » dans leur en-tête).
        for (const std::string& flag : flags) {
            const auto it = classes.find(flag);
            if (it == classes.end() || it->second.suffix || !it->second.crossProduct) continue;
            for (const CompiledRule& rule : it->second.rules) {
                for (const std::u32string& stem : suffixed) {
                    std::u32string form;
                    if (applyRule(stem, rule, /*suffix=*/false, affix.fullStrip, form)) {
                        emit(form);
                    }
                }
            }
        }
    }

    // 5. Tri, dédoublonnage, puis retrait des formes interdites.
    std::sort(forms.begin(), forms.end());
    forms.erase(std::unique(forms.begin(), forms.end()), forms.end());

    if (!forbidden.empty()) {
        std::sort(forbidden.begin(), forbidden.end());
        forbidden.erase(std::unique(forbidden.begin(), forbidden.end()), forbidden.end());
        std::vector<std::string> kept;
        kept.reserve(forms.size());
        std::set_difference(forms.begin(), forms.end(),
                            forbidden.begin(), forbidden.end(),
                            std::back_inserter(kept));
        forms.swap(kept);
    }
    return forms;
}

} // namespace io
