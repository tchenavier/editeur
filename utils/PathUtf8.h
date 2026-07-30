// PathUtf8.h
// -----------------------------------------------------------------------
// Conversion sûre entre chemins de fichiers et chaînes UTF-8.
//
// POURQUOI CE MODULE (et pas les fonctions standard directes) :
//
//  1. std::filesystem::path(std::string) interprète les octets dans
//     l'encodage NATIF ÉTROIT. Sur Windows, c'est la page de codes ANSI,
//     PAS l'UTF-8 : un chemin contenant des accents (« C:\Écrits\roman.md »)
//     serait silencieusement corrompu. Même problème en sens inverse avec
//     path::string().
//
//  2. std::filesystem::u8path existait précisément pour lever cette
//     ambiguïté, mais il est DÉPRÉCIÉ depuis C++20.
//
//  3. Son remplaçant officiel — construire un path depuis un std::u8string —
//     exige le type char8_t, qui n'existe pas en C++17 (notre standard).
//
// La solution retenue ne dépend d'aucune des trois : on convertit
// explicitement en UTF-16 sur Windows (encodage natif des chemins), et on
// construit directement ailleurs (les chemins POSIX sont des octets, déjà
// en UTF-8). Aucun appel déprécié, aucune dépendance externe, et le
// comportement est correct sur toutes les plateformes.
//
// Tout le projet échange les chemins en UTF-8 (comme le pont) : ces deux
// fonctions sont le seul endroit où la conversion a lieu.
// -----------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <string>

namespace utils {

// Construit un chemin à partir d'une chaîne UTF-8 (venue du JS, d'un
// fichier de configuration...).
std::filesystem::path pathFromUtf8(const std::string& utf8);

// Rend un chemin sous forme de chaîne UTF-8 (pour le renvoyer au JS ou
// l'insérer dans un message d'erreur affiché à l'utilisateur).
std::string pathToUtf8(const std::filesystem::path& path);

// Jeton représentant la DATE DE DERNIÈRE MODIFICATION d'un fichier.
// Sert à détecter qu'un fichier a changé en dehors de l'éditeur (autre
// fenêtre, éditeur externe) avant de l'écraser. Chaîne vide si le fichier
// n'existe pas ou est inaccessible. Deux jetons se comparent par égalité :
// s'ils diffèrent, le fichier a changé depuis qu'on l'a lu.
std::string fileModToken(const std::filesystem::path& path);

} // namespace utils
