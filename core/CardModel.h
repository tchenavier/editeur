// CardModel.h
// -----------------------------------------------------------------------
// Phase 2 : fiches de worldbuilding (personnages, lieux, langues, religions,
// créatures, magie...).
//
// EXIGENCE CENTRALE DU CADRAGE : « modèle de données extensible (ajout d'un
// nouveau type de fiche sans tout recoder) ». La conception retenue est donc
// PILOTÉE PAR SCHÉMA :
//
//   - une fiche (Card) ne connaît PAS son type ; elle stocke des couples
//     clé -> valeur, plus un titre et des tags ;
//   - un schéma (CardSchema) décrit les champs d'un type : clé, libellé,
//     nature (texte, paragraphe, nombre, liste, images, références) ;
//   - le registre (SchemaRegistry) contient les schémas intégrés ET accepte
//     l'enregistrement de nouveaux schémas à l'exécution.
//
// Conséquence : ajouter « fiche de vaisseau » = ajouter un schéma (donnée),
// sans toucher à Card, CardLibrary, la sérialisation ni l'interface. Les
// écrans se construisent en parcourant le schéma.
//
// Tolérance à l'évolution : une fiche peut porter des champs absents du
// schéma (fichier écrit avec une version plus récente, schéma modifié). On
// les conserve au lieu de les jeter, pour ne jamais perdre de données.
//
// Aucune dépendance à l'interface : testable seul (comme tout core/).
// -----------------------------------------------------------------------

#pragma once

#include <map>
#include <string>
#include <vector>

namespace core {

// ---------------------------------------------------------------------
// Nature d'un champ. Détermine la manière de l'éditer et de l'afficher,
// jamais la manière de le stocker (cf. FieldValue).
// ---------------------------------------------------------------------
enum class FieldType {
    Text,        // une ligne (âge, localisation...)
    LongText,    // paragraphe (description, historique...)
    Number,      // nombre saisi sous forme de texte (validé à la saisie)
    List,        // liste de valeurs libres (points d'intérêt, dieux...)
    ImageList,   // liste de chemins d'illustrations (PNG)
    CardRefList, // liste d'identifiants d'autres fiches (relations, sous-lieux)
    ChapterRef,  // titre d'un chapitre du manuscrit (ex : chapitre de décès).
                 // Sert à la détection d'incohérences : un personnage mort au
                 // chapitre N ne devrait pas réapparaître après.
};

// Vrai si le champ contient plusieurs valeurs.
bool isListField(FieldType type);

// ---------------------------------------------------------------------
// Description d'un champ dans un schéma.
// ---------------------------------------------------------------------
struct FieldDefinition {
    std::string key;    // identifiant stable, utilisé dans les fichiers
    std::string label;  // libellé affiché à l'utilisateur
    FieldType type = FieldType::Text;
};

// ---------------------------------------------------------------------
// Schéma d'un type de fiche. C'est une DONNÉE : en ajouter un suffit à
// créer un nouveau type de fiche.
// ---------------------------------------------------------------------
struct CardSchema {
    std::string typeId;  // identifiant stable ("personnage", "lieu"...)
    std::string label;   // libellé affiché ("Personnage")
    std::vector<FieldDefinition> fields;

    // Retourne la définition du champ, ou nullptr s'il n'appartient pas
    // au schéma (champ hérité d'une version antérieure, par exemple).
    const FieldDefinition* field(const std::string& key) const;
};

// ---------------------------------------------------------------------
// Valeur d'un champ.
//
// Représentation UNIQUE : une liste de chaînes. Un champ scalaire est une
// liste d'au plus un élément. Ce choix évite un variant, se sérialise
// directement en YAML, et permet de changer un champ scalaire en liste
// (ou l'inverse) sans migration de données.
// ---------------------------------------------------------------------
class FieldValue {
public:
    FieldValue() = default;
    explicit FieldValue(std::string scalar);
    explicit FieldValue(std::vector<std::string> values);

    // Vue scalaire : premier élément, ou chaîne vide si le champ est vide.
    const std::string& scalar() const;
    void setScalar(std::string value);

    const std::vector<std::string>& values() const { return m_values; }
    void setValues(std::vector<std::string> values);

    // Ajoute une valeur (ignorée si vide ou déjà présente : les listes du
    // modèle sont des ensembles ordonnés, sans doublon).
    void add(std::string value);
    bool remove(const std::string& value);
    bool contains(const std::string& value) const;

    bool empty() const { return m_values.empty(); }
    size_t size() const { return m_values.size(); }
    void clear() { m_values.clear(); }

private:
    std::vector<std::string> m_values;
};

// ---------------------------------------------------------------------
// Une fiche. Volontairement ignorante de son schéma : elle ne fait que
// porter des données. Toutes les chaînes sont en UTF-8.
// ---------------------------------------------------------------------
class Card {
public:
    Card() = default;
    Card(std::string id, std::string typeId);

    const std::string& id() const { return m_id; }
    const std::string& typeId() const { return m_typeId; }
    void setTypeId(std::string typeId) { m_typeId = std::move(typeId); }

    const std::string& title() const { return m_title; }
    void setTitle(std::string title) { m_title = std::move(title); }

    // Lecture : nullptr si le champ n'est pas renseigné (une simple
    // lecture ne crée jamais de champ).
    const FieldValue* field(const std::string& key) const;
    // Écriture : retourne une référence modifiable, en créant le champ au
    // besoin. Nom distinct de field() volontairement : deux surcharges qui
    // renvoient l'une un pointeur et l'autre une référence prêtent à
    // confusion à l'usage.
    FieldValue& editField(const std::string& key);
    const std::map<std::string, FieldValue>& fields() const { return m_fields; }
    bool removeField(const std::string& key);

    // Tags transversaux (partagés par tous les types de fiches).
    const std::vector<std::string>& tags() const { return m_tags; }
    void addTag(std::string tag);
    bool removeTag(const std::string& tag);
    bool hasTag(const std::string& tag) const;

    // Retire 'cardId' de tous les champs de type référence. Utilisé quand
    // une fiche est supprimée, pour ne pas laisser de lien mort.
    // 'schema' peut être nul : on parcourt alors tous les champs.
    size_t removeReferencesTo(const std::string& cardId, const CardSchema* schema);

private:
    std::string m_id;
    std::string m_typeId;
    std::string m_title;
    std::map<std::string, FieldValue> m_fields;
    std::vector<std::string> m_tags;
};

// ---------------------------------------------------------------------
// Registre des schémas. Contient les six types du cadrage, et accepte
// l'ajout de types supplémentaires (c'est le point d'extension).
// ---------------------------------------------------------------------
class SchemaRegistry {
public:
    SchemaRegistry(); // enregistre les schémas intégrés

    // Ajoute un schéma, ou remplace celui de même typeId (permet aussi de
    // personnaliser un type intégré).
    void registerSchema(CardSchema schema);

    const CardSchema* schema(const std::string& typeId) const;
    std::vector<const CardSchema*> all() const; // triés par libellé
    bool contains(const std::string& typeId) const;

private:
    std::map<std::string, CardSchema> m_schemas;
};

// Les schémas intégrés, correspondant aux types listés au cadrage.
std::vector<CardSchema> builtinSchemas();

// ---------------------------------------------------------------------
// Collection de fiches. Les identifiants sont uniques ; les références
// entre fiches se font par identifiant.
// ---------------------------------------------------------------------
class CardLibrary {
public:
    // Ajoute ou remplace la fiche de même identifiant. Retourne une
    // référence stable vers la fiche stockée.
    Card& put(Card card);

    // Supprime une fiche. Si 'purgeReferences', retire aussi son
    // identifiant des champs de référence des autres fiches (pas de lien
    // mort). Retourne false si l'identifiant n'existait pas.
    bool remove(const std::string& id, bool purgeReferences = true,
                const SchemaRegistry* registry = nullptr);

    const Card* find(const std::string& id) const;
    Card* find(const std::string& id);

    size_t size() const { return m_cards.size(); }
    const std::map<std::string, Card>& cards() const { return m_cards; }

    // Listes de consultation, triées par titre (ordre d'affichage).
    std::vector<const Card*> byType(const std::string& typeId) const;
    std::vector<const Card*> byTag(const std::string& tag) const;

    // Recherche dans les titres, insensible à la casse et aux majuscules
    // accentuées (même repli que la recherche dans le texte).
    std::vector<const Card*> searchByTitle(const std::string& query) const;

    // Tous les tags utilisés, uniques et triés.
    std::vector<std::string> allTags() const;

private:
    std::map<std::string, Card> m_cards; // références stables (conteneur à noeuds)
};

} // namespace core
