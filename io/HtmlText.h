// HtmlText.h
// -----------------------------------------------------------------------
// Rendu (X)HTML du texte, partagé par les exports de la Phase 4 (EPUB
// aujourd'hui, impression PDF côté UI, et tout futur export HTML).
//
// Placé dans io/ car c'est de la SÉRIALISATION (comme MarkdownIO) : on
// transforme le modèle en un format de fichier. Aucune dépendance à
// l'interface, donc testable seul.
//
// Différence avec MarkdownIO::renderInline : ici la sortie doit être du
// XHTML VALIDE (exigence EPUB) — les balises sont donc toujours bien
// imbriquées (ouvertes puis fermées dans l'ordre inverse), et tout le
// texte de l'auteur est échappé.
// -----------------------------------------------------------------------

#pragma once

#include <string>

namespace core {
class TextBlock;
}

namespace io {

// Échappe un texte UTF-8 pour insertion dans du contenu (X)HTML :
// &, <, > et " deviennent des entités. Renvoie de l'UTF-8.
std::string htmlEscape(const std::string& utf8);

// Rend le contenu EN LIGNE d'un bloc en XHTML bien formé : <strong>,
// <em>, <u>, <s>, <sup>, <sub> et <span style="font-size:Xem"> pour la
// taille des caractères. La balise de bloc (<p>, <h1>...) est ajoutée par
// l'appelant. Le texte est échappé ; les balises sont toujours refermées
// dans l'ordre inverse de leur ouverture (validité XHTML).
std::string renderBlockHtml(const core::TextBlock& block);

} // namespace io
