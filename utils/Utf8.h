// Utf8.h
// -----------------------------------------------------------------------
// Conversion UTF-8 <-> UTF-32, partagée par toutes les couches.
//
// Historiquement dans le pont (EditorApi), déplacée ici (utils/) car la
// couche io/ (sérialisation Markdown) en a également besoin, et io/ ne
// doit pas dépendre du pont.
//
// Implémentation manuelle volontaire : std::codecvt est déprécié depuis
// C++17, et on ne veut pas de dépendance externe (ICU...) juste pour ça.
// L'algorithme UTF-8 est stable (RFC 3629) et tient en quelques lignes.
//
// Robustesse : les séquences invalides sont remplacées par U+FFFD plutôt
// que de provoquer un plantage (données d'entrée jamais supposées saines).
// -----------------------------------------------------------------------

#pragma once

#include <string>

namespace utils {

std::u32string utf8ToU32(const std::string& utf8);
std::string u32ToUtf8(const std::u32string& u32);

} // namespace utils
