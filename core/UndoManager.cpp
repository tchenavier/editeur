// UndoManager.cpp
// Implémentation de l'historique d'annulation. Voir UndoManager.h pour
// les principes (instantanés de bloc, regroupement par mot).

#include "UndoManager.h"

namespace core {

UndoManager::UndoManager(size_t maxSteps)
    : m_maxSteps(maxSteps == 0 ? 1 : maxSteps) {} // 0 n'aurait aucun sens

void UndoManager::push(Step step) {
    m_undoSteps.push_back(std::move(step));
    if (m_undoSteps.size() > m_maxSteps) {
        // On oublie la plus ancienne. O(n) sur un vecteur, mais n est
        // borné par m_maxSteps (200 par défaut) : négligeable.
        m_undoSteps.erase(m_undoSteps.begin());
    }
}

void UndoManager::breakGroup() {
    if (!m_undoSteps.empty()) {
        m_undoSteps.back().open = false;
    }
}

void UndoManager::recordTyping(size_t blockIndex, size_t pos,
                               const std::u32string& inserted,
                               TextBlock before, const TextBlock& after) {
    if (inserted.empty()) return; // insertion vide : rien à annuler
    m_redoSteps.clear();

    const bool single = (inserted.size() == 1);
    const bool separator = single && WordBoundary::isWordSeparator(inserted[0]);

    // Fusion avec l'étape précédente ? Uniquement si :
    //   - elle est encore ouverte (pas de breakGroup entre temps),
    //   - même bloc, frappe exactement à la suite de la précédente,
    //   - même catégorie de caractères (on reste dans le même mot, ou
    //     dans la même suite de séparateurs).
    if (single && !m_undoSteps.empty()) {
        Step& last = m_undoSteps.back();
        if (last.open && last.group == GroupKind::Insert &&
            last.blockIndex == blockIndex && pos == last.nextPos &&
            separator == last.separators) {
            // On étend l'étape : le "avant" ne bouge pas, seul le
            // "après" avance avec la frappe.
            last.after = after;
            last.nextPos = pos + 1;
            return;
        }
    }

    breakGroup(); // la précédente ne pourra plus fusionner
    Step step;
    step.blockIndex = blockIndex;
    step.before = std::move(before);
    step.after = after;
    step.group = GroupKind::Insert;
    step.separators = separator;
    step.nextPos = pos + inserted.size();
    step.open = single; // un collage reste une étape isolée
    push(std::move(step));
}

void UndoManager::recordDeletion(size_t blockIndex, size_t pos,
                                 const std::u32string& removed,
                                 TextBlock before, const TextBlock& after) {
    if (removed.empty()) return;
    m_redoSteps.clear();

    const bool single = (removed.size() == 1);
    const bool separator = single && WordBoundary::isWordSeparator(removed[0]);

    if (single && !m_undoSteps.empty()) {
        Step& last = m_undoSteps.back();
        // Deux sens de suppression contiguë :
        //   - retour arrière : la suppression suivante est juste avant
        //     la précédente (pos == nextPos - 1) ;
        //   - Suppr : le texte glisse vers la gauche, la position ne
        //     bouge pas (pos == nextPos).
        const bool backspace = (last.nextPos > 0 && pos == last.nextPos - 1);
        const bool forward = (pos == last.nextPos);
        if (last.open && last.group == GroupKind::Delete &&
            last.blockIndex == blockIndex && (backspace || forward) &&
            separator == last.separators) {
            last.after = after;
            last.nextPos = pos;
            return;
        }
    }

    breakGroup();
    Step step;
    step.blockIndex = blockIndex;
    step.before = std::move(before);
    step.after = after;
    step.group = GroupKind::Delete;
    step.separators = separator;
    step.nextPos = pos;
    step.open = single;
    push(std::move(step));
}

void UndoManager::recordOther(size_t blockIndex, TextBlock before,
                              const TextBlock& after) {
    m_redoSteps.clear();
    breakGroup();
    Step step;
    step.blockIndex = blockIndex;
    step.before = std::move(before);
    step.after = after;
    step.group = GroupKind::None;
    step.open = false; // jamais fusionnée
    push(std::move(step));
}

void UndoManager::recordStructural(std::vector<TextBlock> before,
                                   const std::vector<TextBlock>& after) {
    m_redoSteps.clear();
    breakGroup();
    Step step;
    step.wholeDocument = true;
    step.docBefore = std::move(before);
    step.docAfter = after;
    step.open = false; // jamais fusionnée
    push(std::move(step));
}

std::optional<ChangeInfo> UndoManager::undo(Document& doc) {
    if (m_undoSteps.empty()) return std::nullopt;

    // Étape structurelle : on restaure l'instantané complet.
    if (m_undoSteps.back().wholeDocument) {
        Step step = std::move(m_undoSteps.back());
        m_undoSteps.pop_back();
        doc.blocks() = step.docBefore;
        m_redoSteps.push_back(std::move(step));
        ChangeInfo info;
        info.wholeDocument = true;
        return info;
    }

    // Sécurité : si le bloc visé n'existe plus (ne devrait pas arriver
    // tant que les étapes ne portent que sur des blocs existants), on
    // n'applique rien plutôt que de risquer une écriture hors limites.
    if (m_undoSteps.back().blockIndex >= doc.blocks().size()) {
        return std::nullopt;
    }

    Step step = std::move(m_undoSteps.back());
    m_undoSteps.pop_back();
    step.open = false; // après un undo, plus aucune fusion possible

    doc.blocks()[step.blockIndex] = step.before;
    ChangeInfo info;
    info.blockIndex = step.blockIndex;
    m_redoSteps.push_back(std::move(step));
    return info;
}

std::optional<ChangeInfo> UndoManager::redo(Document& doc) {
    if (m_redoSteps.empty()) return std::nullopt;

    if (m_redoSteps.back().wholeDocument) {
        Step step = std::move(m_redoSteps.back());
        m_redoSteps.pop_back();
        doc.blocks() = step.docAfter;
        m_undoSteps.push_back(std::move(step));
        ChangeInfo info;
        info.wholeDocument = true;
        return info;
    }

    if (m_redoSteps.back().blockIndex >= doc.blocks().size()) {
        return std::nullopt;
    }

    Step step = std::move(m_redoSteps.back());
    m_redoSteps.pop_back();

    doc.blocks()[step.blockIndex] = step.after;
    ChangeInfo info;
    info.blockIndex = step.blockIndex;
    m_undoSteps.push_back(std::move(step));
    // Pas de push() ici : l'étape revient d'où elle vient, la limite
    // d'historique ne peut pas être dépassée par un aller-retour.
    return info;
}

void UndoManager::clear() {
    m_undoSteps.clear();
    m_redoSteps.clear();
}

} // namespace core
