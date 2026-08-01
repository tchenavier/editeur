// main_webview.cpp
// -----------------------------------------------------------------------
// Point d'entrée de l'application : la SEULE pièce qui connaît webview.
//
// Utilise la librairie webview/webview (https://github.com/webview/webview,
// licence MIT) : un header C/C++ unique qui s'appuie sur le moteur de
// rendu déjà présent sur l'OS (WebView2 sur Windows, WKWebView sur macOS,
// WebKitGTK sur Linux).
//
// NOTE DE COMPILATION (ce fichier n'est pas compilé dans les tests) :
//   - Windows : nécessite le SDK WebView2 (nuget Microsoft.Web.WebView2)
//   - Linux   : nécessite webkit2gtk (paquet libwebkit2gtk-4.1-dev)
//   - macOS   : rien à installer (framework WebKit système)
//   Voir la documentation de webview/webview pour les drapeaux exacts.
//
// Tout le reste du programme (Bridge, EditorApi, TextModel) compile et se
// teste sans aucune de ces dépendances : c'est le but de la séparation.
// -----------------------------------------------------------------------

// Cible Windows volontairement ancienne pour CE fichier, AVANT tout
// include : le SDK 10.0.26100 souffre d'une inclusion circulaire entre
// winbase.h et fileapi.h qui casse la compilation de <windows.h> aux
// versions récentes (STREAM_INFO_LEVELS / CREATEFILE2_EXTENDED_PARAMETERS
// utilisés avant d'être définis). Cibler 0x0501 désactive les blocs
// concernés. Cela ne limite rien ici : ces macros ne font que filtrer les
// déclarations visibles, et ni webview ni notre code n'utilisent d'API
// plus récente. Même contournement que dans NativeDialog.cpp.
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include "webview.h"   // header unique de webview/webview

#include "AccountApi.h"
#include "Bridge.h"
#include "EditorApi.h"
#include "CardApi.h"
#include "ExportApi.h"
#include "ManuscriptApi.h"
#include "CompareApi.h"
#include "ProjectApi.h"
#include "SpellApi.h"
#include "SyncApi.h"
#include "NativeDialog.h"
#include "TextModel.h"
#include "UndoManager.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {

#ifdef _WIN32
// --- Garde-fou à la fermeture (cf. cadrage : prévenir si le projet n'est
// pas enregistré quand l'utilisateur demande l'arrêt de l'application).
//
// webview n'expose pas de rappel « fenêtre en cours de fermeture » : on
// intercepte donc WM_CLOSE en sous-classant la fenêtre native. L'UI tient
// le C++ informé de l'état du document via la commande file.setState.
WNDPROC g_originalWndProc = nullptr;
bridge::ManuscriptApi* g_fileApi = nullptr;

LRESULT CALLBACK closeGuardProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE && g_fileApi && g_fileApi->hasUnsavedChanges()) {
        switch (bridge::confirmUnsavedChanges(hwnd)) {
            case bridge::UnsavedChoice::Cancel:
                return 0; // on ne ferme pas
            case bridge::UnsavedChoice::Save: {
                std::string error;
                bool conflict = false;
                if (!g_fileApi->saveCurrent(&error, /*force=*/false, &conflict)) {
                    if (conflict) {
                        // Le fichier a changé en dehors de l'éditeur : on ne
                        // l'écrase pas sans un accord explicite.
                        const int ans = MessageBoxW(
                            hwnd,
                            L"Le fichier a été modifié en dehors de l'éditeur depuis "
                            L"son ouverture. Écraser ces modifications extérieures ?",
                            L"Conflit de fichier",
                            MB_YESNO | MB_ICONWARNING);
                        if (ans == IDYES && g_fileApi->saveCurrent(&error, /*force=*/true)) {
                            break; // forcé avec l'accord de l'utilisateur
                        }
                    }
                    // Refus, ou autre échec : mieux vaut rester ouvert que
                    // perdre du travail.
                    return 0;
                }
                break;
            }
            case bridge::UnsavedChoice::Discard:
                break; // fermeture demandée en connaissance de cause
        }
    }
    return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
}
#endif

#ifdef _WIN32
// ---------------------------------------------------------------------
// EXPORT PDF (Phase 4) — « via le moteur de rendu interne du webview »
// comme le prévoit le cadrage.
//
// On n'imprime pas un document reconstruit : on demande à WebView2 de
// rendre en PDF la PAGE COURANTE, que l'interface a préalablement mise
// en « vue d'impression » (règles @media print). C'est le moteur de
// Chromium qui pagine — pas de convertisseur maison à maintenir.
//
// Deux points délicats, traités ici :
//
//  1. PrintToPdf est ASYNCHRONE. Le gestionnaire du pont, lui, est
//     appelé depuis la boucle de messages : l'attendre bloquerait cette
//     boucle, donc empêcherait la fin de l'impression — un interblocage
//     garanti. La commande rend donc la main tout de suite
//     ({ started: true }) et la FIN est signalée au JS par un appel à
//     window.__exportPdfDone(...).
//
//  2. La bibliothèque webview n'expose que le contrôleur natif : on
//     descend jusqu'à ICoreWebView2_7 (l'interface qui porte PrintToPdf)
//     par QueryInterface. Si le runtime WebView2 installé est trop
//     ancien, la commande échoue proprement et l'interface se rabat sur
//     la boîte d'impression du système.
// ---------------------------------------------------------------------

// Pointeur COM minimal (RAII) : libère automatiquement. On évite ainsi
// les Release() manuels — un seul oubli sur un chemin d'erreur suffirait
// à faire fuir une interface, ce que le cadrage proscrit.
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put() { reset(); return &m_ptr; }   // pour les sorties (&ptr)
    T* get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }
    void reset() {
        if (m_ptr) { m_ptr->Release(); m_ptr = nullptr; }
    }

private:
    T* m_ptr = nullptr;
};

webview::webview* g_window = nullptr;
bool g_pdfInProgress = false;

// Gestionnaire de fin d'impression. Instance UNIQUE et statique (un seul
// export à la fois) : sa durée de vie couvre celle du programme, donc
// AddRef/Release n'ont rien à libérer et il n'y a aucun new/delete.
class PrintDoneHandler : public ICoreWebView2PrintToPdfCompletedHandler {
public:
    void setTarget(std::string path) { m_path = std::move(path); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2PrintToPdfCompletedHandler) {
            *ppv = this;
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }  // objet statique :
    ULONG STDMETHODCALLTYPE Release() override { return 1; } // jamais détruit

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, BOOL isSuccessful) override {
        g_pdfInProgress = false;
        const bool ok = SUCCEEDED(errorCode) && isSuccessful;
        // On repasse la main au JS : c'est lui qui affiche le message et
        // qui remet l'interface en mode édition.
        bridge::json payload = {{"ok", ok}, {"path", m_path}};
        if (!ok) payload["error"] = "Le moteur d'impression n'a pas pu écrire le PDF.";
        if (g_window) {
            g_window->eval("window.__exportPdfDone && window.__exportPdfDone(" +
                           payload.dump() + ");");
        }
        return S_OK;
    }

private:
    std::string m_path;
};

PrintDoneHandler g_printHandler;

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), len);
    return wide;
}

// Dimensions de page, en POUCES (unité de l'API d'impression).
void pageSizeInches(const std::string& name, double* width, double* height) {
    if (name == "a5")     { *width = 5.83; *height = 8.27; return; }
    if (name == "letter") { *width = 8.50; *height = 11.0; return; }
    *width = 8.27; *height = 11.69; // A4 par défaut
}

// Commande export.pdf. Le chemin de destination vient de l'interface
// (dialogue natif « Enregistrer sous »).
bridge::json exportPdf(const bridge::json& payload) {
    const std::string path = bridge::requireString(payload, "path");
    if (g_pdfInProgress) {
        throw std::runtime_error("Un export PDF est déjà en cours.");
    }
    if (!g_window) {
        throw std::runtime_error("Fenêtre indisponible.");
    }

    const auto controllerResult = g_window->browser_controller();
    auto* controller = controllerResult.ok()
                           ? static_cast<ICoreWebView2Controller*>(controllerResult.value())
                           : nullptr;
    if (!controller) throw std::runtime_error("Moteur de rendu indisponible.");

    ComPtr<ICoreWebView2> core;
    if (FAILED(controller->get_CoreWebView2(core.put())) || !core) {
        throw std::runtime_error("Moteur de rendu indisponible.");
    }

    ComPtr<ICoreWebView2_7> printer;
    if (FAILED(core->QueryInterface(IID_ICoreWebView2_7,
                                    reinterpret_cast<void**>(printer.put()))) ||
        !printer) {
        // Runtime WebView2 trop ancien : l'interface se rabattra sur la
        // boîte d'impression du système (window.print()).
        throw std::runtime_error(
            "L'impression PDF directe n'est pas disponible sur cette version "
            "du moteur WebView2.");
    }

    // Réglages d'impression : sans en-tête ni pied de page (sinon Chromium
    // ajoute date, titre et URL sur chaque page — inacceptable pour un
    // livre), avec les fonds de page pour garder la couverture.
    ComPtr<ICoreWebView2PrintSettings> settings;
    ComPtr<ICoreWebView2_2> core2;
    if (SUCCEEDED(core->QueryInterface(IID_ICoreWebView2_2,
                                       reinterpret_cast<void**>(core2.put()))) && core2) {
        ComPtr<ICoreWebView2Environment> environment;
        if (SUCCEEDED(core2->get_Environment(environment.put())) && environment) {
            ComPtr<ICoreWebView2Environment6> environment6;
            if (SUCCEEDED(environment->QueryInterface(
                    IID_ICoreWebView2Environment6,
                    reinterpret_cast<void**>(environment6.put()))) && environment6) {
                environment6->CreatePrintSettings(settings.put());
            }
        }
    }
    if (settings) {
        double width = 0.0, height = 0.0;
        pageSizeInches(bridge::optString(payload, "pageSize", "a4"), &width, &height);
        const bool landscape = bridge::optBool(payload, "landscape", false);
        settings->put_Orientation(landscape ? COREWEBVIEW2_PRINT_ORIENTATION_LANDSCAPE
                                            : COREWEBVIEW2_PRINT_ORIENTATION_PORTRAIT);
        settings->put_PageWidth(landscape ? height : width);
        settings->put_PageHeight(landscape ? width : height);
        // Marges en pouces, bornées : une valeur absurde venue du JS ne
        // doit pas produire une page sans texte.
        double margin = 0.75;
        if (payload.contains("margin") && payload["margin"].is_number()) {
            margin = payload["margin"].get<double>();
            if (margin < 0.0) margin = 0.0;
            if (margin > 3.0) margin = 3.0;
        }
        settings->put_MarginTop(margin);
        settings->put_MarginBottom(margin);
        settings->put_MarginLeft(margin);
        settings->put_MarginRight(margin);
        settings->put_ShouldPrintBackgrounds(TRUE);
        settings->put_ShouldPrintHeaderAndFooter(FALSE);
    }

    g_printHandler.setTarget(path);
    g_pdfInProgress = true;
    const HRESULT hr = printer->PrintToPdf(widen(path).c_str(), settings.get(),
                                           &g_printHandler);
    if (FAILED(hr)) {
        g_pdfInProgress = false;
        throw std::runtime_error("Impossible de lancer l'impression PDF.");
    }
    // L'impression continue en tâche de fond : la fin arrive par
    // window.__exportPdfDone (voir le commentaire en tête de section).
    return {{"started", true}, {"path", path}};
}
#endif // _WIN32

// Charge l'interface et l'ASSEMBLE en une page unique.
//
// L'interface est écrite en trois fichiers voisins — ui/index.html,
// ui/style.css et ui/app.js — parce qu'un fichier de 220 Ko est
// pénible à parcourir. Mais la page est fournie au moteur de rendu par
// set_html, c'est-à-dire SANS ORIGINE : un <link href="style.css"> ou un
// <script src="app.js"> n'y désignerait aucun emplacement atteignable et
// ne chargerait rien. On remplace donc ces deux balises par le contenu
// des fichiers avant de livrer la page.
//
// L'interface reste ainsi parfaitement auto-contenue à l'exécution (aucune
// ressource extérieure, ce qu'exige le cadrage), tout en restant découpée
// à l'écriture. Ouvrir ui/index.html directement dans un navigateur
// fonctionne aussi : là, les deux balises sont résolues normalement.
//
// NOTE (production) : à terme, ces fichiers seront embarqués dans le
// binaire à la compilation, comme prévu au cadrage. La lecture sur disque
// est un confort de développement — modifier l'interface ne demande pas
// de recompiler, seulement de relancer. Le littéral kIndexHtml plus bas
// sert de filet de sécurité si les fichiers sont introuvables.
std::string readFileOrEmpty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Remplace la première occurrence de 'marker' par 'replacement'.
// Retourne false si le repère est absent : l'appelant préfère alors la
// page de repli à une interface amputée de son style ou de son code.
bool replaceOnce(std::string& text, const std::string& marker,
                 const std::string& replacement) {
    const size_t position = text.find(marker);
    if (position == std::string::npos) return false;
    text.replace(position, marker.size(), replacement);
    return true;
}

std::string loadUiHtml() {
    // Dossiers candidats : l'exécutable peut être lancé depuis divers
    // dossiers de build pendant le développement.
    const char* folders[] = { "ui/", "../ui/", "../../ui/", "../../../ui/" };

    for (const char* folder : folders) {
        std::string html = readFileOrEmpty(std::string(folder) + "index.html");
        if (html.empty()) continue;

        const std::string css = readFileOrEmpty(std::string(folder) + "style.css");
        const std::string js = readFileOrEmpty(std::string(folder) + "app.js");
        if (css.empty() || js.empty()) {
            // index.html sans ses deux compagnons : mieux vaut la page de
            // repli, qui annonce clairement le problème, qu'une interface
            // sans style ni comportement.
            return "";
        }

        // Le contenu inséré n'est jamais échappé : ce sont NOS fichiers,
        // pas des données d'utilisateur. La seule précaution utile serait
        // qu'ils ne contiennent pas de balise fermante parasite, ce que le
        // navigateur signalerait immédiatement.
        if (!replaceOnce(html, "<link rel=\"stylesheet\" href=\"style.css\">",
                         "<style>\n" + css + "\n</style>") ||
            !replaceOnce(html, "<script src=\"app.js\"></script>",
                         "<script>\n" + js + "\n</script>")) {
            return ""; // repères absents : index.html ne correspond plus
        }
        return html;
    }
    return "";
}

// Page de repli si ui/index.html est introuvable : valide seulement que
// le pont répond (elle n'est PAS l'interface réelle).
const char* kIndexHtml = R"html(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<title>Editeur - base du pont</title>
<style>
  body { font-family: sans-serif; margin: 2rem; }
  #status { color: #666; font-style: italic; }
</style>
</head>
<body>
<h1>Pont C++ &harr; JS : test</h1>
<p id="status">Chargement...</p>
<pre id="output"></pre>
<script>
/* --------------------------------------------------------------------
 * bridge.js : côté JS du pont.
 *
 * webview expose la fonction C++ liée sous window.cppInvoke (voir plus
 * bas côté C++). Elle retourne nativement une Promise. On l'enveloppe
 * pour :
 *   1. construire le protocole { command, payload } d'un seul côté ;
 *   2. convertir les réponses { ok:false } en rejets de Promise, pour
 *      que le code appelant utilise un try/catch standard ;
 *   3. offrir un point unique où brancher plus tard des logs ou une
 *      file d'attente (utile pour l'undo/redo groupé par mot).
 * ------------------------------------------------------------------ */
const bridge = {
  async invoke(command, payload = {}) {
    // La fonction liée par webview prend des arguments JSON-sérialisables
    // et retourne la chaîne JSON produite par Bridge::dispatch.
    const raw = await window.cppInvoke(JSON.stringify({ command, payload }));
    const response = JSON.parse(raw);
    if (!response.ok) {
      // Erreur métier ou de validation côté C++ : on la propage comme
      // une exception JS normale.
      throw new Error(response.error);
    }
    return response.result;
  }
};

/* Démonstration minimale : insère du texte, le met en gras, relit l'état. */
(async () => {
  const status = document.getElementById("status");
  const output = document.getElementById("output");
  try {
    await bridge.invoke("editor.insertText",
      { block: 0, pos: 0, text: "Bonjour le monde" });
    await bridge.invoke("editor.toggleWordStyle",
      { block: 0, pos: 0, style: "bold" });

    const doc = await bridge.invoke("editor.getDocument");
    const count = await bridge.invoke("editor.wordCount");

    status.textContent = "Pont fonctionnel. Mots : " + count.total;
    output.textContent = JSON.stringify(doc, null, 2);
  } catch (err) {
    status.textContent = "Erreur : " + err.message;
  }
})();
</script>
</body>
</html>
)html";

} // namespace

int main() {
    // --- Assemblage : modèle -> API -> pont ---
    // Ordre de déclaration important : le Bridge garde des références
    // vers l'API, qui garde une référence vers le document. Déclarés
    // dans cet ordre, ils sont détruits dans l'ordre inverse, donc
    // aucune référence pendante possible.
    core::Document document;
    document.blocks().emplace_back(core::BlockType::Paragraph);

    core::UndoManager undoManager;

    // Fiches de worldbuilding (Phase 2). La bibliothèque démarre vide ;
    // l'interface la charge via cards.open quand un dossier est choisi.
    core::CardLibrary cardLibrary;
    core::SchemaRegistry cardSchemas;

    bridge::Bridge dispatcher;
    bridge::EditorApi editorApi(document, undoManager);
    bridge::ManuscriptApi fileApi(document, undoManager);
    bridge::CardApi cardApi(cardLibrary, cardSchemas);
    bridge::ExportApi exportApi(document, cardLibrary, cardSchemas);
    // Suivi du projet (Phase 5, jalon A) : inventaire et journal des
    // modifications. Entièrement local, aucun compte, aucun réseau.
    bridge::ProjectApi projectApi(document);
    // Correcteur orthographique et synonymes (fin de la Phase 1). Les
    // dictionnaires ne sont PAS chargés ici : l'interface le demandera
    // une fois la fenêtre affichée (voir SpellApi.h).
    bridge::SpellApi spellApi;
    // Comparaison de versions : relire une correction, ou voir ce qui
    // diffère avant d'écraser un fichier modifié en dehors de l'éditeur.
    bridge::CompareApi compareApi(document, undoManager);
    // Comptes et appairage (Phase 5, jalon C). CETTE LIGNE N'OUVRE RIEN :
    // aucune session réseau, aucune poignée système, aucun fil. La
    // connexion reste un objet vide tant que l'auteur ne demande pas
    // explicitement à se connecter — un éditeur de roman doit démarrer
    // aussi vite hors ligne qu'en ligne. Voir net/Connection.h.
    net::Connection connection;
    bridge::AccountApi accountApi(connection, document);
    // Synchronisation (jalon D). Ne parle au réseau que si l'on est
    // connecté ET le projet appairé : deux conditions distinctes.
    bridge::SyncApi syncApi(connection, document);
    if (!editorApi.registerOn(dispatcher) || !fileApi.registerOn(dispatcher) ||
        !cardApi.registerOn(dispatcher) || !exportApi.registerOn(dispatcher) ||
        !projectApi.registerOn(dispatcher) || !spellApi.registerOn(dispatcher) ||
        !compareApi.registerOn(dispatcher) || !accountApi.registerOn(dispatcher) ||
        !syncApi.registerOn(dispatcher)) {
        return 1; // conflit de noms de commandes : bug de câblage, on refuse de démarrer
    }

    // --- Fenêtre webview ---
    // Le 'true' active les outils de développement (console JS) : à
    // désactiver pour les versions distribuées aux utilisateurs.
    webview::webview window(true, nullptr);
    window.set_title("Editeur - base");
    window.set_size(900, 600, WEBVIEW_HINT_NONE);

#ifdef _WIN32
    // L'export PDF est la SEULE commande qui a besoin du webview (il
    // utilise son moteur de rendu) : elle est donc enregistrée ici, et
    // non dans un module *Api testable sans interface.
    g_window = &window;
    if (!dispatcher.registerCommand("export.pdf", exportPdf)) {
        return 1;
    }
#endif

    // Handle de la fenêtre native (HWND sur Windows). webview le renvoie
    // dans un result<> : il faut vérifier avant d'extraire, l'appel pouvant
    // échouer si la fenêtre n'a pas pu être créée.
    const auto windowHandle = window.window();
    void* nativeHandle = windowHandle.ok() ? windowHandle.value() : nullptr;

    // Permet à ManuscriptApi de rendre les dialogues « Ouvrir »/« Enregistrer »
    // modaux par rapport à la fenêtre principale.
    fileApi.setOwnerWindow(nativeHandle);

#ifdef _WIN32
    // Sous-classe la fenêtre pour intercepter sa fermeture (voir plus haut).
    // Sans handle, on s'en passe plutôt que de déréférencer un pointeur nul :
    // l'application reste utilisable, seule l'alerte de fermeture manque.
    if (nativeHandle) {
        g_fileApi = &fileApi;
        g_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(static_cast<HWND>(nativeHandle), GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(closeGuardProc)));
    }
#endif

    // Lie la fonction JS window.cppInvoke au dispatcher C++.
    // webview transmet les arguments JS sous forme de tableau JSON :
    // req est donc '["<notre requete serialisee>"]'. On extrait le
    // premier élément puis on délègue tout au Bridge.
    window.bind("cppInvoke", [&dispatcher](const std::string& req) -> std::string {
        bridge::json args = bridge::json::parse(req, nullptr, /*allow_exceptions=*/false);
        if (args.is_discarded() || !args.is_array() || args.empty() || !args[0].is_string()) {
            // Réponse d'erreur au même format que le Bridge, pour que le
            // JS n'ait qu'un seul format à gérer.
            return R"({"ok":false,"error":"Arguments d'appel invalides."})";
        }
        // dispatch est noexcept : quoi qu'il arrive, une chaîne JSON
        // valide revient au JS. webview la passera telle quelle à la
        // Promise côté JS.
        return dispatcher.dispatch(args[0].get<std::string>());
    });

    // Interface réelle si disponible, sinon page de repli du pont.
    const std::string ui = loadUiHtml();
    window.set_html(ui.empty() ? kIndexHtml : ui.c_str());
    window.run();
    return 0;
}
