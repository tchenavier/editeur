// CardApi.cpp
// Implémentation des commandes cards.*. Voir CardApi.h.

#include "CardApi.h"

#include "CardIO.h"
#include "PathUtf8.h"
#include "Search.h"
#include "Utf8.h"

#include <stdexcept>

namespace bridge {

namespace {

// --- Noms des types de champs, tels que vus par l'interface -----------
// L'UI choisit le contrôle de saisie à partir de ce nom (ligne simple,
// zone de texte, liste modifiable, sélecteur d'images, sélecteur de
// fiches...). Ajouter un type de champ = ajouter une entrée ici et le
// contrôle correspondant côté JS.
const char* fieldTypeName(core::FieldType type) {
    switch (type) {
        case core::FieldType::Text:        return "text";
        case core::FieldType::LongText:    return "longtext";
        case core::FieldType::Number:      return "number";
        case core::FieldType::List:        return "list";
        case core::FieldType::ImageList:   return "images";
        case core::FieldType::CardRefList: return "cardrefs";
        case core::FieldType::ChapterRef:  return "chapterref";
    }
    return "text";
}

// requireString / optString / optBool viennent de Bridge.h (validation
// commune à tous les modules *Api).

core::SearchOptions readOptions(const json& payload) {
    core::SearchOptions opts;
    opts.caseSensitive = optBool(payload, "caseSensitive", false);
    opts.wholeWord = optBool(payload, "wholeWord", false);
    return opts;
}

// Extrait un court aperçu autour de la première occurrence, pour la liste
// de résultats (une valeur de champ, ou le corps « notes », peut être long).
std::string snippet(const std::string& value) {
    const std::u32string u = utils::utf8ToU32(value);
    constexpr size_t kMax = 90;
    if (u.size() <= kMax) return value;
    return utils::u32ToUtf8(u.substr(0, kMax)) + "…";
}

// Lit un tableau de chaînes ; ignore silencieusement les éléments d'un
// autre type plutôt que de rejeter toute la requête.
std::vector<std::string> readStringArray(const json& value) {
    std::vector<std::string> out;
    if (!value.is_array()) return out;
    for (const auto& item : value) {
        if (item.is_string()) out.push_back(item.get<std::string>());
    }
    return out;
}

} // namespace

// =====================================================================
// Sérialisations
// =====================================================================

json cardToJson(const core::Card& card) {
    json fields = json::object();
    for (const auto& [key, value] : card.fields()) {
        fields[key] = value.values(); // toujours un tableau : voir FieldValue
    }
    return {
        {"id", card.id()},
        {"type", card.typeId()},
        {"title", card.title()},
        {"tags", card.tags()},
        {"fields", fields},
    };
}

json cardSummaryToJson(const core::Card& card) {
    json j = {
        {"id", card.id()},
        {"type", card.typeId()},
        {"title", card.title()},
        {"tags", card.tags()},
    };
    // Chapitre de décès (s'il est renseigné) : le manuscrit en a besoin
    // pour colorer en rouge les mentions postérieures à la mort. Clé de
    // convention, indépendante du reste du résumé.
    const core::FieldValue* death = card.field("deces_chapitre");
    if (death && !death->empty()) j["deathChapter"] = death->scalar();
    return j;
}

json schemaToJson(const core::CardSchema& schema) {
    json fields = json::array();
    for (const auto& def : schema.fields) {
        fields.push_back({
            {"key", def.key},
            {"label", def.label},
            {"type", fieldTypeName(def.type)},
        });
    }
    return {{"typeId", schema.typeId}, {"label", schema.label}, {"fields", fields}};
}

// =====================================================================
// CardApi
// =====================================================================

CardApi::CardApi(core::CardLibrary& library, core::SchemaRegistry& registry)
    : m_library(library), m_registry(registry) {}

bool CardApi::registerOn(Bridge& bridge) {
    bool ok = true;
    ok &= bridge.registerCommand("cards.schemas", [this](const json& p) { return schemas(p); });
    ok &= bridge.registerCommand("cards.open",    [this](const json& p) { return open(p); });
    ok &= bridge.registerCommand("cards.setRoot", [this](const json& p) { return setRoot(p); });
    ok &= bridge.registerCommand("cards.state",   [this](const json& p) { return state(p); });
    ok &= bridge.registerCommand("cards.list",    [this](const json& p) { return list(p); });
    ok &= bridge.registerCommand("cards.get",     [this](const json& p) { return get(p); });
    ok &= bridge.registerCommand("cards.save",    [this](const json& p) { return save(p); });
    ok &= bridge.registerCommand("cards.remove",  [this](const json& p) { return remove(p); });
    ok &= bridge.registerCommand("cards.tags",    [this](const json& p) { return tags(p); });
    ok &= bridge.registerCommand("cards.saveAll", [this](const json& p) { return saveAll(p); });
    ok &= bridge.registerCommand("cards.search",  [this](const json& p) { return search(p); });
    ok &= bridge.registerCommand("cards.replaceAll", [this](const json& p) { return replaceAll(p); });
    ok &= bridge.registerCommand("cards.checkRefs",  [this](const json& p) { return checkRefs(p); });
    return ok;
}

// --- Aides ------------------------------------------------------------

void CardApi::persist(const core::Card& card) {
    if (m_root.empty()) return; // pas de dossier fixé : on reste en mémoire
    std::string error;
    if (!io::saveCard(card, m_root, &error, &m_registry)) {
        throw std::runtime_error(error);
    }
    m_cardTokens[card.id()] = utils::fileModToken(io::cardPath(m_root, card));
}

void CardApi::recordAllTokens() {
    m_cardTokens.clear();
    if (m_root.empty()) return;
    for (const auto& [id, card] : m_library.cards()) {
        m_cardTokens[id] = utils::fileModToken(io::cardPath(m_root, card));
    }
}

std::string CardApi::makeUniqueId(const std::string& title) const {
    const std::string base = io::slugify(title);
    if (!m_library.find(base)) return base;
    // Collision : on suffixe jusqu'à trouver libre (aria, aria-2, aria-3...).
    for (int n = 2; n < 10000; ++n) {
        const std::string candidate = base + "-" + std::to_string(n);
        if (!m_library.find(candidate)) return candidate;
    }
    throw std::runtime_error("Impossible de créer un identifiant unique pour : " + title);
}

// --- Commandes --------------------------------------------------------

json CardApi::schemas(const json&) {
    json out = json::array();
    for (const core::CardSchema* schema : m_registry.all()) {
        out.push_back(schemaToJson(*schema));
    }
    return {{"schemas", out}};
}

json CardApi::open(const json& payload) {
    const std::filesystem::path root = utils::pathFromUtf8(requireString(payload, "path"));

    core::CardLibrary loaded;
    size_t skipped = 0;
    std::string error;
    if (!io::loadLibrary(loaded, root, &skipped, &error)) {
        throw std::runtime_error(error.empty() ? "Chargement des fiches impossible." : error);
    }
    m_library = std::move(loaded);
    m_root = root;
    recordAllTokens(); // dates de référence des fiches qu'on vient de lire
    return {{"root", utils::pathToUtf8(m_root)},
            {"count", m_library.size()},
            {"skipped", skipped}};
}

json CardApi::setRoot(const json& payload) {
    m_root = utils::pathFromUtf8(requireString(payload, "path"));
    // Écrit les fiches déjà en mémoire vers ce dossier (aucune écriture si
    // la bibliothèque est vide : le dossier n'est créé que s'il y a de quoi
    // le remplir). Une erreur d'écriture remonte au JS, qui la montre.
    std::string error;
    if (!io::saveLibrary(m_library, m_root, &error, &m_registry)) {
        throw std::runtime_error(error);
    }
    recordAllTokens();
    return {{"root", utils::pathToUtf8(m_root)}, {"count", m_library.size()}};
}

json CardApi::state(const json&) {
    return {{"root", utils::pathToUtf8(m_root)}, {"count", m_library.size()}};
}

json CardApi::list(const json& payload) {
    const std::string type = optString(payload, "type");
    const std::string tag = optString(payload, "tag");
    const std::string query = optString(payload, "query");

    // On part du filtre le plus sélectif disponible, puis on affine
    // ci-dessous. Sans filtre, searchByTitle("") renvoie tout, déjà trié
    // par titre — même ordre que les autres chemins.
    std::vector<const core::Card*> cards;
    if (!query.empty())     cards = m_library.searchByTitle(query);
    else if (!type.empty()) cards = m_library.byType(type);
    else if (!tag.empty())  cards = m_library.byTag(tag);
    else                    cards = m_library.searchByTitle("");

    json out = json::array();
    for (const core::Card* card : cards) {
        if (!type.empty() && card->typeId() != type) continue;
        if (!tag.empty() && !card->hasTag(tag)) continue;
        out.push_back(cardSummaryToJson(*card));
    }
    return {{"cards", out}};
}

json CardApi::get(const json& payload) {
    const std::string id = requireString(payload, "id");
    const core::Card* card = m_library.find(id);
    if (!card) throw std::runtime_error("Fiche introuvable : " + id);
    return {{"card", cardToJson(*card)}};
}

json CardApi::save(const json& payload) {
    const std::string type = requireString(payload, "type");
    if (!m_registry.contains(type)) {
        // Type inconnu : on refuse explicitement plutôt que de créer une
        // fiche qu'aucun formulaire ne saurait afficher.
        throw std::runtime_error("Type de fiche inconnu : " + type);
    }
    const std::string title = optString(payload, "title");
    const bool force = payload.contains("force") && payload["force"].is_boolean() &&
                       payload["force"].get<bool>();

    const std::string givenId = optString(payload, "id");
    std::string id = givenId;
    if (id.empty()) id = makeUniqueId(title);

    core::Card card(id, type);
    card.setTitle(title);
    if (payload.contains("tags")) {
        for (const auto& tag : readStringArray(payload["tags"])) card.addTag(tag);
    }
    if (payload.contains("fields") && payload["fields"].is_object()) {
        for (const auto& [key, value] : payload["fields"].items()) {
            if (key.empty()) continue;
            if (value.is_string()) {
                card.editField(key).setScalar(value.get<std::string>());
            } else if (value.is_array()) {
                card.editField(key).setValues(readStringArray(value));
            }
            // Tout autre type est ignoré : le modèle ne stocke que du texte.
        }
    }

    // Modification externe : la fiche existante a-t-elle changé sur le
    // disque depuis qu'on l'a lue ? (Sans objet pour une fiche neuve, dont
    // le fichier n'existe pas encore.) On ne l'écrase pas sans accord.
    if (!force && !givenId.empty() && !m_root.empty()) {
        const auto path = io::cardPath(m_root, card);
        const auto it = m_cardTokens.find(id);
        if (it != m_cardTokens.end() && std::filesystem::exists(path) &&
            utils::fileModToken(path) != it->second) {
            return {{"conflict", true}, {"id", id}};
        }
    }

    const core::Card& stored = m_library.put(std::move(card));
    persist(stored); // met aussi à jour le jeton de la fiche
    return {{"card", cardToJson(stored)}};
}

json CardApi::remove(const json& payload) {
    const std::string id = requireString(payload, "id");
    const core::Card* card = m_library.find(id);
    if (!card) return {{"removed", false}};

    // Le fichier d'abord : si l'effacement disque échoue, on n'a pas déjà
    // perdu la fiche en mémoire.
    if (!m_root.empty()) {
        std::string error;
        io::removeCardFile(*card, m_root, &error); // absence de fichier : sans gravité
    }
    // purgeReferences : aucune autre fiche ne doit garder un lien mort.
    const bool removed = m_library.remove(id, /*purgeReferences=*/true, &m_registry);

    m_cardTokens.erase(id);
    // Les fiches dont les références ont changé doivent être réécrites. Comme
    // partout ailleurs (save, saveAll), un échec d'écriture est signalé et non
    // avalé : sinon la suppression paraîtrait réussie alors que le disque
    // garderait des liens morts vers la fiche supprimée.
    if (removed && !m_root.empty()) {
        std::string error;
        if (!io::saveLibrary(m_library, m_root, &error, &m_registry)) {
            throw std::runtime_error(error);
        }
        recordAllTokens(); // leurs dates ont changé
    }
    return {{"removed", removed}};
}

json CardApi::tags(const json&) {
    return {{"tags", m_library.allTags()}};
}

json CardApi::saveAll(const json&) {
    if (m_root.empty()) {
        throw std::runtime_error("Aucun dossier de fiches : utilisez d'abord cards.open.");
    }
    std::string error;
    if (!io::saveLibrary(m_library, m_root, &error, &m_registry)) {
        throw std::runtime_error(error);
    }
    recordAllTokens();
    return {{"saved", m_library.size()}};
}

json CardApi::search(const json& payload) {
    const std::u32string query = utils::utf8ToU32(requireString(payload, "query"));
    const core::SearchOptions opts = readOptions(payload);

    json results = json::array();
    if (query.empty()) return {{"results", results}};

    auto addResult = [&](const core::Card& card, const std::string& fieldKey,
                         const std::string& fieldLabel, const std::string& value) {
        results.push_back({
            {"id", card.id()}, {"type", card.typeId()}, {"title", card.title()},
            {"field", fieldKey}, {"fieldLabel", fieldLabel}, {"snippet", snippet(value)},
        });
    };

    for (const core::Card* card : m_library.searchByTitle("")) { // toutes, triées
        const core::CardSchema* schema = m_registry.schema(card->typeId());
        if (core::contains(utils::utf8ToU32(card->title()), query, opts)) {
            addResult(*card, "titre", "Titre", card->title());
        }
        for (const auto& [key, value] : card->fields()) {
            const core::FieldDefinition* def = schema ? schema->field(key) : nullptr;
            const std::string label = def ? def->label : key;
            // Une seule entrée par champ, même si plusieurs valeurs matchent.
            for (const std::string& v : value.values()) {
                if (core::contains(utils::utf8ToU32(v), query, opts)) {
                    addResult(*card, key, label, v);
                    break;
                }
            }
        }
    }
    return {{"results", results}};
}

json CardApi::replaceAll(const json& payload) {
    const std::u32string query = utils::utf8ToU32(requireString(payload, "query"));
    const std::u32string replacement = utils::utf8ToU32(requireString(payload, "replacement"));
    const core::SearchOptions opts = readOptions(payload);
    if (query.empty()) return {{"count", 0}, {"cards", 0}};

    size_t total = 0;
    size_t touched = 0;

    // On récupère d'abord les identifiants (l'itération donne des const&),
    // puis on modifie chaque fiche par accès mutable.
    std::vector<std::string> ids;
    for (const auto& [id, card] : m_library.cards()) ids.push_back(id);

    for (const std::string& id : ids) {
        core::Card* card = m_library.find(id);
        if (!card) continue;
        const core::CardSchema* schema = m_registry.schema(card->typeId());
        size_t cardCount = 0;

        // Titre.
        size_t c = 0;
        const std::u32string newTitle = core::replaceInString(
            utils::utf8ToU32(card->title()), query, replacement, opts, &c);
        if (c) { card->setTitle(utils::u32ToUtf8(newTitle)); cardCount += c; }

        // Champs de texte. On NE touche PAS aux références de fiches ni aux
        // chemins d'images : y remplacer du texte casserait des liens.
        std::vector<std::string> keys;
        for (const auto& [k, v] : card->fields()) keys.push_back(k);
        for (const std::string& key : keys) {
            const core::FieldDefinition* def = schema ? schema->field(key) : nullptr;
            if (def && (def->type == core::FieldType::CardRefList ||
                        def->type == core::FieldType::ImageList ||
                        def->type == core::FieldType::ChapterRef)) {
                continue; // références / chemins : on n'y remplace pas de texte
            }
            core::FieldValue& fv = card->editField(key);
            std::vector<std::string> updated;
            updated.reserve(fv.size());
            for (const std::string& v : fv.values()) {
                size_t vc = 0;
                const std::u32string nv = core::replaceInString(
                    utils::utf8ToU32(v), query, replacement, opts, &vc);
                cardCount += vc;
                updated.push_back(utils::u32ToUtf8(nv));
            }
            fv.setValues(updated);
        }

        if (cardCount) {
            total += cardCount;
            ++touched;
            persist(*card); // réécrit le fichier + met à jour son jeton
        }
    }
    return {{"count", total}, {"cards", touched}};
}

json CardApi::checkRefs(const json&) {
    json broken = json::array();
    for (const core::Card* card : m_library.searchByTitle("")) { // toutes, triées
        const core::CardSchema* schema = m_registry.schema(card->typeId());
        if (!schema) continue; // sans schéma, on ne sait pas quels champs sont des liens
        for (const auto& def : schema->fields) {
            if (def.type != core::FieldType::CardRefList) continue;
            const core::FieldValue* fv = card->field(def.key);
            if (!fv) continue;
            for (const std::string& targetId : fv->values()) {
                if (!m_library.find(targetId)) {
                    broken.push_back({
                        {"id", card->id()}, {"title", card->title()}, {"type", card->typeId()},
                        {"field", def.key}, {"fieldLabel", def.label}, {"targetId", targetId},
                    });
                }
            }
        }
    }
    return {{"broken", broken}};
}

} // namespace bridge
