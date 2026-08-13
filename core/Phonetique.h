// Phonetique.h
// -----------------------------------------------------------------------
// La clé sonore d'un mot français : deux mots qui se prononcent pareil
// reçoivent la même clé.
//
// À QUOI ÇA SERT, et pourquoi la distance d'édition ne suffit pas. Le
// correcteur propose aujourd'hui les mots à UNE opération d'écart. C'est
// le bon réglage pour une faute de frappe — mais pas du tout pour la faute
// la plus courante en français, qui consiste à écrire un mot comme on
// l'entend :
//
//     « fesais »     -> « faisais »      deux opérations d'écart
//     « ortografe »  -> « orthographe »  trois
//     « cerize »     -> « cerise »       une, mais noyée parmi cent autres
//
// Élargir la distance d'édition à deux ou trois ferait exploser le nombre
// de candidats sans les classer : des milliers de mots sont à trois
// opérations de n'importe quoi. Le son, lui, est un filtre : « fesais » et
// « faisais » ont exactement la même clé, et presque rien d'autre ne l'a.
//
// CE N'EST PAS UNE TRANSCRIPTION PHONÉTIQUE. C'est une normalisation
// grossière, volontairement sourde aux différences que l'orthographe
// française n'entend pas : lettres muettes, consonnes doubles, accents,
// « ph » contre « f », « au » contre « o ». Elle se trompe parfois — elle
// rapproche des mots qui ne riment pas tout à fait — et ce n'est pas
// grave : elle ne décide de rien, elle propose.
//
// PORTAGE. Ce module reprend `src/phonetique.c` du projet « ortho » du
// même auteur : mêmes groupes, mêmes règles, même choix de noter les
// voyelles nasales par des chiffres. Deux différences, imposées par ce
// projet-ci : le texte est en POINTS DE CODE (jamais en octets), et rien
// n'est écrit dans un tampon de taille fixe.
// -----------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>

namespace core {

// La clé sonore. Chaîne vide si le mot ne contient aucune lettre.
//
// Le résultat est en ASCII : les sons sont notés par des lettres simples,
// et les voyelles NASALES par des chiffres — 1 pour « in », 2 pour
// « on », 3 pour « an », 4 pour « un ». Un chiffre ne peut pas entrer en
// collision avec une consonne, ce qui évite de confondre « pain » et
// « pan ».
std::string phoneticKey(const std::u32string& word);

// La même, à partir d'un mot en UTF-8.
std::string phoneticKey(const std::string& word);

// Empreinte 64 bits de la clé. C'est elle, et non la clé, que le
// correcteur range pour ses 454 000 formes : douze octets par mot au lieu
// d'une chaîne, soit quelques mégaoctets au lieu de quelques dizaines.
// Les collisions sont possibles et sans conséquence — les candidats sont
// vérifiés en recalculant leur vraie clé.
uint64_t phoneticHash(const std::string& key);

} // namespace core
