@echo off
REM ======================================================================
REM  LISTE DES SOURCES DU PROJET, partagee par tous les scripts.
REM
REM  POURQUOI CE FICHIER : chaque script (compilation de l'application,
REM  tests, audit de securite, campagne de fuzzing) enumerait sa propre
REM  liste de fichiers. Ajouter un module obligeait donc a penser a cinq
REM  endroits, et l'oubli ne se revelait qu'a l'execution, parfois apres
REM  plusieurs minutes de compilation. C'est arrive trois fois.
REM
REM  Ici, les sources sont DECOUVERTES, pas enumerees : ajouter un module
REM  ne demande plus de toucher a aucun script.
REM
REM  REGLE : tout *.cpp de core\, io\, utils\, net\ et Bridge\ EST une
REM  source, sauf :
REM    - test_*.cpp        : chaque suite de tests a sa propre liste
REM                          (voir build_tests.bat, et la remarque plus bas)
REM    - fuzz_parsers.cpp  : programme d'audit, compile a part
REM    - leak_check.cpp    : idem (exige le CRT de debogage /MDd)
REM    - main_webview.cpp  : point d'entree de l'application, la SEULE
REM                          piece qui connaisse webview ; les tests et
REM                          l'audit compilent tout le reste sans lui.
REM
REM  Variables produites (chemins RELATIFS a la racine du projet) :
REM    SRC_CORE, SRC_UTILS, SRC_IO, SRC_NET, SRC_BRIDGE, et SRC_ALL.
REM
REM  USAGE :   call "%~dp0sources.bat"
REM            cl ... %SRC_ALL% mon_programme.cpp
REM
REM  NOTE SUR LES TESTS UNITAIRES : ils gardent volontairement une liste
REM  minimale, et ce n'est pas un oubli. C'est ce qui PROUVE le decoupage
REM  en couches du cadrage : que core\ compile sans io\, et io\ sans le
REM  pont. Leur donner SRC_ALL ferait disparaitre cette garantie. Seule la
REM  suite d'integration (test_bridge) utilise SRC_ALL, puisqu'elle
REM  assemble tout par nature.
REM ======================================================================

if "%ROOT%"=="" set ROOT=%~dp0..

set SRC_CORE=
set SRC_UTILS=
set SRC_IO=
set SRC_NET=
set SRC_BRIDGE=

call :collecter SRC_CORE   core
call :collecter SRC_UTILS  utils
call :collecter SRC_IO     io
call :collecter SRC_NET    net
call :collecter SRC_BRIDGE Bridge

set SRC_ALL=%SRC_CORE% %SRC_UTILS% %SRC_IO% %SRC_NET% %SRC_BRIDGE%

REM Garde-fou : si la decouverte ne rend rien, tous les scripts
REM echoueraient plus loin avec des erreurs incomprehensibles.
if "%SRC_ALL%"=="    " goto :vide
if "%SRC_CORE%"==""  goto :vide
if "%SRC_IO%"==""    goto :vide
if "%SRC_NET%"==""   goto :vide
exit /b 0

REM ----------------------------------------------------------------------
REM  :collecter <nom_de_variable> <dossier>
REM  Ajoute a la variable tous les .cpp du dossier, sauf les exclusions.
REM  Le double filtre findstr fait le tri : /b "test_" ecarte les suites
REM  de tests par leur prefixe, /x ecarte les trois fichiers nommes.
REM ----------------------------------------------------------------------
:collecter
for /f "delims=" %%f in ('dir /b "%ROOT%\%~2\*.cpp" 2^>nul ^| findstr /v /i /b "test_" ^| findstr /v /i /x "fuzz_parsers.cpp leak_check.cpp main_webview.cpp"') do (
    call set "%~1=%%%~1%% %~2\%%f"
)
exit /b 0

:vide
echo *** sources.bat : aucune source trouvee dans %ROOT% ***
echo Verifiez que le projet est complet ^(dossiers core, io, utils, net, Bridge^).
exit /b 1
