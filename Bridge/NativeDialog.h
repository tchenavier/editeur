// NativeDialog.h
// -----------------------------------------------------------------------
// Boîtes de dialogue natives « Ouvrir » / « Enregistrer sous ».
//
// La librairie webview n'en fournit pas : on s'appuie sur les dialogues
// natifs de l'OS (sur Windows : GetOpenFileName/GetSaveFileName de
// comdlg32). Isolé dans son propre module pour que le reste du code reste
// indépendant de la plateforme ; l'ajout de macOS/Linux se fera ici, sans
// toucher au pont ni au modèle.
//
// Les chemins sont échangés en UTF-8 (comme tout le reste du pont).
// -----------------------------------------------------------------------

#pragma once

#include <string>

namespace bridge {

struct FileDialogResult {
    bool ok = false;      // false si l'utilisateur a annulé (ou plateforme non gérée)
    std::string path;     // chemin choisi (UTF-8), significatif seulement si ok
};

// Nature du fichier attendu : détermine le filtre proposé et l'extension
// ajoutée d'office si l'utilisateur n'en tape pas. Un seul énuméré plutôt
// qu'une chaîne de filtre brute : les modules appelants n'ont pas à
// connaître le format Win32 (paires séparées par des \0), et la liste des
// formats du projet reste visible d'un coup d'œil.
enum class FileKind {
    Markdown,  // manuscrit .md
    Epub,      // livre exporté .epub
    Pdf,       // livre exporté .pdf
    Image,     // couverture / illustration (PNG, JPEG, GIF)
};

// 'owner' est le handle de fenêtre natif (HWND sur Windows), pour que le
// dialogue soit modal par rapport à la fenêtre de l'application. Peut être
// nul (dialogue sans parent).
FileDialogResult openFileDialog(void* owner, const std::string& title,
                                FileKind kind = FileKind::Markdown);
FileDialogResult saveFileDialog(void* owner, const std::string& title,
                                const std::string& defaultName,
                                FileKind kind = FileKind::Markdown);

// Choix d'un DOSSIER (bible narrative : les fiches sont rangées dans une
// arborescence, pas dans un fichier unique).
FileDialogResult chooseFolderDialog(void* owner, const std::string& title);

// Réponse de l'utilisateur quand il ferme l'application avec des
// modifications non enregistrées (cf. cadrage : « prévenir si le projet
// n'est pas sauvegardé et que l'utilisateur demande l'arrêt »).
enum class UnsavedChoice {
    Save,     // enregistrer puis quitter
    Discard,  // quitter sans enregistrer
    Cancel,   // ne pas quitter
};

// Demande confirmation avant de perdre des modifications. Sur une
// plateforme non gérée, retourne Cancel (le plus prudent : on ne ferme
// pas en silence).
UnsavedChoice confirmUnsavedChanges(void* owner);

} // namespace bridge
