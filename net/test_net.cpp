// test_net.cpp
// Tests du client de protocole (net/SyncClient) contre un transport simulé.
//
// UN VRAI SERVEUR RÉPOND BIEN. C'est précisément pour cela qu'il ne suffit
// pas à éprouver un client : ce qui casse, ce sont les réponses tordues —
// corps qui ne correspond pas à son empreinte, transfert coupé, type de
// contenu faux, code inattendu, serveur d'une autre version. Toutes
// s'écrivent ici en une ligne, et aucune ne doit produire autre chose
// qu'un refus propre.
//
// Aucun réseau n'est ouvert par ce fichier.

#include "FakeTransport.h"
#include "Sha256.h"
#include "SyncClient.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

std::string sha(const std::string& content) {
    utils::Sha256 hash;
    hash.update(content);
    return hash.finalHex();
}

const char* const kProjet = "11111111-2222-4333-8444-555555555555";

} // namespace

int main() {
    std::cout << std::unitbuf;

    // --- Test 1 : poignée de main ---
    {
        net::FakeTransport transport;
        net::SyncClient client(transport, "jeton-secret");

        transport.reply(200, "api 1\nserver editeur-sync 0.1\n");
        net::ServerInfo info;
        assert(client.version(info).ok());
        assert(info.api == 1);
        assert(info.server == "editeur-sync 0.1");

        // Le jeton n'est PAS envoyé à la poignée de main : on ne se
        // présente pas à un serveur qu'on n'a pas encore reconnu.
        assert(net::findHeader(transport.lastRequest().headers, "Authorization").empty());
        assert(transport.lastRequest().path == "/api/version");
        std::cout << "Test 1 OK (poignee de main, jeton non divulgue)\n";
    }

    // --- Test 2 : serveurs avec lesquels on refuse de parler ---
    {
        auto refuse = [](const std::string& corps, net::Outcome attendu) {
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.reply(200, corps);
            net::ServerInfo info;
            const net::Result result = client.version(info);
            assert(result.outcome == attendu);
            assert(!result.message.empty()); // il y a toujours quelque chose à dire à l'auteur
        };

        refuse("api 2\n", net::Outcome::Refused);          // autre version du contrat
        refuse("<html>Bienvenue</html>", net::Outcome::BadResponse); // pas un serveur de synchro
        refuse("", net::Outcome::BadResponse);
        refuse("api douze\n", net::Outcome::BadResponse);
        refuse("api 999999999999999999999\n", net::Outcome::BadResponse); // pas de débordement
        std::cout << "Test 2 OK (versions et reponses inintelligibles refusees)\n";
    }

    // --- Test 3 : chemins refusés AVANT de construire une URL ---
    {
        assert(net::SyncClient::isSafePath("roman.md"));
        assert(net::SyncClient::isSafePath("fiches/élise.md"));
        assert(net::SyncClient::isSafePath("images/couverture.png"));

        assert(!net::SyncClient::isSafePath(""));
        assert(!net::SyncClient::isSafePath("/etc/passwd"));      // absolu
        assert(!net::SyncClient::isSafePath("../secret.md"));     // remontée
        assert(!net::SyncClient::isSafePath("a/../../b.md"));
        assert(!net::SyncClient::isSafePath("dossier\\a.md"));    // antislash
        assert(!net::SyncClient::isSafePath("C:/a.md"));          // lettre de lecteur
        assert(!net::SyncClient::isSafePath("a.md:flux"));        // flux NTFS
        assert(!net::SyncClient::isSafePath("a//b.md"));          // segment vide
        assert(!net::SyncClient::isSafePath(std::string("a\nb.md")));
        assert(!net::SyncClient::isSafePath(std::string("a\0b.md", 6)));

        // Et le client refuse tout seul, sans même envoyer.
        net::FakeTransport transport;
        net::SyncClient client(transport);
        std::string contenu, empreinte;
        const net::Result result = client.readFile(kProjet, "../secret.md", contenu, empreinte);
        assert(result.outcome == net::Outcome::Refused);
        assert(transport.sent.empty()); // rien n'est parti
        std::cout << "Test 3 OK (chemins hostiles refuses sans requete)\n";
    }

    // --- Test 4 : encodage d'URL ---
    {
        assert(net::SyncClient::percentEncode("fiches/elise.md") == "fiches/elise.md");
        assert(net::SyncClient::percentEncode("mon roman.md") == "mon%20roman.md");
        assert(net::SyncClient::percentEncode("élise.md") == "%C3%A9lise.md");
        assert(net::SyncClient::percentEncode("a?b#c") == "a%3Fb%23c");
        std::cout << "Test 4 OK (encodage d'URL)\n";
    }

    // --- Test 5 : lecture d'un fichier et vérification de l'empreinte ---
    {
        const std::string contenu = "# Chapitre premier\n\nIl faisait nuit.\n";

        {   // Cas nominal.
            net::FakeTransport transport;
            net::SyncClient client(transport, "jeton");
            transport.replyBytes(200, contenu, sha(contenu));

            std::string recu, empreinte;
            assert(client.readFile(kProjet, "roman.md", recu, empreinte).ok());
            assert(recu == contenu);
            assert(empreinte == sha(contenu));
            // Là, le jeton doit être présent.
            assert(net::findHeader(transport.lastRequest().headers, "Authorization") ==
                   "Bearer jeton");
        }

        {   // LE cas qui compte : le corps ne correspond pas à l'empreinte
            // annoncée. Un transfert coupé, un intermédiaire bavard, un
            // serveur fautif — peu importe la cause, on refuse.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.replyBytes(200, contenu.substr(0, 10), sha(contenu));

            std::string recu, empreinte;
            const net::Result result = client.readFile(kProjet, "roman.md", recu, empreinte);
            assert(result.outcome == net::Outcome::BadResponse);
            assert(recu.empty()); // rien n'est rendu à moitié
        }

        {   // Aucune empreinte annoncée : on ne devine pas.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.reply(200, contenu, "application/octet-stream");
            std::string recu, empreinte;
            assert(client.readFile(kProjet, "roman.md", recu, empreinte).outcome ==
                   net::Outcome::BadResponse);
        }

        {   // Empreinte en majuscules, ou en validation faible : acceptées.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            std::string majuscules = sha(contenu);
            for (char& c : majuscules) {
                if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
            }
            transport.reply(200, contenu, "application/octet-stream",
                            {{"etag", "W/\"" + majuscules + "\""}});
            std::string recu, empreinte;
            assert(client.readFile(kProjet, "roman.md", recu, empreinte).ok());
            assert(empreinte == sha(contenu));
        }
        std::cout << "Test 5 OK (contenu verifie contre son empreinte)\n";
    }

    // --- Test 6 : écriture, et le conflit qui n'est pas une erreur ---
    {
        const std::string contenu = "texte";

        {   // Fichier qu'on croit déjà là : on dit ce qu'on croit écraser.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.replyBytes(200, "", sha(contenu));
            std::string nouvelle;
            assert(client.writeFile(kProjet, "roman.md", contenu, sha("ancien"), nouvelle).ok());
            assert(nouvelle == sha(contenu));
            assert(net::findHeader(transport.lastRequest().headers, "If-Match") ==
                   "\"" + sha("ancien") + "\"");
            assert(transport.lastRequest().body == contenu);
        }

        {   // Fichier qu'on croit absent.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.replyBytes(200, "", sha(contenu));
            std::string nouvelle;
            assert(client.writeFile(kProjet, "neuf.md", contenu, "", nouvelle).ok());
            assert(net::findHeader(transport.lastRequest().headers, "If-Match") == "*");
        }

        {   // Quelqu'un est passé avant : information, pas panne.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.reply(409, "Le fichier a été modifié depuis votre dernière synchronisation.");
            std::string nouvelle;
            const net::Result result =
                client.writeFile(kProjet, "roman.md", contenu, sha("ancien"), nouvelle);
            assert(result.outcome == net::Outcome::Conflict);
            assert(!result.worthRetrying()); // réessayer donnerait le même refus
            // Le message du serveur, court et propre, est repris tel quel.
            assert(result.message.find("modifié") != std::string::npos);
        }

        {   // Empreinte attendue qui n'en est pas une : refus sans requête.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            std::string nouvelle;
            assert(client.writeFile(kProjet, "roman.md", contenu, "pas-une-empreinte",
                                    nouvelle).outcome == net::Outcome::Refused);
            assert(transport.sent.empty());
        }
        std::cout << "Test 6 OK (ecriture, If-Match, conflit)\n";
    }

    // --- Test 7 : l'inventaire distant passe par l'analyseur déjà durci ---
    {
        net::FakeTransport transport;
        net::SyncClient client(transport);

        io::ProjectIndex source;
        source.projectId = kProjet;
        io::ProjectFile fichier;
        fichier.path = "roman.md";
        fichier.size = 42;
        fichier.sha256 = sha("roman");
        fichier.modToken = "1";
        source.files.push_back(fichier);
        transport.reply(200, io::serializeIndex(source));

        io::ProjectIndex recu;
        assert(client.remoteIndex(kProjet, recu).ok());
        assert(recu.files.size() == 1);
        assert(recu.find("roman.md") != nullptr);
        assert(recu.find("roman.md")->sha256 == sha("roman"));

        // Un inventaire à moitié abîmé ne fait pas échouer la lecture :
        // parseIndex ignore les lignes illisibles, comme sur le disque.
        net::FakeTransport abime;
        net::SyncClient client2(abime);
        abime.reply(200, "\x01\x02 n'importe quoi\nlignes sans structure\n");
        io::ProjectIndex vide;
        assert(client2.remoteIndex(kProjet, vide).ok());
        assert(vide.files.empty());

        // Projet inconnu du serveur.
        net::FakeTransport absent;
        net::SyncClient client3(absent);
        absent.reply(404, "Projet non appairé.");
        io::ProjectIndex rien;
        assert(client3.remoteIndex(kProjet, rien).outcome == net::Outcome::NotFound);

        // Identifiant de projet bricolé : refusé sans requête.
        net::FakeTransport bricole;
        net::SyncClient client4(bricole);
        io::ProjectIndex ignore;
        assert(client4.remoteIndex("../../etc", ignore).outcome == net::Outcome::Refused);
        assert(bricole.sent.empty());
        std::cout << "Test 7 OK (inventaire distant, analyseur tolerant)\n";
    }

    // --- Test 8 : réponses hostiles et pannes ---
    {
        {   // Corps démesuré : coupé par la borne, donc refusé — jamais
            // rendu comme s'il était complet.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.reply(200, std::string(net::kMaxVersionBytes + 1000, 'x'));
            net::ServerInfo info;
            assert(client.version(info).outcome == net::Outcome::BadResponse);
        }

        {   // Panne de transport : rien n'a été dit, cela vaut la peine de
            // réessayer plus tard.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.breakDown("câble débranché (simulé)");
            net::ServerInfo info;
            const net::Result result = client.version(info);
            assert(result.outcome == net::Outcome::Unreachable);
            assert(result.worthRetrying());
            assert(result.message.find("câble") != std::string::npos);
        }

        auto codeDonne = [](int code, net::Outcome attendu, bool reessayable) {
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.reply(code, "");
            io::ProjectIndex index;
            const net::Result result = client.remoteIndex(kProjet, index);
            assert(result.outcome == attendu);
            assert(result.worthRetrying() == reessayable);
            assert(!result.message.empty());
        };
        codeDonne(401, net::Outcome::Unauthorized, false);
        codeDonne(403, net::Outcome::Forbidden, false);
        codeDonne(413, net::Outcome::TooLarge, false);
        codeDonne(429, net::Outcome::Busy, true);
        codeDonne(500, net::Outcome::ServerError, true);
        codeDonne(503, net::Outcome::ServerError, true);
        codeDonne(418, net::Outcome::Refused, false);

        {   // Un serveur ne dicte pas ce qui s'affiche : un corps d'erreur
            // démesuré ou truffé de caractères de contrôle est écarté au
            // profit de notre propre phrase.
            net::FakeTransport transport;
            net::SyncClient client(transport);
            transport.reply(404, std::string(5000, 'z'));
            io::ProjectIndex index;
            const net::Result result = client.remoteIndex(kProjet, index);
            assert(result.outcome == net::Outcome::NotFound);
            assert(result.message.size() < 200);

            net::FakeTransport controle;
            net::SyncClient client2(controle);
            controle.reply(404, std::string("\x1b[2J\x07 efface l'ecran", 21));
            io::ProjectIndex index2;
            const net::Result result2 = client2.remoteIndex(kProjet, index2);
            assert(result2.message.find('\x1b') == std::string::npos);
        }
        std::cout << "Test 8 OK (reponses hostiles, pannes, messages bornes)\n";
    }

    // --- Test 9 : suppression ---
    {
        net::FakeTransport transport;
        net::SyncClient client(transport);
        transport.reply(204, "", "");
        assert(client.removeFile(kProjet, "vieux.md", sha("vieux")).ok());
        assert(transport.lastRequest().method == net::Method::Delete);
        assert(net::findHeader(transport.lastRequest().headers, "If-Match") ==
               "\"" + sha("vieux") + "\"");
        assert(std::string(net::methodName(net::Method::Delete)) == "DELETE");
        std::cout << "Test 9 OK (suppression)\n";
    }

    std::cout << "Tous les tests reseau sont passes.\n";
    return 0;
}
