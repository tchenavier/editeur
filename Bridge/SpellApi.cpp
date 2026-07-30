// SpellApi.cpp
// Implémentation des commandes spell.*. Voir SpellApi.h.

#include "SpellApi.h"

#include "AffixDictionary.h"
#include "PathUtf8.h"
#include "Utf8.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace bridge {

namespace {

namespace fs = std::filesystem;

std::string readWholeFile(const fs::path& path, bool* ok) {
    if (ok) *ok = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if (ok) *ok = true;
    return content;
}

} // namespace

SpellApi::SpellApi() = default;

fs::path SpellApi::findDictionaryFolder() {
    // Mêmes chemins relatifs que pour l'interface : l'exécutable peut
    // être lancé depuis divers dossiers pendant le développement.
    const char* const candidates[] = {
        "third_party/dictionnaires",
        "../third_party/dictionnaires",
        "../../third_party/dictionnaires",
        "../../../third_party/dictionnaires",
    };
    for (const char* candidate : candidates) {
        const fs::path folder = utils::pathFromUtf8(candidate);
        std::error_code ec;
        if (fs::exists(folder / "fr.dic", ec)) return folder;
    }
    return fs::path();
}

fs::path SpellApi::personalPathFor(const fs::path& manuscript) {
    if (manuscript.empty()) return fs::path();
    fs::path path = manuscript.parent_path() / manuscript.stem();
    path += "_dictionnaire.txt";
    return path;
}

bool SpellApi::registerOn(Bridge& bridge) {
    bool ok = true;
    ok &= bridge.registerCommand("spell.state",   [this](const json& p) { return state(p); });
    ok &= bridge.registerCommand("spell.load",    [this](const json& p) { return load(p); });
    ok &= bridge.registerCommand("spell.check",   [this](const json& p) { return check(p); });
    ok &= bridge.registerCommand("spell.suggest", [this](const json& p) { return suggest(p); });
    ok &= bridge.registerCommand("spell.synonyms",[this](const json& p) { return synonyms(p); });
    ok &= bridge.registerCommand("spell.addWord", [this](const json& p) { return addWord(p); });
    ok &= bridge.registerCommand("spell.removeWord",
                                                  [this](const json& p) { return removeWord(p); });
    ok &= bridge.registerCommand("spell.ignore",  [this](const json& p) { return ignore(p); });
    return ok;
}

void SpellApi::loadPersonal(const std::string& manuscriptPath) {
    if (manuscriptPath.empty()) return;
    m_personalPath = personalPathFor(utils::pathFromUtf8(manuscriptPath));

    bool ok = false;
    const std::string content = readWholeFile(m_personalPath, &ok);
    std::vector<std::string> words;
    if (ok) {
        size_t start = 0;
        while (start <= content.size()) {
            size_t end = content.find('\n', start);
            if (end == std::string::npos) end = content.size();
            std::string word = content.substr(start, end - start);
            if (!word.empty() && word.back() == '\r') word.pop_back();
            // Un fichier édité à la main peut contenir n'importe quoi :
            // on ne garde que des mots plausibles.
            if (!word.empty() && word.size() <= 100 &&
                word.find(' ') == std::string::npos) {
                words.push_back(std::move(word));
            }
            if (end == content.size()) break;
            start = end + 1;
        }
    }
    m_speller.setPersonal(std::move(words));
}

bool SpellApi::savePersonal(std::string* errorMessage) const {
    if (m_personalPath.empty()) return true; // rien à écrire : pas une erreur

    std::string text;
    for (const std::string& word : m_speller.personal()) text += word + "\n";

    // Écriture atomique, comme toute écriture du projet.
    fs::path tmp = m_personalPath;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) {
                *errorMessage = "Dictionnaire personnel inaccessible : " +
                                utils::pathToUtf8(tmp);
            }
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            fs::remove(tmp, ignored);
            if (errorMessage) *errorMessage = "Échec d'écriture du dictionnaire personnel.";
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, m_personalPath, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        if (errorMessage) *errorMessage = "Échec du remplacement du dictionnaire personnel.";
        return false;
    }
    return true;
}

json SpellApi::state(const json&) {
    const fs::path folder = findDictionaryFolder();
    json personal = json::array();
    for (const std::string& word : m_speller.personal()) personal.push_back(word);

    return {{"available", !folder.empty()},
            {"folder", folder.empty() ? std::string() : utils::pathToUtf8(folder)},
            {"loaded", m_speller.loaded()},
            {"forms", m_speller.formCount()},
            {"thesaurus", m_thesaurus.loaded()},
            {"thesaurusEntries", m_thesaurus.entryCount()},
            {"personal", personal},
            {"personalPath", m_personalPath.empty() ? std::string()
                                                    : utils::pathToUtf8(m_personalPath)},
            {"error", m_lastError}};
}

json SpellApi::load(const json& payload) {
    // Le dictionnaire personnel suit le manuscrit : il se recharge donc à
    // chaque appel, même si les dictionnaires généraux sont déjà en place.
    loadPersonal(optString(payload, "path"));

    if (m_speller.loaded() || m_triedLoad) return state(payload);
    m_triedLoad = true;

    const fs::path folder = findDictionaryFolder();
    if (folder.empty()) {
        m_lastError = "Dictionnaires absents : lancez scripts\\fetch_deps.bat.";
        return state(payload);
    }

    bool affOk = false, dicOk = false;
    const std::string aff = readWholeFile(folder / "fr.aff", &affOk);
    const std::string dic = readWholeFile(folder / "fr.dic", &dicOk);
    if (!affOk || !dicOk) {
        m_lastError = "Fichiers de dictionnaire illisibles dans " +
                      utils::pathToUtf8(folder) + ".";
        return state(payload);
    }

    const io::AffixData affix = io::parseAff(aff);
    m_speller.setForms(io::expandDictionary(dic, affix));
    m_speller.setReplacements(affix.replacements);

    // Le thésaurus est facultatif : son absence ne doit pas priver
    // l'auteur du correcteur.
    std::string thesaurusError;
    m_thesaurus.loadFile(folder / "thes_fr.dat", &thesaurusError);

    m_lastError = m_speller.loaded() ? std::string()
                                     : "Aucune forme n'a pu être produite du dictionnaire.";
    return state(payload);
}

json SpellApi::check(const json& payload) {
    const std::string text = requireString(payload, "text");
    const std::vector<core::Speller::Misspelling> found =
        m_speller.check(utils::utf8ToU32(text));

    json words = json::array();
    for (const core::Speller::Misspelling& item : found) {
        words.push_back({{"start", item.start}, {"length", item.length}, {"word", item.word}});
    }
    return {{"words", words}, {"loaded", m_speller.loaded()}};
}

json SpellApi::suggest(const json& payload) {
    const std::string word = requireString(payload, "word");
    const size_t max = optIndex(payload, "max", 7);
    json list = json::array();
    for (const std::string& candidate : m_speller.suggest(word, max)) list.push_back(candidate);
    return {{"word", word}, {"suggestions", list}};
}

json SpellApi::synonyms(const json& payload) {
    const std::string word = requireString(payload, "word");
    json meanings = json::array();
    for (const io::ThesaurusMeaning& meaning : m_thesaurus.lookup(word)) {
        json synonymList = json::array();
        for (const std::string& synonym : meaning.synonyms) synonymList.push_back(synonym);
        meanings.push_back({{"label", meaning.label}, {"synonyms", synonymList}});
    }
    return {{"word", word}, {"meanings", meanings}, {"loaded", m_thesaurus.loaded()}};
}

json SpellApi::addWord(const json& payload) {
    const std::string word = requireString(payload, "word");
    if (word.empty()) throw std::runtime_error("Mot vide.");
    const std::string path = optString(payload, "path");
    if (!path.empty() && m_personalPath.empty()) loadPersonal(path);

    const bool added = m_speller.addPersonal(word);
    std::string error;
    const bool saved = savePersonal(&error);
    return {{"added", added},
            {"saved", saved && !m_personalPath.empty()},
            {"error", saved ? std::string() : error},
            {"count", m_speller.personal().size()}};
}

json SpellApi::removeWord(const json& payload) {
    const std::string word = requireString(payload, "word");
    const bool removed = m_speller.removePersonal(word);
    std::string error;
    const bool saved = savePersonal(&error);
    return {{"removed", removed},
            {"saved", saved && !m_personalPath.empty()},
            {"error", saved ? std::string() : error},
            {"count", m_speller.personal().size()}};
}

json SpellApi::ignore(const json& payload) {
    const std::string word = requireString(payload, "word");
    m_speller.ignoreWord(word);
    return {{"ignored", word}};
}

} // namespace bridge
