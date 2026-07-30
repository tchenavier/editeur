// NativeDialog.cpp
// Implémentation des dialogues de fichiers. Voir NativeDialog.h.
//
// IMPORTANT : sur Windows, <windows.h> est inclus EN TOUT PREMIER, avant
// tout en-tête de la bibliothèque standard (NativeDialog.h tire <string>).
// Inclure des en-têtes standard avant <windows.h> perturbe l'état du
// préprocesseur des en-têtes du SDK (ordre d'inclusion de fileapi.h vs
// winbase.h) et provoque des erreurs de compilation selon la version du
// SDK. On respecte donc la convention Windows : <windows.h> d'abord.

#ifdef _WIN32

// On cible volontairement Windows 7 (0x0601) pour CE fichier : les
// dialogues GetOpenFileName/GetSaveFileName existent depuis Windows 2000,
// donc aucune perte de fonctionnalité. Cela évite d'activer les blocs
// « fromapp » réservés à Windows 10 RS3+ de certains SDK récents
// (fileapifromapp.h), qui provoquent des erreurs de compilation.
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h> // SHBrowseForFolder : choix d'un dossier

// GetOpenFileName/GetSaveFileName vivent dans comdlg32 : on demande au
// compilateur MSVC de lier la bibliothèque, sans toucher au script de
// build (les autres compilateurs l'ignorent).
#ifdef _MSC_VER
#pragma comment(lib, "comdlg32.lib") // GetOpenFileName / GetSaveFileName
#pragma comment(lib, "user32.lib")   // MessageBoxW
#pragma comment(lib, "shell32.lib")  // SHBrowseForFolder
#pragma comment(lib, "ole32.lib")    // CoTaskMemFree
#endif

#endif // _WIN32

#include "NativeDialog.h"

#ifdef _WIN32

namespace bridge {

namespace {

// Conversions UTF-8 <-> UTF-16 (l'API Win32 large attend de l'UTF-16).
// On utilise les fonctions système plutôt que utils::Utf8 (qui fait de
// l'UTF-32) : c'est le bon outil ici, et ça garde ce fichier autonome.
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                        static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), len);
    return w;
}

std::string wideToUtf8(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string s(static_cast<size_t>(len - 1), '\0'); // -1 : sans le \0 final
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// Filtres proposés par le dialogue. Les paires « libellé \0 motif \0 »
// se suivent, la liste se termine par un \0 supplémentaire : d'où les
// littéraux explicitement terminés et le type wchar_t[] (un const wchar_t*
// s'arrêterait au premier \0 pour qui en calculerait la longueur).
const wchar_t* filterFor(FileKind kind) {
    switch (kind) {
        case FileKind::Epub:
            return L"Livre EPUB (*.epub)\0*.epub\0Tous les fichiers (*.*)\0*.*\0";
        case FileKind::Pdf:
            return L"Document PDF (*.pdf)\0*.pdf\0Tous les fichiers (*.*)\0*.*\0";
        case FileKind::Image:
            return L"Images (*.png;*.jpg;*.jpeg;*.gif)\0*.png;*.jpg;*.jpeg;*.gif\0"
                   L"Tous les fichiers (*.*)\0*.*\0";
        case FileKind::Markdown:
            break;
    }
    return L"Fichiers Markdown (*.md)\0*.md\0Tous les fichiers (*.*)\0*.*\0";
}

// Extension ajoutée si l'utilisateur n'en tape pas.
const wchar_t* defaultExtensionFor(FileKind kind) {
    switch (kind) {
        case FileKind::Epub:  return L"epub";
        case FileKind::Pdf:   return L"pdf";
        case FileKind::Image: return L"png";
        case FileKind::Markdown: break;
    }
    return L"md";
}

} // namespace

FileDialogResult openFileDialog(void* owner, const std::string& title, FileKind kind) {
    wchar_t file[MAX_PATH] = L"";
    const std::wstring wtitle = utf8ToWide(title);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(owner);
    ofn.lpstrFilter = filterFor(kind);
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wtitle.empty() ? nullptr : wtitle.c_str();
    // Le fichier doit exister ; NOCHANGEDIR pour ne pas perturber le
    // répertoire courant du processus (dont dépend le chargement de l'UI).
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    FileDialogResult result;
    if (GetOpenFileNameW(&ofn)) {
        result.ok = true;
        result.path = wideToUtf8(file);
    }
    return result;
}

FileDialogResult saveFileDialog(void* owner, const std::string& title,
                                const std::string& defaultName, FileKind kind) {
    wchar_t file[MAX_PATH] = L"";
    const std::wstring wdef = utf8ToWide(defaultName);
    // Nom proposé par défaut (copie bornée dans le tampon).
    for (size_t i = 0; i < wdef.size() && i + 1 < MAX_PATH; ++i) file[i] = wdef[i];

    const std::wstring wtitle = utf8ToWide(title);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(owner);
    ofn.lpstrFilter = filterFor(kind);
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = wtitle.empty() ? nullptr : wtitle.c_str();
    // Ajoute l'extension du format si l'utilisateur n'en tape pas.
    ofn.lpstrDefExt = defaultExtensionFor(kind);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    FileDialogResult result;
    if (GetSaveFileNameW(&ofn)) {
        result.ok = true;
        result.path = wideToUtf8(file);
    }
    return result;
}

FileDialogResult chooseFolderDialog(void* owner, const std::string& title) {
    // On utilise SHBrowseForFolder plutôt que IFileDialog (plus moderne) :
    // ce dernier exige _WIN32_WINNT >= 0x0600, or ce fichier cible
    // volontairement 0x0501 pour contourner le bug d'en-têtes du SDK
    // 10.0.26100 (voir en tête de fichier). SHBrowseForFolder est
    // disponible depuis toujours et fait le travail.
    const std::wstring wtitle = utf8ToWide(title);

    BROWSEINFOW bi{};
    bi.hwndOwner = static_cast<HWND>(owner);
    bi.lpszTitle = wtitle.empty() ? nullptr : wtitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    FileDialogResult result;
    LPITEMIDLIST idList = SHBrowseForFolderW(&bi);
    if (idList) {
        wchar_t path[MAX_PATH] = L"";
        if (SHGetPathFromIDListW(idList, path)) {
            result.ok = true;
            result.path = wideToUtf8(path);
        }
        CoTaskMemFree(idList); // libération imposée par l'API
    }
    return result;
}

UnsavedChoice confirmUnsavedChanges(void* owner) {
    const int answer = MessageBoxW(
        static_cast<HWND>(owner),
        L"Le manuscrit contient des modifications non enregistrées.\n\n"
        L"Voulez-vous les enregistrer avant de quitter ?",
        L"Modifications non enregistrées",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    switch (answer) {
        case IDYES: return UnsavedChoice::Save;
        case IDNO:  return UnsavedChoice::Discard;
        default:    return UnsavedChoice::Cancel; // IDCANCEL, fermeture du dialogue...
    }
}

} // namespace bridge

#else // Plateformes non Windows : à implémenter (NSOpenPanel, GTK...).

namespace bridge {

FileDialogResult openFileDialog(void*, const std::string&, FileKind) {
    return FileDialogResult{}; // ok == false : la couche UI se rabattra sur la saisie manuelle
}
FileDialogResult saveFileDialog(void*, const std::string&, const std::string&, FileKind) {
    return FileDialogResult{};
}
FileDialogResult chooseFolderDialog(void*, const std::string&) {
    return FileDialogResult{};
}
UnsavedChoice confirmUnsavedChanges(void*) {
    // Plateforme non gérée : on refuse de fermer plutôt que de risquer
    // de perdre le travail de l'auteur en silence.
    return UnsavedChoice::Cancel;
}

} // namespace bridge

#endif
