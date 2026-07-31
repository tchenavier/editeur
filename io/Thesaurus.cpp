// Thesaurus.cpp
// Lecture du format MyThes. Voir Thesaurus.h.

#include "Thesaurus.h"

#include "PathUtf8.h"
#include "Speller.h" // toLowerFr : la casse se traite comme pour le correcteur
#include "Utf8.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace io {

namespace {

// Fin de la ligne commençant à 'start' (position du '\n', ou fin du texte).
size_t lineEnd(const std::string& text, size_t start) {
    const size_t position = text.find('\n', start);
    return (position == std::string::npos) ? text.size() : position;
}

std::string trimRight(const std::string& value) {
    size_t end = value.size();
    while (end > 0 && (value[end - 1] == '\r' || value[end - 1] == ' ')) --end;
    return value.substr(0, end);
}

// Découpe une ligne sur les barres verticales.
std::vector<std::string> splitBars(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= line.size()) {
        const size_t bar = line.find('|', start);
        const size_t end = (bar == std::string::npos) ? line.size() : bar;
        fields.push_back(trimRight(line.substr(start, end - start)));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return fields;
}

bool isNumber(const std::string& value) {
    if (value.empty() || value.size() > 6) return false;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

std::string lowered(const std::string& value) {
    return utils::u32ToUtf8(core::toLowerFr(utils::utf8ToU32(value)));
}

// Clé d'un couple exclu : les deux mots en minuscules, ORDONNÉS. Ranger
// « noir|gâteau » et « gâteau|noir » sous la même clé rend la symétrie
// automatique, sans avoir à interroger le jeu deux fois par synonyme.
std::pair<std::string, std::string> exclusionKey(const std::string& a, const std::string& b) {
    const std::string first = lowered(a);
    const std::string second = lowered(b);
    return (first <= second) ? std::make_pair(first, second)
                             : std::make_pair(second, first);
}

} // namespace

bool Thesaurus::load(std::string content) {
    m_content = std::move(content);
    m_index.clear();
    if (m_content.empty()) return false;

    size_t position = 0;
    // Première ligne : l'encodage. On n'accepte que l'UTF-8 — convertir
    // depuis une autre page de codes demanderait une table dont on n'a
    // pas besoin ici (le fichier français est en UTF-8).
    const size_t firstEnd = lineEnd(m_content, 0);
    const std::string encoding = trimRight(m_content.substr(0, firstEnd));
    if (encoding != "UTF-8" && encoding != "utf-8") return false;
    position = firstEnd + 1;

    // Une entrée commence par « mot|nombre_de_sens ». Les lignes de sens
    // commencent par « ( » : on distingue donc sans ambiguïté.
    while (position < m_content.size()) {
        const size_t end = lineEnd(m_content, position);
        const std::string line = trimRight(m_content.substr(position, end - position));
        if (!line.empty() && line[0] != '(') {
            const size_t bar = line.find('|');
            if (bar != std::string::npos && isNumber(line.substr(bar + 1))) {
                m_index.emplace_back(line.substr(0, bar), position);
            }
        }
        position = end + 1;
    }

    std::sort(m_index.begin(), m_index.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return !m_index.empty();
}

bool Thesaurus::loadFile(const std::filesystem::path& path, std::string* errorMessage) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorMessage) {
            *errorMessage = "Thésaurus introuvable : " + utils::pathToUtf8(path);
        }
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if (!load(std::move(content))) {
        if (errorMessage) {
            *errorMessage = "Fichier de thésaurus illisible : " + utils::pathToUtf8(path);
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// Corrections (voir Thesaurus.h)
// ---------------------------------------------------------------------

bool Thesaurus::isExcluded(const std::string& word, const std::string& synonym) const {
    if (m_exclusions.empty()) return false;
    return m_exclusions.find(exclusionKey(word, synonym)) != m_exclusions.end();
}

bool Thesaurus::addExclusion(const std::string& word, const std::string& synonym) {
    if (word.empty() || synonym.empty()) return false;
    return m_exclusions.insert(exclusionKey(word, synonym)).second;
}

void Thesaurus::setExclusions(const std::vector<std::pair<std::string, std::string>>& pairs) {
    m_exclusions.clear();
    for (const auto& pair : pairs) addExclusion(pair.first, pair.second);
}

bool Thesaurus::loadExclusions(const std::filesystem::path& path, std::string* errorMessage) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // Fichier absent : ce n'est PAS une erreur. Un projet neuf n'a
        // aucune correction, et le correcteur doit fonctionner sans.
        if (errorMessage) errorMessage->clear();
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    m_exclusions.clear();

    size_t start = 0;
    while (start <= content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();
        std::string line = trimRight(content.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == '#') continue;

        const size_t bar = line.find('|');
        if (bar == std::string::npos) continue; // ligne abîmée : ignorée
        const std::string word = trimRight(line.substr(0, bar));
        const std::string synonym = trimRight(line.substr(bar + 1));
        if (!word.empty() && !synonym.empty()) addExclusion(word, synonym);
    }
    return true;
}

bool Thesaurus::saveExclusions(const std::filesystem::path& path,
                               std::string* errorMessage) const {
    std::string text =
        "# Synonymes écartés du thésaurus, un couple par ligne.\n"
        "#\n"
        "# Ce fichier corrige les erreurs du dictionnaire de synonymes sans y\n"
        "# toucher : celui-ci est retéléchargeable, toute retouche y serait\n"
        "# perdue. Les exclusions valent dans LES DEUX SENS.\n"
        "#\n"
        "# Il se remplit depuis l'éditeur (clic droit sur un mot, puis la croix\n"
        "# à côté d'un synonyme), mais reste modifiable à la main.\n"
        "\n";
    for (const auto& pair : m_exclusions) {
        text += pair.first + "|" + pair.second + "\n";
    }

    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);

    // Écriture atomique, comme toute écriture du projet.
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) {
                *errorMessage = "Impossible d'écrire : " + utils::pathToUtf8(tmp);
            }
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            if (errorMessage) *errorMessage = "Échec d'écriture des corrections.";
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        if (errorMessage) *errorMessage = "Échec du remplacement des corrections.";
        return false;
    }
    return true;
}

std::vector<ThesaurusMeaning> Thesaurus::parseAt(size_t offset) const {
    std::vector<ThesaurusMeaning> meanings;
    if (offset >= m_content.size()) return meanings;

    // Ligne d'en-tête : « mot|nombre ».
    size_t position = offset;
    const size_t headerEnd = lineEnd(m_content, position);
    const std::string header = trimRight(m_content.substr(position, headerEnd - position));
    const size_t bar = header.find('|');
    if (bar == std::string::npos) return meanings;
    const int declared = std::atoi(header.c_str() + bar + 1);
    if (declared <= 0) return meanings;
    position = headerEnd + 1;

    // Puis autant de lignes de sens. On s'arrête aussi dès qu'une ligne
    // ne ressemble plus à un sens : un fichier abîmé ne doit pas nous
    // faire lire l'entrée suivante par-dessus celle-ci.
    for (int i = 0; i < declared && position < m_content.size(); ++i) {
        const size_t end = lineEnd(m_content, position);
        const std::string line = trimRight(m_content.substr(position, end - position));
        position = end + 1;
        if (line.empty() || line[0] != '(') break;

        const std::vector<std::string> fields = splitBars(line);
        if (fields.size() < 2) continue;
        ThesaurusMeaning meaning;
        meaning.label = fields[0];
        for (size_t k = 1; k < fields.size(); ++k) {
            if (!fields[k].empty()) meaning.synonyms.push_back(fields[k]);
        }
        if (!meaning.synonyms.empty()) meanings.push_back(std::move(meaning));
    }
    return meanings;
}

std::vector<ThesaurusMeaning> Thesaurus::lookup(const std::string& word) const {
    if (word.empty() || m_index.empty()) return {};

    auto find = [this](const std::string& key) -> size_t {
        const auto position = std::lower_bound(
            m_index.begin(), m_index.end(), key,
            [](const std::pair<std::string, size_t>& entry, const std::string& value) {
                return entry.first < value;
            });
        if (position == m_index.end() || position->first != key) return std::string::npos;
        return position->second;
    };

    size_t offset = find(word);
    if (offset == std::string::npos) {
        // Le thésaurus est en minuscules : « Roman » doit trouver « roman ».
        const std::string minuscule = lowered(word);
        if (minuscule != word) offset = find(minuscule);
    }
    if (offset == std::string::npos) return {};

    std::vector<ThesaurusMeaning> meanings = parseAt(offset);
    if (m_exclusions.empty()) return meanings;

    // Retrait des couples corrigés par l'auteur. Un sens vidé de tous ses
    // synonymes disparaît : afficher un intitulé sans rien dessous
    // laisserait croire à un défaut d'affichage.
    std::vector<ThesaurusMeaning> kept;
    for (ThesaurusMeaning& meaning : meanings) {
        std::vector<std::string> synonyms;
        for (const std::string& synonym : meaning.synonyms) {
            if (!isExcluded(word, synonym)) synonyms.push_back(synonym);
        }
        if (!synonyms.empty()) {
            meaning.synonyms = std::move(synonyms);
            kept.push_back(std::move(meaning));
        }
    }
    return kept;
}

} // namespace io
