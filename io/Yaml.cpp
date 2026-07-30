// Yaml.cpp
// Implémentation du sous-ensemble YAML décrit dans Yaml.h.

#include "Yaml.h"

namespace io {

namespace {

std::string trimRight(const std::string& s) {
    const size_t end = s.find_last_not_of(" \t\r");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) return "";
    const size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

size_t indentOf(const std::string& s) {
    size_t n = 0;
    while (n < s.size() && s[n] == ' ') ++n;
    return n;
}

// Échappement d'une valeur entre guillemets doubles.
std::string quote(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\')      out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    out += '"';
    return out;
}

// Retire les guillemets et les échappements d'une valeur lue.
std::string unquote(const std::string& raw) {
    const std::string value = trim(raw);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return value; // valeur non quotée : acceptée telle quelle
    }
    const std::string inner = value.substr(1, value.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            const char next = inner[i + 1];
            if (next == 'n')  { out += '\n'; ++i; continue; }
            if (next == '"' || next == '\\') { out += next; ++i; continue; }
        }
        out += inner[i];
    }
    return out;
}

} // namespace

// =====================================================================
// Écriture
// =====================================================================

void YamlBlock::setScalar(const std::string& key, const std::string& value) {
    setList(key, std::vector<std::string>{value}, /*forceList=*/false);
}

void YamlBlock::setList(const std::string& key, const std::vector<std::string>& values,
                        bool forceList) {
    if (key.empty()) return;
    for (auto& entry : m_entries) {
        if (entry.key == key) { entry.values = values; entry.forceList = forceList; return; }
    }
    m_entries.push_back(Entry{key, values, forceList});
}

const std::vector<std::string>* YamlBlock::get(const std::string& key) const {
    for (const auto& entry : m_entries) {
        if (entry.key == key) return &entry.values;
    }
    return nullptr;
}

std::string YamlBlock::scalar(const std::string& key) const {
    const auto* values = get(key);
    return (values && !values->empty()) ? values->front() : std::string();
}

bool YamlBlock::has(const std::string& key) const {
    return get(key) != nullptr;
}

std::string YamlBlock::toString() const {
    std::string out;
    for (const auto& entry : m_entries) {
        const std::string& key = entry.key;
        const std::vector<std::string>& values = entry.values;
        if (values.empty()) continue; // une clé sans valeur n'apporte rien

        // Un seul élément contenant des sauts de ligne -> bloc « | »,
        // bien plus lisible qu'une longue ligne pleine de « \n ».
        if (values.size() == 1 && !entry.forceList &&
            values.front().find('\n') != std::string::npos) {
            out += key + ": |\n";
            std::string line;
            for (char c : values.front()) {
                if (c == '\n') { out += "  " + line + "\n"; line.clear(); }
                else if (c != '\r') line += c;
            }
            out += "  " + line + "\n"; // dernière ligne (même vide)
            continue;
        }

        if (values.size() == 1 && !entry.forceList) {
            out += key + ": " + quote(values.front()) + "\n";
            continue;
        }

        out += key + ":\n";
        for (const auto& value : values) {
            out += "  - " + quote(value) + "\n";
        }
    }
    return out;
}

// =====================================================================
// Lecture
// =====================================================================

YamlBlock YamlBlock::parse(const std::vector<std::string>& lines) {
    YamlBlock block;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& raw = lines[i];
        if (trim(raw).empty()) continue;
        if (indentOf(raw) > 0) continue;      // continuation déjà consommée
        if (trim(raw)[0] == '#') continue;    // commentaire

        const size_t colon = raw.find(':');
        if (colon == std::string::npos) continue; // ligne incomprise : ignorée

        const std::string key = trim(raw.substr(0, colon));
        const std::string rest = trim(raw.substr(colon + 1));
        if (key.empty()) continue;

        // --- Bloc multi-ligne : « clé: | » ---
        if (rest == "|" || rest == "|-") {
            std::vector<std::string> parts;
            size_t j = i + 1;
            for (; j < lines.size(); ++j) {
                if (trim(lines[j]).empty()) {
                    // Ligne vide : elle appartient au bloc seulement s'il
                    // continue après elle.
                    bool continues = false;
                    for (size_t k = j + 1; k < lines.size(); ++k) {
                        if (trim(lines[k]).empty()) continue;
                        continues = indentOf(lines[k]) >= 2;
                        break;
                    }
                    if (!continues) break;
                    parts.push_back("");
                    continue;
                }
                if (indentOf(lines[j]) < 2) break; // fin du bloc
                parts.push_back(trimRight(lines[j].substr(2)));
            }
            std::string text;
            for (size_t k = 0; k < parts.size(); ++k) {
                if (k) text += "\n";
                text += parts[k];
            }
            block.setScalar(key, text);
            i = j - 1;
            continue;
        }

        // --- Liste : « clé: » suivi de lignes « - valeur » ---
        if (rest.empty()) {
            std::vector<std::string> values;
            size_t j = i + 1;
            for (; j < lines.size(); ++j) {
                const std::string item = trim(lines[j]);
                if (item.empty()) continue;
                if (indentOf(lines[j]) < 1 || item[0] != '-') break;
                values.push_back(unquote(trim(item.substr(1))));
            }
            if (!values.empty()) {
                block.setList(key, values);
                i = j - 1;
            }
            continue;
        }

        // --- Scalaire ---
        block.setScalar(key, unquote(rest));
    }
    return block;
}

// =====================================================================
// Utilitaires de découpage
// =====================================================================

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') { lines.push_back(current); current.clear(); }
        else if (c != '\r') { current += c; }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

void splitFrontMatter(const std::string& text,
                      std::vector<std::string>& header,
                      std::string& body) {
    header.clear();
    body.clear();

    // BOM UTF-8 tolérée (éditeurs Windows).
    std::string content = text;
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }

    const std::vector<std::string> lines = splitLines(content);
    if (lines.empty() || trim(lines[0]) != "---") { body = content; return; }

    size_t closing = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (trim(lines[i]) == "---") { closing = i; break; }
    }
    // En-tête jamais fermé : tout est du corps (on ne devine pas).
    if (closing == 0) { body = content; return; }

    header.assign(lines.begin() + 1, lines.begin() + static_cast<std::ptrdiff_t>(closing));
    for (size_t i = closing + 1; i < lines.size(); ++i) {
        if (i > closing + 1) body += "\n";
        body += lines[i];
    }
}

} // namespace io
