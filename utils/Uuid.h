// Uuid.h
// -----------------------------------------------------------------------
// Identifiant unique de projet (Phase 5, jalon A).
//
// POURQUOI UN IDENTIFIANT : deux romans peuvent porter le même titre, un
// fichier peut être renommé, déplacé, copié. Pour qu'une synchronisation
// sache un jour « ce projet-ci est le même que celui du serveur », il faut
// un repère stable qui ne dépende ni du nom du fichier ni de son chemin.
// Il est écrit dans l'en-tête du manuscrit : il voyage donc avec le
// projet, y compris si l'auteur déplace son dossier.
//
// Ce n'est PAS un secret : il identifie, il n'autorise rien. L'accès à un
// projet distant dépendra du compte et des permissions, jamais de la
// connaissance de cet identifiant.
//
// Différence avec l'identifiant de livre de l'EPUB (io/Epub) : celui-là
// est DÉTERMINISTE, dérivé du titre et de l'auteur, pour qu'une liseuse
// reconnaisse une nouvelle version du même livre. Celui-ci est ALÉATOIRE :
// deux projets distincts ne doivent jamais entrer en collision, même
// créés la même seconde avec le même titre.
// -----------------------------------------------------------------------

#pragma once

#include <string>

namespace utils {

// Nouvel identifiant aléatoire, au format « 8-4-4-4-12 » en minuscules
// (version 4). Deux appels ne renvoient jamais la même valeur.
std::string newUuid();

// Vrai si la chaîne a exactement la forme attendue. Sert à valider ce qui
// vient d'un fichier édité à la main : un identifiant abîmé doit être
// détecté, pas propagé.
bool isValidUuid(const std::string& value);

} // namespace utils
