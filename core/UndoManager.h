// UndoManager.h
// -----------------------------------------------------------------------
// Historique d'annulation/rétablissement maison (cf. cadrage : pas de
// dépendance à execCommand côté navigateur, qui est déprécié ; l'undo
// doit rester stable dans le temps et appartenir au C++).
//
// Principe : chaque étape mémorise une copie du bloc AVANT et APRÈS la
// modification (des "instantanés"). Annuler = reposer la copie "avant",
// rétablir = reposer la copie "après". C'est volontairement simple et
// déterministe :
//   - un bloc est un paragraphe, pas un chapitre entier : la copie est
//     peu coûteuse ;
//   - aucun calcul d'inversion d'opération (source classique de bugs
//     subtils, notamment pour restaurer les styles d'un texte supprimé) ;
//   - la stabilité prime sur la performance (priorité transversale du
//     projet).
//
// Regroupement "par mot" : la frappe au clavier est fusionnée dans une
// même étape tant qu'elle reste contiguë et dans la même catégorie de
// caractères (lettres OU séparateurs, cf. WordBoundary). Ctrl+Z annule
// donc le dernier mot tapé, ni un caractère isolé, ni tout le paragraphe.
// Les suppressions au clavier (retour arrière / Suppr) sont regroupées
// de la même façon. Un déplacement du curseur doit clore le groupe en
// cours : la couche UI appelle breakGroup() (commande
// editor.breakUndoGroup côté pont).
//
// La granularité "par mot" reste une affaire d'ergonomie : le modèle,
// lui, n'est jamais limité (cf. note en tête de TextModel.h).
// -----------------------------------------------------------------------

#pragma once

#include "TextModel.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace core {

// Décrit ce qu'une annulation/rétablissement a modifié, pour que la
// couche UI sache quoi re-rendre :
//   - wholeDocument == true  : la structure a changé (bloc coupé,
//     fusionné...), tout le document doit être re-rendu ;
//   - sinon : seul 'blockIndex' a changé, un seul bloc à re-rendre.
struct ChangeInfo {
    bool wholeDocument = false;
    size_t blockIndex = 0;
};

class UndoManager {
public:
    // 'maxSteps' borne la mémoire consommée par l'historique : au-delà,
    // les étapes les plus anciennes sont oubliées (comportement standard
    // des éditeurs). 200 étapes ≈ 200 derniers mots/actions : largement
    // suffisant en pratique.
    explicit UndoManager(size_t maxSteps = 200);

    // --- Enregistrement (appelé par la couche API, jamais par le modèle) --
    // Convention commune aux trois méthodes :
    //   - 'before' : copie du bloc AVANT la modification (déplacée ici) ;
    //   - 'after'  : le bloc APRÈS la modification (copié en interne) ;
    //   - 'pos' et le texte inséré/supprimé : la modification EFFECTIVE
    //     (positions déjà bornées), utilisés uniquement pour décider du
    //     regroupement.
    // Tout nouvel enregistrement invalide la pile de rétablissement
    // (comportement standard : après Ctrl+Z puis une frappe, le "redo"
    // n'a plus de sens).

    // Frappe de texte. Seule une insertion d'un caractère unique est
    // fusionnable ; un collage (plusieurs caractères) forme une étape
    // isolée, annulable d'un coup.
    void recordTyping(size_t blockIndex, size_t pos,
                      const std::u32string& inserted,
                      TextBlock before, const TextBlock& after);

    // Suppression au clavier. Fusionne les suppressions contiguës, vers
    // l'arrière (retour arrière) comme vers l'avant (Suppr).
    void recordDeletion(size_t blockIndex, size_t pos,
                        const std::u32string& removed,
                        TextBlock before, const TextBlock& after);

    // Toute autre modification d'un bloc (style, type de bloc...) :
    // étape isolée, jamais fusionnée.
    void recordOther(size_t blockIndex, TextBlock before, const TextBlock& after);

    // Modification STRUCTURELLE (coupe/fusion de blocs) : les indices de
    // blocs changent, on mémorise donc un instantané complet de la liste
    // de blocs avant et après. Plus coûteux en mémoire qu'une étape par
    // bloc, mais ces opérations sont rares (une par Entrée/fusion) et
    // c'est le choix le plus simple et le plus sûr pour rester cohérent
    // (priorité du projet : stabilité avant rapidité). Étape isolée.
    void recordStructural(std::vector<TextBlock> before,
                          const std::vector<TextBlock>& after);

    // Clôt le groupe de frappe en cours (à appeler quand le curseur
    // bouge : clic, flèches...). Sans effet si aucun groupe n'est ouvert.
    void breakGroup();

    bool canUndo() const { return !m_undoSteps.empty(); }
    bool canRedo() const { return !m_redoSteps.empty(); }

    // Annule (resp. rétablit) la dernière étape sur 'doc'. Retourne ce
    // qui a changé (cf. ChangeInfo), ou std::nullopt s'il n'y a rien à
    // faire (pile vide) ou si l'étape ne correspond plus au document
    // (bloc disparu : on préfère ignorer que corrompre).
    std::optional<ChangeInfo> undo(Document& doc);
    std::optional<ChangeInfo> redo(Document& doc);

    // Vide tout l'historique (à appeler après un chargement de fichier :
    // l'historique de l'ancien document n'a plus de sens).
    void clear();

private:
    enum class GroupKind { None, Insert, Delete };

    struct Step {
        bool wholeDocument = false; // étape structurelle (instantané complet) ?

        // --- Étape "par bloc" (wholeDocument == false) ---
        size_t blockIndex = 0;
        TextBlock before;        // état du bloc avant l'étape
        TextBlock after;         // état du bloc après l'étape

        // --- Étape structurelle (wholeDocument == true) ---
        std::vector<TextBlock> docBefore;
        std::vector<TextBlock> docAfter;

        GroupKind group = GroupKind::None;
        bool separators = false; // groupe de séparateurs (espaces, ponctuation) ?
        size_t nextPos = 0;      // position attendue de la prochaine frappe fusionnable
        bool open = false;       // encore fusionnable ?
    };

    std::vector<Step> m_undoSteps;
    std::vector<Step> m_redoSteps;
    size_t m_maxSteps;

    // Empile une étape d'annulation en respectant la limite d'historique.
    void push(Step step);
};

} // namespace core
