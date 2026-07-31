// test_diff.cpp
// Tests de la comparaison de deux versions d'un texte (core/Diff).
//
// La propriété qui compte, et que la plupart des tests vérifient sous un
// angle ou un autre : une modification LOCALE ne doit produire qu'une
// différence LOCALE. Insérer un mot en tête de paragraphe décale tout le
// reste ; si l'alignement est bon, le reste n'apparaît pas comme modifié.

#include "Diff.h"
#include "Utf8.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::u32string u32(const char* utf8) { return utils::utf8ToU32(utf8); }

// Rend les plages sous forme lisible, pour des assertions parlantes :
// « très|beau » plutôt que des couples de positions.
std::string extract(const std::u32string& text, const std::vector<core::WordSpan>& spans) {
    std::string out;
    for (const core::WordSpan& span : spans) {
        if (!out.empty()) out += "|";
        out += utils::u32ToUtf8(text.substr(span.start, span.length));
    }
    return out;
}

std::vector<uint64_t> hashWords(const std::u32string& text) {
    std::vector<uint64_t> hashes;
    for (const core::WordSpan& span : core::tokenizeWords(text)) {
        hashes.push_back(core::wordHash(text, span));
    }
    return hashes;
}

size_t countKind(const std::vector<core::DiffOp>& ops, core::DiffKind kind) {
    size_t n = 0;
    for (const core::DiffOp& op : ops) {
        if (op.kind == kind) ++n;
    }
    return n;
}

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : découpage en mots ---
    {
        const std::u32string texte = u32("Élise, très émue, partit.");
        const auto mots = core::tokenizeWords(texte);
        assert(extract(texte, mots) == "Élise|très|émue|partit");
        // Positions en POINTS DE CODE : « très » commence au 7e caractère,
        // pas au 8e octet.
        assert(mots[1].start == 7);
        assert(mots[1].length == 4);

        assert(core::tokenizeWords(u32("")).empty());
        assert(core::tokenizeWords(u32("   ...   ")).empty());
        // L'apostrophe sépare, comme dans le correcteur.
        assert(extract(u32("l'arbre"), core::tokenizeWords(u32("l'arbre"))) == "l|arbre");
        std::cout << "Test 1 OK (decoupage en mots)\n";
    }

    // --- Test 2 : normalisation — ce qui compte, ce qui ne compte pas ---
    {
        // Ne DOIT PAS compter : espace insécable, apostrophe courbe.
        assert(core::blockHash(u32("un texte")) == core::blockHash(u32("un texte")));
        assert(core::blockHash(u32("l'un")) == core::blockHash(u32("l’un")));
        // Espaces multiples : sans conséquence dans un bloc.
        assert(core::blockHash(u32("un  texte")) == core::blockHash(u32("un texte")));

        // DOIT compter : la casse et les accents. Pour qui relit une
        // version corrigée, ce sont exactement les différences à voir.
        assert(core::blockHash(u32("Le chat")) != core::blockHash(u32("le chat")));
        assert(core::blockHash(u32("élevé")) != core::blockHash(u32("eleve")));
        std::cout << "Test 2 OK (normalisation : casse et accents preserves)\n";
    }

    // --- Test 3 : alignement de séquences ---
    {
        const std::vector<uint64_t> a = {1, 2, 3, 4, 5};

        // Identiques : une seule opération, aucune différence.
        auto ops = core::diffSequences(a, a);
        assert(ops.size() == 1 && ops[0].kind == core::DiffKind::Equal);
        assert(ops[0].oldCount == 5);

        // Insertion au MILIEU : le reste ne bouge pas.
        ops = core::diffSequences(a, {1, 2, 9, 3, 4, 5});
        assert(countKind(ops, core::DiffKind::Inserted) == 1);
        assert(countKind(ops, core::DiffKind::Deleted) == 0);

        // Insertion en TÊTE : c'est le cas qui piège une comparaison
        // position par position — tout se décale, et pourtant une seule
        // différence doit ressortir.
        ops = core::diffSequences(a, {9, 1, 2, 3, 4, 5});
        assert(countKind(ops, core::DiffKind::Inserted) == 1);
        assert(countKind(ops, core::DiffKind::Deleted) == 0);
        for (const auto& op : ops) {
            if (op.kind == core::DiffKind::Inserted) assert(op.newCount == 1);
        }

        // Suppression, remplacement, vide des deux côtés.
        ops = core::diffSequences(a, {1, 2, 4, 5});
        assert(countKind(ops, core::DiffKind::Deleted) == 1);
        ops = core::diffSequences(a, {1, 2, 99, 4, 5});
        assert(countKind(ops, core::DiffKind::Deleted) == 1);
        assert(countKind(ops, core::DiffKind::Inserted) == 1);
        assert(core::diffSequences({}, {}).empty());
        assert(countKind(core::diffSequences({}, a), core::DiffKind::Inserted) == 1);
        assert(countKind(core::diffSequences(a, {}), core::DiffKind::Deleted) == 1);
        std::cout << "Test 3 OK (alignement de sequences)\n";
    }

    // --- Test 4 : LE cas d'usage — un mot ajouté ne décale rien ---
    {
        const std::u32string avant = u32("Le chat dormait sur le vieux tapis rouge.");
        const std::u32string apres = u32("Le gros chat dormait sur le vieux tapis rouge.");

        const auto changes = core::diffText(avant, apres);
        assert(changes.removed.empty());              // rien n'a été retiré
        assert(extract(apres, changes.added) == "gros"); // et un seul mot ajouté
        std::cout << "Test 4 OK (un mot ajoute ne decale rien)\n";
    }

    // --- Test 5 : mot remplacé, mots consécutifs regroupés ---
    {
        const std::u32string avant = u32("Il faisait nuit noire sur la vallée.");
        const std::u32string apres = u32("Il faisait nuit claire sur la vallée.");
        auto changes = core::diffText(avant, apres);
        assert(extract(avant, changes.removed) == "noire");
        assert(extract(apres, changes.added) == "claire");

        // Plusieurs mots consécutifs : une seule plage, pas trois.
        const std::u32string a2 = u32("Il partit sans un mot vers la forêt.");
        const std::u32string b2 = u32("Il partit vers la forêt.");
        changes = core::diffText(a2, b2);
        assert(changes.removed.size() == 1);
        assert(extract(a2, changes.removed) == "sans un mot");
        assert(changes.added.empty());
        std::cout << "Test 5 OK (remplacement, regroupement des plages)\n";
    }

    // --- Test 6 : ce que la comparaison doit ignorer, et ne pas ignorer ---
    {
        // Le style n'est pas dans le texte : rien à ignorer, c'est acquis.
        // En revanche l'espace insécable et l'apostrophe courbe, si.
        auto changes = core::diffText(u32("il dit : bonjour"), u32("il dit : bonjour"));
        assert(changes.removed.empty() && changes.added.empty());
        changes = core::diffText(u32("l'ami d'enfance"), u32("l’ami d’enfance"));
        assert(changes.removed.empty() && changes.added.empty());

        // La casse et les accents restent des différences.
        changes = core::diffText(u32("le chat"), u32("Le chat"));
        assert(changes.removed.size() == 1 && changes.added.size() == 1);
        changes = core::diffText(u32("il eleve"), u32("il élevé"));
        assert(changes.added.size() == 1);
        std::cout << "Test 6 OK (variantes invisibles ignorees, corrections vues)\n";
    }

    // --- Test 7 : alignement de blocs (paragraphes) ---
    {
        // Un paragraphe inséré ne doit pas faire passer les suivants pour
        // modifiés : c'est la même propriété qu'au test 4, un étage plus haut.
        const std::vector<std::u32string> avant = {
            u32("# Chapitre premier"), u32("Il faisait nuit."),
            u32("Élise se leva."), u32("Elle sortit."),
        };
        const std::vector<std::u32string> apres = {
            u32("# Chapitre premier"), u32("Il faisait nuit."),
            u32("Un chien aboya au loin."), // inséré
            u32("Élise se leva."), u32("Elle sortit."),
        };
        std::vector<uint64_t> ha, hb;
        for (const auto& t : avant) ha.push_back(core::blockHash(t));
        for (const auto& t : apres) hb.push_back(core::blockHash(t));

        const auto ops = core::diffSequences(ha, hb);
        assert(countKind(ops, core::DiffKind::Inserted) == 1);
        assert(countKind(ops, core::DiffKind::Deleted) == 0);
        size_t equalBlocks = 0;
        for (const auto& op : ops) {
            if (op.kind == core::DiffKind::Equal) equalBlocks += op.oldCount;
        }
        assert(equalBlocks == 4); // les quatre paragraphes d'origine, intacts
        std::cout << "Test 7 OK (alignement des paragraphes)\n";
    }

    // --- Test 8 : cas dégénérés, aucun plantage ---
    {
        assert(core::diffText(u32(""), u32("")).added.empty());
        auto changes = core::diffText(u32(""), u32("un texte neuf"));
        assert(changes.removed.empty() && changes.added.size() == 1);
        changes = core::diffText(u32("un texte perdu"), u32(""));
        assert(changes.added.empty() && changes.removed.size() == 1);

        // Deux textes longs et entièrement différents : le résultat doit
        // arriver, sans faire enfler le calcul.
        std::u32string longA, longB;
        for (int i = 0; i < 3000; ++i) {
            longA += u32("alpha beta gamma ");
            longB += u32("delta epsilon zeta ");
        }
        changes = core::diffText(longA, longB);
        assert(!changes.removed.empty() && !changes.added.empty());

        // Répétitions à l'identique : aucun mot n'est unique, donc aucune
        // ancre — c'est le chemin de repli qui est exercé ici.
        changes = core::diffText(u32("la la la la la"), u32("la la la"));
        assert(changes.added.empty());
        assert(!changes.removed.empty());
        std::cout << "Test 8 OK (cas degeneres et textes longs)\n";
    }

    std::cout << "Tous les tests de comparaison sont passes.\n";
    return 0;
}
