@echo off
REM ======================================================================
REM  Compilation de l'APPLICATION (editeur.exe).
REM
REM  Prérequis : avoir lancé scripts\fetch_deps.bat au moins une fois.
REM
REM  À noter : ce script assemble les mêmes fichiers que les tests, plus
REM  main_webview.cpp (le seul qui connaisse webview). Les couches core/,
REM  io/ et utils/ restent compilables sans aucune dépendance graphique :
REM  c'est tout l'intérêt du découpage, et build_tests.bat le vérifie.
REM
REM  Le sous-système reste « console » : une fenêtre de console s'ouvre à
REM  côté de l'application. C'est volontaire pendant le développement (on
REM  y voit les messages). Pour une version distribuée, ajouter
REM  /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup
REM ======================================================================

setlocal
set ROOT=%~dp0..
set TP=%ROOT%\third_party
set OUT=%ROOT%\build

REM --- Vérification des dépendances -------------------------------------
if not exist "%TP%\webview\webview.h" goto :manque
if not exist "%TP%\webview2\WebView2.h" goto :manque

REM --- Environnement de compilation MSVC --------------------------------
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if %errorlevel% neq 0 (
    echo *** Visual Studio introuvable : ajustez le chemin de vcvars64.bat ***
    exit /b 1
)

cd /d "%ROOT%"
if not exist "%OUT%" mkdir "%OUT%"

REM Liste des sources : decouverte automatique, partagee avec les autres
REM scripts (voir sources.bat). Ajouter un module ne demande rien ici.
call "%~dp0sources.bat"
if %errorlevel% neq 0 exit /b 1

REM /guard:cf = Control Flow Guard : le compilateur et l'editeur de liens
REM verifient a l'execution que chaque appel indirect vise bien une fonction
REM legitime. C'est une protection en profondeur : si une faille de lecture
REM de fichier permettait un jour d'ecraser un pointeur de fonction, le
REM detournement serait bloque net. Cout negligeable, et l'application lit
REM des fichiers qui ne viennent pas forcement de l'auteur.
set FLAGS=/nologo /std:c++17 /utf-8 /EHsc /W4 /guard:cf
set INC=/Icore /Iio /Iutils /IBridge /I"%TP%\webview" /I"%TP%\webview2"

echo === Compilation de l'application ===
cl %FLAGS% %INC% %SRC_ALL% Bridge\main_webview.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\editeur.exe" ^
   /link /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
if %errorlevel% neq 0 (
    echo.
    echo *** ECHEC de la compilation ***
    exit /b 1
)

echo.
echo === Compilation reussie : %OUT%\editeur.exe ===
echo.
echo IMPORTANT : lancez l'application depuis la RACINE du projet, sinon
echo elle ne trouvera pas ui\index.html et se rabattra sur une page de
echo demonstration minimale :
echo     cd /d "%ROOT%"
echo     build\editeur.exe
exit /b 0

:manque
echo *** Dependances absentes ***
echo Lancez d'abord : scripts\fetch_deps.bat
exit /b 1
