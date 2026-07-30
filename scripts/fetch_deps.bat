@echo off
REM ======================================================================
REM  Récupération des dépendances nécessaires pour COMPILER l'application.
REM
REM  Deux dépendances, toutes deux constituées d'en-têtes uniquement :
REM
REM   1. webview/webview (licence MIT) : un header C++ autonome qui pilote
REM      le moteur de rendu déjà présent sur l'OS. Aucune bibliothèque à
REM      lier : il déclare lui-même celles dont il a besoin (#pragma
REM      comment(lib, ...)) et charge WebView2Loader.dll dynamiquement.
REM
REM   2. SDK WebView2 (Microsoft, paquet NuGet) : seuls les EN-TÊTES sont
REM      nécessaires (WebView2.h et WebView2EnvironmentOptions.h), inclus
REM      par webview.h. Un paquet NuGet n'est qu'une archive ZIP : pas
REM      besoin d'installer nuget.exe.
REM
REM  Le RUNTIME WebView2 (ce qui fait tourner l'application chez
REM  l'utilisateur final) est déjà présent sur Windows 11 et fourni avec
REM  Microsoft Edge. Rien à installer pour lui.
REM
REM  Tout atterrit dans third_party\, qui n'a pas vocation à être versionné.
REM ======================================================================

setlocal
set ROOT=%~dp0..
set TP=%ROOT%\third_party

REM --- Versions épinglées (vérifiées le 2026-07-20) ---------------------
REM Épingler évite qu'une mise à jour amont casse la compilation sans
REM prévenir. Pour monter de version, changez ces deux lignes et
REM recompilez.
set WV_TAG=0.12.0
set WV2_VER=1.0.4078.44
REM Dictionnaires francais (correcteur et synonymes) : depot LibreOffice,
REM epingle sur un commit precis. Ce sont des DONNEES, pas du code.
REM   fr.aff + fr.dic  : orthographe, Grammalecte v7.7, licence MPL 2.0
REM   thes_fr.dat      : synonymes, format MyThes, licence LGPL 2.1+
set DICT_SHA=8a58c570f0e9cb1d908db15c878c1f371a793d8b

echo === Dependances de l'editeur ===
echo   webview      : %WV_TAG%
echo   SDK WebView2 : %WV2_VER%
echo   Dictionnaires: LibreOffice @ %DICT_SHA:~0,7%
echo   Destination  : %TP%
echo.

if not exist "%TP%\webview"  mkdir "%TP%\webview"
if not exist "%TP%\webview2" mkdir "%TP%\webview2"
if not exist "%TP%\dictionnaires" mkdir "%TP%\dictionnaires"

REM --- 1. webview.h ------------------------------------------------------
REM Depuis la version 0.11, le header vit sous core/include/webview/ ;
REM il reste autonome (il n'inclut que des en-têtes standard et système).
echo [1/2] Telechargement de webview.h...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$u='https://raw.githubusercontent.com/webview/webview/%WV_TAG%/core/include/webview/webview.h';" ^
  "Invoke-WebRequest -Uri $u -OutFile '%TP%\webview\webview.h' -UseBasicParsing"
if %errorlevel% neq 0 goto :echec
if not exist "%TP%\webview\webview.h" goto :echec

REM --- 2. En-têtes du SDK WebView2 ---------------------------------------
echo [2/2] Telechargement du SDK WebView2 (archive NuGet)...
set WV2ZIP=%TEMP%\microsoft.web.webview2.%WV2_VER%.zip
set WV2DIR=%TEMP%\webview2_sdk_%WV2_VER%
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$u='https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/%WV2_VER%/microsoft.web.webview2.%WV2_VER%.nupkg';" ^
  "Invoke-WebRequest -Uri $u -OutFile '%WV2ZIP%' -UseBasicParsing;" ^
  "if (Test-Path '%WV2DIR%') { Remove-Item -Recurse -Force '%WV2DIR%' };" ^
  "Expand-Archive -Path '%WV2ZIP%' -DestinationPath '%WV2DIR%' -Force;" ^
  "Copy-Item -Force '%WV2DIR%\build\native\include\*.h' '%TP%\webview2\'"
if %errorlevel% neq 0 goto :echec
if not exist "%TP%\webview2\WebView2.h" goto :echec

REM --- 3. Dictionnaires francais -----------------------------------------
REM Le correcteur fonctionne sans eux (il ne signale simplement rien) :
REM leur absence n'empeche donc pas de compiler ni d'ecrire.
echo [3/3] Telechargement des dictionnaires francais...
set DICTURL=https://raw.githubusercontent.com/LibreOffice/dictionaries/%DICT_SHA%/fr_FR/dictionaries
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "foreach ($f in 'fr.aff','fr.dic','thes_fr.dat','README_dict_fr.txt','README_thes_fr.txt') {" ^
  "  Invoke-WebRequest -Uri ('%DICTURL%/' + $f) -OutFile ('%TP%\dictionnaires\' + $f) -UseBasicParsing }"
if %errorlevel% neq 0 goto :echec_dico
if not exist "%TP%\dictionnaires\fr.dic" goto :echec_dico

echo.
echo === Dependances pretes ===
dir /b "%TP%\webview" "%TP%\webview2" "%TP%\dictionnaires"
echo.
echo Vous pouvez maintenant lancer scripts\build_app.bat
exit /b 0

:echec_dico
echo.
echo *** Dictionnaires non recuperes ***
echo L'editeur fonctionne quand meme : le correcteur restera simplement
echo inactif. Pour les installer a la main, copier dans
echo third_party\dictionnaires\ les fichiers fr.aff, fr.dic et thes_fr.dat
echo depuis %DICTURL%
exit /b 0

:echec
echo.
echo *** ECHEC de la recuperation des dependances ***
echo Verifiez votre connexion reseau, ou telechargez manuellement :
echo   - https://raw.githubusercontent.com/webview/webview/%WV_TAG%/core/include/webview/webview.h
echo     -^> a placer dans third_party\webview\webview.h
echo   - https://www.nuget.org/packages/Microsoft.Web.WebView2/%WV2_VER%
echo     -^> renommer en .zip, extraire, copier build\native\include\*.h
echo        dans third_party\webview2\
exit /b 1
