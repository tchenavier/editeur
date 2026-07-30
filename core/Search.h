// Search.h
// -----------------------------------------------------------------------
// Recherche et remplacement dans le document (cf. cadrage : « recherche et
// remplacement des mots avec choix de la portée : chapitre courant ou
// manuscrit entier », raccourci Ctrl+F).
//
// Logique métier pure, sans dépendance à l'interface : testable seule.
// La PORTÉE est exprimée comme une plage de blocs [rangeStart, rangeEnd) ;
// c'est la couche UI qui traduit « chapitre courant » / « manuscrit
// entier » en une plage (elle connaît déjà le découpage en chapitres).
//
// Toutes les positions sont en indices de CARACTÈRE (points de code),
// comme le reste du modèle.
// -----------------------------------------------------------------------

#pragma once

#include "TextModel.h"

#include <cstddef>
#include <string>

namespace core {

// Repli de casse (ASCII + Latin-1, donc les majuscules accentuées du
// français : É, À, Ç, Ô...). Exposé ici car il sert à la fois à la
// recherche dans le texte et à la recherche dans les fiches (CardModel) :
// une seule définition, un seul comportement.
char32_t foldLower(char32_t c);

struct SearchOptions {
    bool caseSensitive = false; // sinon : insensible à la casse (ASCII + Latin-1)
    bool wholeWord = false;     // n'accepte que des mots entiers
};

struct SearchHit {
    bool found = false;
    size_t block = 0;
    size_t start = 0;  // début (points de code) dans le bloc
    size_t end = 0;    // fin exclusive
    size_t index = 0;  // rang 1-based de cette occurrence dans la portée (0 si aucune)
    size_t total = 0;  // nombre total d'occurrences dans la portée
};

// Cherche l'occurrence suivante (ou précédente si 'backward') de 'query'
// à partir de (fromBlock, fromPos), dans les blocs [rangeStart, rangeEnd).
// Si 'wrap', la recherche fait le tour une fois. Renseigne toujours
// 'total' (et 'index' si une occurrence est trouvée), pour un affichage
// du type « 3 / 12 ». Une requête vide ne trouve rien.
SearchHit findNext(const Document& doc, const std::u32string& query,
                   size_t rangeStart, size_t rangeEnd,
                   size_t fromBlock, size_t fromPos,
                   const SearchOptions& opts, bool backward, bool wrap);

// Compte les occurrences dans la portée, sans rien modifier.
size_t countMatches(const Document& doc, const std::u32string& query,
                    size_t rangeStart, size_t rangeEnd,
                    const SearchOptions& opts);

// Remplace toutes les occurrences de 'query' par 'replacement' dans la
// portée. Modifie 'doc' (via l'API publique de TextBlock, donc les plages
// de style restent cohérentes). Retourne le nombre de remplacements.
size_t replaceAll(Document& doc, const std::u32string& query,
                  const std::u32string& replacement,
                  size_t rangeStart, size_t rangeEnd,
                  const SearchOptions& opts);

// --- Recherche sur une simple chaîne ---------------------------------
// Mêmes règles de correspondance (casse, mots entiers) que ci-dessus,
// mais sur une chaîne quelconque : utilisé pour chercher/remplacer dans
// les CHAMPS DE FICHES, avec exactement le même comportement que dans le
// texte (une seule sémantique de recherche pour tout le projet).

// Vrai si 'query' (non vide) apparaît dans 'text'.
bool contains(const std::u32string& text, const std::u32string& query,
              const SearchOptions& opts);

// Remplace toutes les occurrences dans 'text' et retourne le résultat.
// *count (si non nul) reçoit le nombre de remplacements effectués.
std::u32string replaceInString(const std::u32string& text,
                               const std::u32string& query,
                               const std::u32string& replacement,
                               const SearchOptions& opts, size_t* count = nullptr);

} // namespace core
