// Reflow.h
// -----------------------------------------------------------------------
// Rendre ses paragraphes à un texte qui les a perdus.
//
// LE PROBLÈME, tel qu'il se pose vraiment : on envoie son manuscrit à un
// correcteur extérieur, et il le rend d'un seul tenant — tout le texte
// dans un unique paragraphe, les titres fondus dans la masse. Le contenu
// est bon, la mise en forme est morte. Recouper à la main un roman entier
// n'est pas une option.
//
// LA SOLUTION N'EST PAS UNE DEVINETTE. On dispose de la version d'avant :
// elle porte exactement la structure voulue. Deux versions d'un même texte
// ne diffèrent que par les corrections : leur suite de MOTS est donc
// presque identique, et c'est elle qu'on aligne. Chaque mot du texte
// aplati retrouve ainsi le paragraphe d'où il vient, et il n'y a plus qu'à
// regrouper. Le découpage obtenu est celui de l'auteur, pas celui d'une
// heuristique, et les titres retrouvent leur niveau.
//
// POURQUOI LES MOTS ET NON LES PHRASES : dans un texte aplati, un titre
// se retrouve collé à la phrase qui le suit, sans ponctuation entre les
// deux — « Acte I Le vent hurlait. ». Aucun découpage en phrases ne peut
// rendre cette frontière ; l'alignement des mots, si.
//
// Ce qui est conservé : le texte corrigé mot pour mot, ses styles, la
// ponctuation de fin de paragraphe et celle qui ouvre le suivant (tiret de
// dialogue, guillemet).
// Ce qui ne l'est pas : les paragraphes vides de la version aplatie (ils
// n'ont plus de mot à porter). L'opération passe par l'historique : elle
// s'annule d'un Ctrl+Z.
//
// Module de PUR CALCUL : aucune entrée/sortie, aucune dépendance à
// l'interface. Il se teste sur des documents construits à la main.
// -----------------------------------------------------------------------

#pragma once

#include "TextModel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace core {

// Un fragment repéré dans un document : le bloc d'où il vient, et sa place
// dans ce bloc (en POINTS DE CODE, comme partout ailleurs).
struct TextRef {
    size_t blockIndex = 0;
    size_t start = 0;
    size_t length = 0;
};

// Toutes les phrases d'un document, dans l'ordre de lecture. Un bloc vide
// n'en produit aucune ; un titre en produit une.
std::vector<TextRef> documentSentences(const std::vector<TextBlock>& blocks);

// Tous les mots d'un document, dans l'ordre de lecture.
std::vector<TextRef> documentWords(const std::vector<TextBlock>& blocks);

// Le texte d'un fragment. Sans effet de bord : sert aux empreintes et aux
// tests.
std::u32string refText(const std::vector<TextBlock>& blocks, const TextRef& ref);

// Mesure de PLATITUDE : le nombre de phrases que porte le bloc le plus
// chargé, rapporté au nombre total de phrases. Vaut 1 quand tout le texte
// tient dans un seul bloc, et tombe vers 0 sur un manuscrit découpé. Vaut
// 0 tant qu'aucun bloc n'atteint trois phrases : en dessous, la mesure ne
// dirait rien (un texte de deux phrases tient dans un bloc sans avoir rien
// perdu).
//
// C'est la mesure qui permet de dire « cette version a perdu ses
// paragraphes » sans avoir à le demander à l'utilisateur.
double flatness(const std::vector<TextBlock>& blocks);

// Recoupe 'flat' en suivant la structure de 'reference'. Retourne les
// nouveaux blocs, dont le TYPE (paragraphe, titre H1..H6) est repris du
// bloc de référence correspondant.
//
// Retourne 'flat' INCHANGÉ si l'un des deux est vide, ou si moins de la
// moitié des mots se raccroche à la référence : mieux vaut ne rien faire
// qu'un découpage inventé. Une version simplement corrigée en raccroche
// plus de neuf sur dix — bien loin du seuil.
std::vector<TextBlock> reflowLike(const std::vector<TextBlock>& flat,
                                  const std::vector<TextBlock>& reference);

} // namespace core
