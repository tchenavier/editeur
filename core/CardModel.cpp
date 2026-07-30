// CardModel.cpp
// Implémentation du modèle de fiches. Voir CardModel.h pour la conception
// (modèle piloté par schéma, extensible sans recompilation du reste).

#include "CardModel.h"

#include "Search.h" // core::foldLower : un seul repli de casse pour tout le projet
#include "Utf8.h"

#include <algorithm>

namespace core {

namespace {

const std::string kEmpty; // renvoyé par scalar() quand le champ est vide

// Compare deux chaînes UTF-8 sans tenir compte de la casse (y compris
// pour les majuscules accentuées, via le repli partagé avec la recherche).
std::u32string foldedOf(const std::string& utf8) {
    std::u32string out = utils::utf8ToU32(utf8);
    for (char32_t& c : out) c = foldLower(c);
    return out;
}

bool containsFolded(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    const std::u32string h = foldedOf(haystack);
    const std::u32string n = foldedOf(needle);
    return h.find(n) != std::u32string::npos;
}

// Tri d'affichage : par titre, en ignorant la casse.
bool byTitleLess(const Card* a, const Card* b) {
    const std::u32string fa = foldedOf(a->title());
    const std::u32string fb = foldedOf(b->title());
    if (fa != fb) return fa < fb;
    return a->id() < b->id(); // départage stable
}

} // namespace

// =====================================================================
// FieldType
// =====================================================================
bool isListField(FieldType type) {
    switch (type) {
        case FieldType::List:
        case FieldType::ImageList:
        case FieldType::CardRefList:
            return true;
        default:
            return false;
    }
}

// =====================================================================
// CardSchema
// =====================================================================
const FieldDefinition* CardSchema::field(const std::string& key) const {
    for (const auto& f : fields) {
        if (f.key == key) return &f;
    }
    return nullptr;
}

// =====================================================================
// FieldValue
// =====================================================================
FieldValue::FieldValue(std::string scalar) {
    if (!scalar.empty()) m_values.push_back(std::move(scalar));
}

FieldValue::FieldValue(std::vector<std::string> values) {
    setValues(std::move(values));
}

const std::string& FieldValue::scalar() const {
    return m_values.empty() ? kEmpty : m_values.front();
}

void FieldValue::setScalar(std::string value) {
    m_values.clear();
    if (!value.empty()) m_values.push_back(std::move(value));
}

void FieldValue::setValues(std::vector<std::string> values) {
    m_values.clear();
    for (auto& v : values) add(std::move(v)); // applique les règles (vide, doublon)
}

void FieldValue::add(std::string value) {
    if (value.empty()) return;
    if (contains(value)) return; // ensemble ordonné : pas de doublon
    m_values.push_back(std::move(value));
}

bool FieldValue::remove(const std::string& value) {
    const auto it = std::find(m_values.begin(), m_values.end(), value);
    if (it == m_values.end()) return false;
    m_values.erase(it);
    return true;
}

bool FieldValue::contains(const std::string& value) const {
    return std::find(m_values.begin(), m_values.end(), value) != m_values.end();
}

// =====================================================================
// Card
// =====================================================================
Card::Card(std::string id, std::string typeId)
    : m_id(std::move(id)), m_typeId(std::move(typeId)) {}

const FieldValue* Card::field(const std::string& key) const {
    const auto it = m_fields.find(key);
    return (it == m_fields.end()) ? nullptr : &it->second;
}

FieldValue& Card::editField(const std::string& key) {
    return m_fields[key]; // créé à la demande
}

bool Card::removeField(const std::string& key) {
    return m_fields.erase(key) > 0;
}

void Card::addTag(std::string tag) {
    if (tag.empty() || hasTag(tag)) return;
    m_tags.push_back(std::move(tag));
}

bool Card::removeTag(const std::string& tag) {
    const auto it = std::find(m_tags.begin(), m_tags.end(), tag);
    if (it == m_tags.end()) return false;
    m_tags.erase(it);
    return true;
}

bool Card::hasTag(const std::string& tag) const {
    return std::find(m_tags.begin(), m_tags.end(), tag) != m_tags.end();
}

size_t Card::removeReferencesTo(const std::string& cardId, const CardSchema* schema) {
    size_t removed = 0;
    for (auto& [key, value] : m_fields) {
        // Si le schéma est connu, on ne touche qu'aux champs de référence ;
        // sinon on inspecte tout (tolérance : schéma absent ou champ hérité).
        if (schema) {
            const FieldDefinition* def = schema->field(key);
            if (def && def->type != FieldType::CardRefList) continue;
        }
        if (value.remove(cardId)) ++removed;
    }
    return removed;
}

// =====================================================================
// Schémas intégrés (les six types listés au cadrage)
// =====================================================================
std::vector<CardSchema> builtinSchemas() {
    using F = FieldType;
    std::vector<CardSchema> schemas;

    schemas.push_back({"personnage", "Personnage", {
        {"description_physique", "Description physique", F::LongText},
        {"age",                  "Âge",                 F::Text},
        {"caractere",            "Caractère",           F::LongText},
        {"relations",            "Relations",           F::CardRefList},
        {"historique",           "Historique",          F::LongText},
        {"deces_chapitre",       "Décès (chapitre)",    F::ChapterRef},
        {"illustrations",        "Illustrations",       F::ImageList},
    }});

    schemas.push_back({"lieu", "Lieu", {
        {"ambiance",         "Ambiance",                  F::LongText},
        {"localisation",     "Localisation",              F::Text},
        {"faction",          "Pays / faction",            F::Text},
        {"points_interet",   "Points d'intérêt",          F::List},
        {"historique",       "Informations historiques",  F::LongText},
        {"sous_lieux",       "Sous-lieux",                F::CardRefList},
        {"illustrations",    "Illustrations",             F::ImageList},
    }});

    schemas.push_back({"langue", "Langue", {
        {"inspiration",   "Inspiration (latin, grec...)", F::Text},
        {"faction",       "Faction / royaume",            F::Text},
        {"equivalences",  "Équivalences de mots",         F::List},
    }});

    schemas.push_back({"religion", "Religion", {
        {"valeurs",     "Valeurs",             F::LongText},
        {"dieux",       "Dieux",               F::List},
        {"dirigeants",  "Dirigeants",          F::List},
        {"membres",     "Personnages membres", F::CardRefList},
    }});

    schemas.push_back({"creature", "Créature", {
        {"description_physique", "Description physique", F::LongText},
        {"age",                  "Âge",                 F::Text},
        {"caractere",            "Caractère",           F::LongText},
        {"relations",            "Relations",           F::CardRefList},
        {"historique",           "Historique",          F::LongText},
        {"deces_chapitre",       "Décès (chapitre)",    F::ChapterRef},
        {"illustrations",        "Illustrations",       F::ImageList},
    }});

    schemas.push_back({"magie", "Magie", {
        {"element",       "Élément",       F::Text},
        {"niveau",        "Niveau",        F::Text},
        {"effet",         "Effet",         F::LongText},
        {"description",   "Description",   F::LongText},
        {"illustrations", "Illustrations", F::ImageList},
    }});

    return schemas;
}

// =====================================================================
// SchemaRegistry
// =====================================================================
SchemaRegistry::SchemaRegistry() {
    for (auto& schema : builtinSchemas()) {
        registerSchema(std::move(schema));
    }
}

void SchemaRegistry::registerSchema(CardSchema schema) {
    if (schema.typeId.empty()) return;
    const std::string key = schema.typeId;
    m_schemas[key] = std::move(schema);
}

const CardSchema* SchemaRegistry::schema(const std::string& typeId) const {
    const auto it = m_schemas.find(typeId);
    return (it == m_schemas.end()) ? nullptr : &it->second;
}

bool SchemaRegistry::contains(const std::string& typeId) const {
    return m_schemas.find(typeId) != m_schemas.end();
}

std::vector<const CardSchema*> SchemaRegistry::all() const {
    std::vector<const CardSchema*> out;
    out.reserve(m_schemas.size());
    for (const auto& [id, schema] : m_schemas) out.push_back(&schema);
    std::sort(out.begin(), out.end(), [](const CardSchema* a, const CardSchema* b) {
        const std::u32string fa = foldedOf(a->label);
        const std::u32string fb = foldedOf(b->label);
        if (fa != fb) return fa < fb;
        return a->typeId < b->typeId;
    });
    return out;
}

// =====================================================================
// CardLibrary
// =====================================================================
Card& CardLibrary::put(Card card) {
    const std::string id = card.id();
    m_cards[id] = std::move(card);
    return m_cards[id];
}

bool CardLibrary::remove(const std::string& id, bool purgeReferences,
                         const SchemaRegistry* registry) {
    if (m_cards.erase(id) == 0) return false;
    if (purgeReferences) {
        for (auto& [otherId, card] : m_cards) {
            const CardSchema* schema = registry ? registry->schema(card.typeId()) : nullptr;
            card.removeReferencesTo(id, schema);
        }
    }
    return true;
}

const Card* CardLibrary::find(const std::string& id) const {
    const auto it = m_cards.find(id);
    return (it == m_cards.end()) ? nullptr : &it->second;
}

Card* CardLibrary::find(const std::string& id) {
    const auto it = m_cards.find(id);
    return (it == m_cards.end()) ? nullptr : &it->second;
}

std::vector<const Card*> CardLibrary::byType(const std::string& typeId) const {
    std::vector<const Card*> out;
    for (const auto& [id, card] : m_cards) {
        if (card.typeId() == typeId) out.push_back(&card);
    }
    std::sort(out.begin(), out.end(), byTitleLess);
    return out;
}

std::vector<const Card*> CardLibrary::byTag(const std::string& tag) const {
    std::vector<const Card*> out;
    for (const auto& [id, card] : m_cards) {
        if (card.hasTag(tag)) out.push_back(&card);
    }
    std::sort(out.begin(), out.end(), byTitleLess);
    return out;
}

std::vector<const Card*> CardLibrary::searchByTitle(const std::string& query) const {
    std::vector<const Card*> out;
    for (const auto& [id, card] : m_cards) {
        if (containsFolded(card.title(), query)) out.push_back(&card);
    }
    std::sort(out.begin(), out.end(), byTitleLess);
    return out;
}

std::vector<std::string> CardLibrary::allTags() const {
    std::vector<std::string> out;
    for (const auto& [id, card] : m_cards) {
        for (const auto& tag : card.tags()) {
            if (std::find(out.begin(), out.end(), tag) == out.end()) out.push_back(tag);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace core
