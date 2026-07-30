// Yaml.h
// -----------------------------------------------------------------------
// Écriture et lecture d'un en-tête YAML « plat » : des clés associées
// chacune à une ou plusieurs valeurs.
//
// Ce n'est PAS une implémentation générale de YAML (le format complet est
// vaste et plein de pièges). On ne gère que le sous-ensemble que nous
// écrivons nous-mêmes, ce qui suffit aux fiches de worldbuilding et
// garde un analyseur court, prévisible et testable :
//
//   titre: "Aria de Val"           <- scalaire (toujours entre guillemets)
//   tags:                          <- liste
//     - "noblesse"
//     - "chapitre-1"
//   description: |                 <- texte multi-ligne (« bloc »)
//     Grande, cheveux sombres.
//     Une cicatrice à la joue.
//
// Une valeur est TOUJOURS représentée comme une liste de chaînes : un
// scalaire est une liste d'un seul élément. C'est exactement le modèle de
// core::FieldValue, donc la conversion se fait sans perte ni cas
// particulier.
//
// L'ORDRE des clés est conservé (à l'écriture comme à la lecture) : les
// fichiers restent stables d'une sauvegarde à l'autre, ce qui rend les
// différences lisibles — même exigence que pour le manuscrit.
//
// Robustesse : la lecture accepte des fichiers écrits à la main. Une
// ligne incomprise est ignorée plutôt que de faire échouer tout le
// fichier ; les valeurs non quotées sont acceptées.
// -----------------------------------------------------------------------

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace io {

class YamlBlock {
public:
    struct Entry {
        std::string key;
        std::vector<std::string> values;
        // Force la forme « liste à tirets » même avec une seule valeur.
        // Utile pour un champ dont le SCHÉMA dit qu'il est une liste : sa
        // forme reste la même quel que soit le nombre de valeurs, donc
        // ajouter un élément ne bouleverse pas la structure du fichier
        // (différences lisibles entre deux sauvegardes).
        bool forceList = false;
    };

    // Ajoute (ou remplace) une clé. Une clé vide est ignorée.
    void setScalar(const std::string& key, const std::string& value);
    void setList(const std::string& key, const std::vector<std::string>& values,
                 bool forceList = false);

    // nullptr si la clé est absente.
    const std::vector<std::string>* get(const std::string& key) const;
    // Valeur scalaire pratique : premier élément, ou chaîne vide.
    std::string scalar(const std::string& key) const;
    bool has(const std::string& key) const;
    bool empty() const { return m_entries.empty(); }

    const std::vector<Entry>& entries() const { return m_entries; }

    // Sérialise le bloc, SANS les délimiteurs « --- » (l'appelant décide
    // du contexte). Se termine par un saut de ligne si non vide.
    std::string toString() const;

    // Analyse des lignes déjà extraites (sans les « --- »).
    static YamlBlock parse(const std::vector<std::string>& lines);

private:
    std::vector<Entry> m_entries; // ordre d'insertion préservé
};

// Découpe un texte en lignes, en acceptant les fins de ligne Windows.
std::vector<std::string> splitLines(const std::string& text);

// Sépare un document « en-tête YAML + corps ». Si le texte commence par
// « --- » et qu'un « --- » fermant existe, renseigne 'header' et 'body' ;
// sinon 'header' reste vide et tout le texte va dans 'body'.
void splitFrontMatter(const std::string& text,
                      std::vector<std::string>& header,
                      std::string& body);

} // namespace io
