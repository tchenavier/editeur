// fuzz_parsers.cpp
// -----------------------------------------------------------------------
// Test de robustesse des analyseurs face à des ENTRÉES NON FIABLES : un
// fichier Markdown/fiche ouvert par l'utilisateur peut être malformé,
// tronqué ou hostile. On bombarde les analyseurs de milliers d'entrées
// aléatoires (graine fixe = reproductible) et on vérifie qu'aucun ne
// plante. Combiné à AddressSanitizer, ce test attrape tout accès hors
// limites déclenché par une entrée pathologique.
//
// Aucune assertion sur le RÉSULTAT : le contrat est seulement « ne jamais
// planter, ne jamais lire hors limites ». Si le programme se termine
// normalement (et qu'ASan ne signale rien), le test réussit.
//
// TROIS générateurs, complémentaires :
//   1. octets quelconques      — entrées franchement absurdes ;
//   2. octets « structurés »   — biaisés vers les caractères que nos
//      analyseurs traitent spécialement (marqueurs Markdown, YAML) ;
//   3. MUTATION d'entrées VALIDES — de loin le plus efficace : un
//      document correct dont on retourne un bit, coupe une ligne ou
//      duplique un morceau franchit les premières validations et va
//      chercher les chemins profonds, ceux qu'un tirage purement
//      aléatoire n'atteint quasiment jamais.
//
// USAGE :  fuzz.exe [itérations] [graine]
//   Sans argument : 40 000 itérations, graine fixe (audit rapide).
//   Campagne longue : voir scripts\fuzz_long.bat. La graine est TOUJOURS
//   affichée au démarrage — c'est elle qui rend un plantage reproductible.
// -----------------------------------------------------------------------

#include "AffixDictionary.h"
#include "Book.h"
#include "CardIO.h"
#include "CardModel.h"
#include "ChangeLog.h"
#include "Diff.h"
#include "Epub.h"
#include "Speller.h"
#include "Thesaurus.h"
#include "MarkdownIO.h"
#include "FakeTransport.h"
#include "ProjectIndex.h"
#include "Reflow.h"
#include "SyncClient.h"
#include "SyncPlan.h"
#include "WinHttpTransport.h"
#include "Sha256.h"
#include "TextModel.h"
#include "Utf8.h"
#include "Uuid.h"
#include "Yaml.h"
#include "Zip.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

std::mt19937 rng(12345); // graine par défaut : reproductible

// Chaîne d'octets quelconques (y compris \0 au milieu, séquences UTF-8
// invalides, etc.).
std::string randomBytes(size_t maxLen) {
    std::uniform_int_distribution<int> lenDist(0, static_cast<int>(maxLen));
    const size_t n = static_cast<size_t>(lenDist(rng));
    std::string s;
    s.reserve(n);
    std::uniform_int_distribution<int> byteDist(0, 255);
    for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>(byteDist(rng)));
    return s;
}

// Chaîne biaisée vers les caractères « intéressants » pour nos analyseurs
// (marqueurs Markdown, YAML, JSON), plus susceptible d'atteindre les
// chemins de code rares.
std::string randomStructured(size_t maxLen) {
    static const char* alphabet =
        "---\n#*_~`<>[](){}\":|- abcXY0123\t\\\xC3\xA9\xE2\x80\x94\xF0\x9F\x98\x80";
    const size_t alen = std::char_traits<char>::length(alphabet);
    std::uniform_int_distribution<int> lenDist(0, static_cast<int>(maxLen));
    const size_t n = static_cast<size_t>(lenDist(rng));
    std::string s;
    s.reserve(n);
    std::uniform_int_distribution<size_t> pick(0, alen - 1);
    for (size_t i = 0; i < n; ++i) s.push_back(alphabet[pick(rng)]);
    return s;
}

// Entier aléatoire borné, pour tester les opérations du modèle avec des
// positions arbitraires (le modèle doit toujours les borner lui-même).
size_t randomPos(size_t hi) {
    if (hi == 0) return 0;
    std::uniform_int_distribution<size_t> d(0, hi);
    return d(rng);
}

// --- Générateur par MUTATION -----------------------------------------
// Corpus d'entrées VALIDES, représentatives de ce que l'éditeur écrit
// vraiment : un manuscrit complet et une fiche complète. Les muter donne
// des entrées « presque correctes », qui passent les premières
// validations et exercent le cœur des analyseurs.
const std::vector<std::string>& corpus() {
    static const std::vector<std::string> samples = {
        // Manuscrit : en-tête YAML + titres + styles en ligne + accents.
        // Les clés sont celles que MarkdownIO écrit RÉELLEMENT (title,
        // author, cover, summary) : un corpus aux clés inventées ne
        // ferait jamais entrer le mutant dans le code de lecture des
        // métadonnées — vérifié, ce piège avait été tendu.
        "---\n"
        "title: \"Les Cendres d'Élise\"\n"
        "author: Thibaud\n"
        "cover: images/couverture.png\n"
        "summary: Un roman d'essai, sur une ligne.\n"
        "---\n"
        "\n"
        "# Chapitre premier\n"
        "\n"
        "Il faisait **nuit noire**, et le 1<sup>er</sup> coup sonna.\n"
        "\n"
        "## Une sous-partie\n"
        "\n"
        "Élise se leva, <u>lentement</u>, puis <span style=\"font-size:1.3em\">cria</span>.\n"
        "\n"
        "# Chapitre second\n"
        "\n"
        "~~Barré~~ et *italique* et <sub>indice</sub>.\n",

        // Fiche de worldbuilding : en-tête YAML (scalaires, listes, bloc)
        // + corps libre.
        "---\n"
        "id: elise\n"
        "type: personnage\n"
        "titre: \"Élise\"\n"
        "tags:\n"
        "  - héroïne\n"
        "  - noblesse\n"
        "age: \"24\"\n"
        "description_physique: |\n"
        "  Grande, brune.\n"
        "  Une cicatrice au front.\n"
        "relations:\n"
        "  - marc\n"
        "  - bran\n"
        "illustrations:\n"
        "  - images/elise.png\n"
        "deces_chapitre: Chapitre second\n"
        "---\n"
        "\n"
        "Née dans la vallée, elle n'en est jamais vraiment partie.\n",
    };
    return samples;
}

// Applique une mutation au hasard. Les six opérations couvrent les
// accidents réels (fichier tronqué, octet corrompu, ligne dupliquée) et
// les cas hostiles (marqueur injecté, chaîne démesurée).
std::string mutated() {
    const std::vector<std::string>& samples = corpus();
    std::uniform_int_distribution<size_t> pickSample(0, samples.size() - 1);
    std::string s = samples[pickSample(rng)];

    std::uniform_int_distribution<int> howMany(1, 6); // plusieurs mutations
    const int count = howMany(rng);
    for (int m = 0; m < count && !s.empty(); ++m) {
        std::uniform_int_distribution<size_t> pos(0, s.size() - 1);
        std::uniform_int_distribution<int> byteDist(0, 255);
        std::uniform_int_distribution<int> which(0, 5);
        const size_t p = pos(rng);
        switch (which(rng)) {
            case 0: // retourner un bit
                s[p] = static_cast<char>(s[p] ^ (1 << (byteDist(rng) % 8)));
                break;
            case 1: // remplacer un octet
                s[p] = static_cast<char>(byteDist(rng));
                break;
            case 2: // insérer un octet
                s.insert(s.begin() + static_cast<long>(p),
                         static_cast<char>(byteDist(rng)));
                break;
            case 3: // supprimer un octet
                s.erase(s.begin() + static_cast<long>(p));
                break;
            case 4: { // dupliquer un morceau (lignes répétées, listes gonflées)
                const size_t len = std::min<size_t>(s.size() - p, 64);
                s.insert(p, s.substr(p, len));
                break;
            }
            case 5: // tronquer (fichier coupé en cours d'écriture)
                s.resize(p);
                break;
            default:
                break;
        }
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    // Sortie non tamponnée : sur une campagne longue, on veut voir la
    // progression en direct — et, en cas d'arrêt brutal, savoir où.
    std::cout << std::unitbuf;

    long long iterations = 40000;
    unsigned seed = 12345;
    if (argc > 1) {
        const long long n = std::atoll(argv[1]);
        if (n > 0) iterations = n;
    }
    if (argc > 2) {
        const long long s = std::atoll(argv[2]);
        if (s > 0) seed = static_cast<unsigned>(s);
    }
    rng.seed(seed);
    std::cout << "Fuzz : " << iterations << " iterations, graine " << seed << "\n";

    for (long long i = 0; i < iterations; ++i) {
        // Trois générateurs en alternance (voir l'en-tête du fichier).
        const int mode = static_cast<int>(i % 3);
        const std::string raw = (mode == 0) ? randomBytes(200)
                              : (mode == 1) ? randomStructured(300)
                                            : mutated();

        // Repère de progression, discret : une ligne tous les 250 000 tours.
        if (i > 0 && i % 250000 == 0) {
            std::cout << "  ... " << i << " entrees traitees\n";
        }

        // 1. Décodeur UTF-8 : ne doit jamais déborder sur une séquence tronquée.
        const std::u32string u32 = utils::utf8ToU32(raw);
        (void)utils::u32ToUtf8(u32);

        // 2. Analyseur Markdown complet (en-tête YAML + corps + styles en ligne).
        core::Document doc = io::parse(raw);
        // ...puis on ré-sérialise ce qu'on a lu (aller-retour).
        (void)io::serialize(doc);

        // 3. Analyseur YAML seul.
        (void)io::YamlBlock::parse(io::splitLines(raw));

        // 4. Analyseur de fiche.
        core::Card card;
        (void)io::parseCard(raw, card);

        // 5. Opérations du modèle avec des positions ARBITRAIRES : le modèle
        //    doit borner lui-même toute position venue de l'extérieur.
        if (!doc.blocks().empty()) {
            core::TextBlock& b = doc.blocks()[0];
            const size_t len = b.length();
            b.insertText(randomPos(len + 50), u32);
            b.applyStyle(randomPos(len + 50), randomPos(len + 50), core::TextStyle::Bold);
            b.setFontSize(randomPos(len + 50), randomPos(len + 50),
                          static_cast<uint16_t>(randomPos(900)));
            b.removeText(randomPos(len + 50), randomPos(len + 50));
            (void)b.styleAt(randomPos(len + 50));
            (void)b.sizeAt(randomPos(len + 50));
            doc.splitBlock(randomPos(doc.blocks().size() + 2), randomPos(len + 50));
            doc.moveBlocks(randomPos(doc.blocks().size() + 2),
                           randomPos(doc.blocks().size() + 2),
                           randomPos(doc.blocks().size() + 2));
        }

        // 5 bis. COMPARAISON et REDÉCOUPAGE. Le découpage en phrases
        //    regarde en avant et en arrière du point courant (fermetures,
        //    abréviations) : c'est exactement le genre de code où un
        //    caractère au mauvais endroit fait sortir des bornes. Le
        //    redécoupage, lui, reçoit deux documents sans aucun rapport —
        //    il doit rendre quelque chose plutôt que de s'égarer.
        //
        //    Le découpage en phrases, lui, passe à CHAQUE tour : il est
        //    bon marché, et c'est celui qui inspecte le plus finement les
        //    caractères. Le reste — alignements de mots, deux documents à
        //    analyser — coûte assez pour doubler la durée d'une campagne :
        //    une itération sur quatre suffit, comme pour les
        //    dictionnaires plus bas.
        (void)core::splitSentences(u32);
        if (i % 4 == 0) {
            (void)core::tokenizeWords(u32);
            (void)core::diffText(u32, u32.substr(0, u32.size() / 2));

            core::Document autre = io::parse(raw.substr(0, raw.size() / 2) + raw);
            (void)core::flatness(doc.blocks());
            (void)core::reflowLike(doc.blocks(), autre.blocks());
            (void)core::reflowLike(autre.blocks(), doc.blocks());
        }

        // 5 ter. RÉPONSES DE SERVEUR (Phase 5, jalon B). C'est une
        //    surface d'entrée non fiable toute neuve, et d'une nature
        //    particulière : contrairement à un fichier, elle vient d'une
        //    machine distante que l'auteur ne contrôle pas forcément. Le
        //    client doit encaisser n'importe quel corps, n'importe quel
        //    code d'état et n'importe quel en-tête sans jamais rendre pour
        //    bon ce qu'il n'a pas vérifié.
        //
        //    Aucune connexion n'est ouverte : le transport est simulé.
        {
            net::FakeTransport transport;
            transport.failWhenEmpty = false;
            for (int k = 0; k < 4; ++k) {
                transport.reply(static_cast<int>(100 + (rng() % 450)), raw,
                                raw.substr(0, 32),
                                {{"ETag", raw.substr(0, 80)},
                                 {"Content-Type", raw.substr(0, 40)}});
            }

            net::SyncClient client(transport, raw.substr(0, 24));
            net::ServerInfo info;
            (void)client.version(info);

            io::ProjectIndex distant;
            (void)client.remoteIndex("11111111-2222-4333-8444-555555555555", distant);
            (void)client.remoteIndex(raw.substr(0, 40), distant);

            std::string contenu, empreinte;
            (void)client.readFile("11111111-2222-4333-8444-555555555555",
                                  raw.substr(0, 60), contenu, empreinte);
            (void)client.writeFile("11111111-2222-4333-8444-555555555555", "roman.md",
                                   raw, raw.substr(0, 64), empreinte);

            (void)net::SyncClient::isSafePath(raw);
            (void)net::SyncClient::percentEncode(raw.substr(0, 200));
            (void)net::SyncClient::readKey(raw, "api");
            (void)net::findHeader({{raw.substr(0, 20), raw}}, "etag");
#ifdef _WIN32
            (void)net::WinHttpTransport::parseBaseUrl(raw.substr(0, 120));
#endif

            // Le moteur de synchronisation reçoit lui aussi des
            // inventaires venus du réseau : deux inventaires quelconques
            // doivent produire un plan, jamais un égarement.
            const io::ProjectIndex a = io::parseIndex(raw);
            const io::ProjectIndex b = io::parseIndex(raw.substr(raw.size() / 2) + raw);
            const io::SyncPlan plan = io::planSync(a, b, distant, io::SyncMode::TwoWay);
            (void)plan.bytesToTransfer();
            (void)io::planSync(a, b, distant, io::SyncMode::ReadOnly);
            (void)io::advanceBase(a, b, distant, plan, {raw.substr(0, 30), "roman.md"});
        }

        // 6. Chaîne d'EXPORT (Phase 4). Le contenu qui la traverse vient
        //    du manuscrit, donc de l'utilisateur : elle doit encaisser
        //    n'importe quoi. On lui donne aussi des noms d'entrées
        //    d'archive quelconques (le nom doit être validé, jamais
        //    recopié tel quel dans un chemin).
        {
            io::ZipWriter zip;
            (void)io::ZipWriter::isValidEntryName(raw);
            (void)zip.addFile(raw, raw);                    // nom non fiable
            (void)zip.addFile("OEBPS/text/x.xhtml", raw);   // contenu non fiable
            (void)zip.build();
        }
        // 7. Fichiers techniques du SUIVI DE PROJET (Phase 5). Ils vivent
        //    dans le dossier de l'auteur : ils peuvent être tronqués par
        //    une coupure, bricolés à la main, ou recopiés depuis une
        //    autre machine. Leur lecture doit toujours rendre quelque
        //    chose d'exploitable, jamais planter.
        {
            const io::ProjectIndex index = io::parseIndex(raw);
            (void)io::serializeIndex(index);           // aller-retour
            (void)io::diffIndex(index, io::parseIndex(raw + "\n" + raw));
            (void)io::parseJournal(raw);
            (void)utils::isValidUuid(raw);
            (void)utils::sha256Hex(raw);
        }

        // 8. Fichiers de DICTIONNAIRE (fin de Phase 1). Ce sont des
        //    fichiers extérieurs au projet, téléchargés ou remplacés par
        //    l'utilisateur : leur lecture doit encaisser n'importe quoi.
        //
        //    UNE ITÉRATION SUR DIX seulement : proposer des corrections
        //    engendre des milliers de mots candidats, et sous
        //    AddressSanitizer cela multipliait par vingt la durée de tout
        //    l'audit. Les mêmes chemins de code sont couverts, simplement
        //    moins souvent. L'expansion est bornée à quelques milliers de
        //    formes, sans quoi un fichier hostile ferait enfler la mémoire.
        if (i % 10 == 0) {
            const io::AffixData affix = io::parseAff(raw);
            io::ExpandOptions bounded;
            bounded.maxForms = 5000;
            const std::vector<std::string> forms = io::expandDictionary(raw, affix, bounded);

            core::Speller speller;
            speller.setForms(forms);
            speller.setReplacements(affix.replacements);
            (void)speller.check(u32);
            (void)speller.suggest(raw.substr(0, 12));

            io::Thesaurus thesaurus;
            if (thesaurus.load(raw)) (void)thesaurus.lookup(raw.substr(0, 16));
            (void)thesaurus.lookup("roman");
        }

        // L'EPUB complet est nettement plus coûteux : une itération sur
        // vingt suffit à couvrir les mêmes chemins de code.
        if (i % 20 == 0) {
            const io::Book book = io::bookFromDocument(doc);
            (void)io::buildEpub(book);
        }
    }

    std::cout << "Fuzz analyseurs : " << iterations
              << " entrees pathologiques traitees sans plantage (graine "
              << seed << ").\n";
    return 0;
}
