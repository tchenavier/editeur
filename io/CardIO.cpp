// CardIO.cpp
// Implémentation de la lecture/écriture des fiches. Voir CardIO.h.

#include "CardIO.h"

#include "PathUtf8.h"
#include "Utf8.h"
#include "Yaml.h"

#include <fstream>
#include <iterator>

namespace io {

const char* const kCardNotesField = "notes";

namespace {

// Clés réservées de l'en-tête : elles décrivent la fiche elle-même et ne
// sont donc pas des champs de schéma.
const char* const kKeyId = "id";
const char* const kKeyType = "type";
const char* const kKeyTitle = "titre";
const char* const kKeyTags = "tags";

bool isReservedKey(const std::string& key) {
    return key == kKeyId || key == kKeyType || key == kKeyTitle || key == kKeyTags;
}

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Écriture atomique (fichier temporaire + renommage), comme pour le
// manuscrit : une coupure ne laisse jamais un fichier à moitié écrit.
bool writeFileAtomic(const std::filesystem::path& path, const std::string& content,
                     std::string* errorMessage) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        if (errorMessage) {
            *errorMessage = "Impossible de créer le dossier : " + utils::pathToUtf8(path.parent_path());
        }
        return false;
    }

    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (errorMessage) *errorMessage = "Impossible d'écrire : " + utils::pathToUtf8(tmp);
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            if (errorMessage) *errorMessage = "Échec d'écriture : " + utils::pathToUtf8(tmp);
            return false;
        }
    } // fermeture AVANT le renommage (obligatoire sous Windows)

    fs::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        if (errorMessage) {
            *errorMessage = "Échec du renommage vers : " + utils::pathToUtf8(path) +
                            " (" + ec.message() + ")";
        }
        return false;
    }
    return true;
}

} // namespace

// =====================================================================
// slugify
// =====================================================================
std::string slugify(const std::string& title) {
    // On travaille en points de code : un caractère accentué ne doit pas
    // être coupé en deux octets.
    const std::u32string cps = utils::utf8ToU32(title);
    std::u32string out;
    bool lastWasDash = true; // évite un tiret en tête

    for (char32_t c : cps) {
        // Repli des lettres accentuées latines vers leur base ASCII.
        if (c >= 0xC0 && c <= 0xFF) {
            static const char* const kBase =
                "aaaaaaaceeeeiiiidnooooo*ouuuuypsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
            const char base = kBase[c - 0xC0];
            c = static_cast<char32_t>(base);
        }
        if (c >= U'A' && c <= U'Z') c = c + 0x20; // minuscule

        const bool keep = (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9');
        if (keep) { out += c; lastWasDash = false; }
        else if (!lastWasDash) { out += U'-'; lastWasDash = true; }
    }
    while (!out.empty() && out.back() == U'-') out.pop_back();

    const std::string slug = utils::u32ToUtf8(out);
    return slug.empty() ? "fiche" : slug;
}

// =====================================================================
// Sérialisation
// =====================================================================
std::string serializeCard(const core::Card& card, const core::SchemaRegistry* registry) {
    const core::CardSchema* schema = registry ? registry->schema(card.typeId()) : nullptr;

    YamlBlock block;
    block.setScalar(kKeyId, card.id());
    block.setScalar(kKeyType, card.typeId());
    if (!card.title().empty()) block.setScalar(kKeyTitle, card.title());
    // Les tags sont par nature une liste : forme stable, toujours à tirets.
    if (!card.tags().empty()) block.setList(kKeyTags, card.tags(), /*forceList=*/true);

    // Champs de la fiche (ordre alphabétique garanti par le std::map du
    // modèle : deux sauvegardes identiques produisent le même fichier).
    std::string notes;
    for (const auto& [key, value] : card.fields()) {
        if (value.empty()) continue;
        if (key == kCardNotesField) { notes = value.scalar(); continue; }
        if (isReservedKey(key)) continue; // ne jamais écraser les clés réservées
        // Le schéma (s'il est connu) décide de la forme : un champ de type
        // liste reste une liste même avec une seule valeur.
        const core::FieldDefinition* def = schema ? schema->field(key) : nullptr;
        const bool forceList = def && core::isListField(def->type);
        block.setList(key, value.values(), forceList);
    }

    std::string out = "---\n";
    out += block.toString();
    out += "---\n";
    if (!notes.empty()) {
        out += "\n" + notes + "\n";
    }
    return out;
}

bool parseCard(const std::string& text, core::Card& out) {
    std::vector<std::string> header;
    std::string body;
    splitFrontMatter(text, header, body);
    if (header.empty()) return false; // pas d'en-tête : ce n'est pas une fiche

    const YamlBlock block = YamlBlock::parse(header);
    const std::string id = block.scalar(kKeyId);
    if (id.empty()) return false; // sans identifiant, la fiche est inexploitable

    core::Card card(id, block.scalar(kKeyType));
    card.setTitle(block.scalar(kKeyTitle));
    if (const auto* tags = block.get(kKeyTags)) {
        for (const auto& tag : *tags) card.addTag(tag);
    }
    for (const auto& entry : block.entries()) {
        if (isReservedKey(entry.key)) continue;
        card.editField(entry.key).setValues(entry.values);
    }
    const std::string notes = trim(body);
    if (!notes.empty()) card.editField(kCardNotesField).setScalar(notes);

    out = std::move(card);
    return true;
}

// =====================================================================
// Fichiers
// =====================================================================
std::filesystem::path cardPath(const std::filesystem::path& root, const core::Card& card) {
    const std::string type = card.typeId().empty() ? "divers" : card.typeId();
    std::filesystem::path path = root / utils::pathFromUtf8(type) / utils::pathFromUtf8(card.id());
    path += ".md";
    return path;
}

bool saveCard(const core::Card& card, const std::filesystem::path& root,
              std::string* errorMessage, const core::SchemaRegistry* registry) {
    if (card.id().empty()) {
        if (errorMessage) *errorMessage = "Fiche sans identifiant : enregistrement impossible.";
        return false;
    }
    return writeFileAtomic(cardPath(root, card), serializeCard(card, registry), errorMessage);
}

bool removeCardFile(const core::Card& card, const std::filesystem::path& root,
                    std::string* errorMessage) {
    std::error_code ec;
    const std::filesystem::path path = cardPath(root, card);
    if (!std::filesystem::remove(path, ec) || ec) {
        if (errorMessage) *errorMessage = "Impossible de supprimer : " + utils::pathToUtf8(path);
        return false;
    }
    return true;
}

bool loadLibrary(core::CardLibrary& library, const std::filesystem::path& root,
                 size_t* skipped, std::string* errorMessage) {
    namespace fs = std::filesystem;
    if (skipped) *skipped = 0;

    std::error_code ec;
    if (!fs::exists(root, ec)) {
        // Pas encore de bible narrative : ce n'est pas une erreur.
        return true;
    }

    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".md") continue;

        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) { if (skipped) ++*skipped; continue; }
        const std::string content{std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>()};

        core::Card card;
        // Un fichier abîmé ne doit pas empêcher de charger les autres :
        // on l'ignore en le comptant, plutôt que d'échouer en bloc.
        if (!parseCard(content, card)) { if (skipped) ++*skipped; continue; }
        library.put(std::move(card));
    }
    if (ec && errorMessage) {
        *errorMessage = "Lecture incomplète du dossier : " + utils::pathToUtf8(root);
    }
    return true;
}

bool saveLibrary(const core::CardLibrary& library, const std::filesystem::path& root,
                 std::string* errorMessage, const core::SchemaRegistry* registry) {
    for (const auto& [id, card] : library.cards()) {
        if (!saveCard(card, root, errorMessage, registry)) return false;
    }
    return true;
}

} // namespace io
