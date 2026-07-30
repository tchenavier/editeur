@echo off
REM ======================================================================
REM  Audit de sécurité du socle métier (à relancer avant chaque jalon).
REM
REM  Couvre les exigences de sécurité du cadrage :
REM   1. Compilation stricte : /W4 /WX /permissive- (avertissements = erreurs).
REM   2. AddressSanitizer (/fsanitize=address) sur toute la suite du pont :
REM      débordements de tampon, use-after-free, accès hors limites.
REM   3. Fuzzing des analyseurs ET de la chaîne d'export sous ASan :
REM      40 000 entrées malformées, la principale surface d'entrée non
REM      fiable (fichiers .md ouverts, noms d'entrées d'archive).
REM   4. AddressSanitizer sur l'export (ZIP/EPUB) : code à décalages et
REM      tailles binaires, où une erreur d'un octet ne se voit pas.
REM   5. Détection de fuites mémoire (tas de débogage du CRT, /MDd),
REM      pont compris, chemins d'erreur inclus.
REM   6. Analyse statique (/analyze) : défauts prouvables sur des chemins
REM      d'exécution que les tests n'atteignent pas forcément.
REM
REM  N'inclut PAS le code Win32 des dialogues (ouvre de vraies fenêtres) ni
REM  main_webview (nécessite le SDK webview) : ces parties sont revues à la
REM  main. La sécurité anti-XSS de l'interface (aucun innerHTML, tout en
REM  textContent) se vérifie côté ui/index.html.
REM ======================================================================

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if %errorlevel% neq 0 exit /b 1
cd /d "%~dp0.."
set OUT=%~dp0..\build\security
if not exist "%OUT%" mkdir "%OUT%"
set INC=/Icore /Iio /Iutils /IBridge

REM Sources : decouvertes automatiquement et partagees avec les autres
REM scripts (voir sources.bat). Rien a mettre a jour ici quand un module
REM est ajoute.
call "%~dp0sources.bat"
if %errorlevel% neq 0 exit /b 1

echo === 1. Compilation stricte (/W4 /WX /permissive-) ===
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /permissive- %INC% ^
   %SRC_ALL% Bridge\test_bridge.cpp /Fo"%OUT%\\" /Fe"%OUT%\strict.exe" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC : avertissement ou erreur *** & exit /b 1 )
"%OUT%\strict.exe" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC execution *** & exit /b 1 )

REM Les deux programmes d'audit sont compiles ICI, sans edition de liens,
REM uniquement pour que leurs erreurs tombent TOUT DE SUITE. Sans cela, un
REM simple #include manquant dans l'un d'eux ne se revelait qu'aux etapes
REM 3 ou 5, apres plusieurs minutes de compilation sous sanitizer. C'est
REM arrive trois fois ; trente secondes valent mieux que huit minutes.
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /permissive- /c %INC% ^
   core\fuzz_parsers.cpp /Fo"%OUT%\\" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC : fuzz_parsers.cpp ne compile pas *** & exit /b 1 )
REM leak_check.cpp exige le CRT de DEBOGAGE : sans /MDd, les fonctions
REM _CrtMemCheckpoint ne sont meme pas declarees.
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /MDd /c %INC% ^
   core\leak_check.cpp /Fo"%OUT%\\" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC : leak_check.cpp ne compile pas *** & exit /b 1 )
echo   OK

echo === 2. AddressSanitizer sur la suite du pont ===
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /Zi /fsanitize=address %INC% ^
   %SRC_ALL% Bridge\test_bridge.cpp /Fo"%OUT%\\" /Fe"%OUT%\asan.exe" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC compilation ASan *** & exit /b 1 )
"%OUT%\asan.exe" >nul
if %errorlevel% neq 0 ( echo   *** ASan a signale une erreur memoire *** & exit /b 1 )
echo   OK

echo === 3. Fuzzing des analyseurs sous ASan (40 000 entrees) ===
REM Le fuzzer n'a pas besoin du pont, mais lui donner SRC_ALL evite d'avoir
REM a maintenir une liste de plus : le compilateur ecarte ce qui ne sert pas.
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /Zi /fsanitize=address %INC% ^
   %SRC_ALL% core\fuzz_parsers.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\fuzz.exe" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC compilation fuzz *** & exit /b 1 )
"%OUT%\fuzz.exe"
if %errorlevel% neq 0 ( echo   *** Fuzz : plantage ou erreur memoire *** & exit /b 1 )

echo === 4. AddressSanitizer sur l'export (archive, livre, EPUB) ===
REM Le code d'export manipule des DECALAGES et des tailles binaires (en-tetes
REM ZIP, CRC) : c'est exactement le genre de code ou une erreur d'un octet ne
REM se voit pas dans le resultat. On le passe donc aussi sous ASan.
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /Zi /fsanitize=address %INC% ^
   core\TextModel.cpp core\Search.cpp core\CardModel.cpp ^
   utils\Utf8.cpp utils\PathUtf8.cpp io\HtmlText.cpp io\Zip.cpp io\Book.cpp io\Epub.cpp ^
   io\test_epub.cpp /Fo"%OUT%\\" /Fe"%OUT%\asan_epub.exe" >nul
REM ATTENTION : jamais de parentheses dans un message entre ( ... ) — cmd
REM les prend pour la fin du bloc et le script casse a l'analyse, meme
REM quand la condition est fausse.
if %errorlevel% neq 0 ( echo   *** ECHEC compilation ASan export *** & exit /b 1 )
"%OUT%\asan_epub.exe" >nul
if %errorlevel% neq 0 ( echo   *** ASan a signale une erreur memoire dans l'export *** & exit /b 1 )
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /Zi /fsanitize=address %INC% ^
   utils\Utf8.cpp utils\PathUtf8.cpp io\Zip.cpp io\test_zip.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\asan_zip.exe" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC compilation ASan zip *** & exit /b 1 )
"%OUT%\asan_zip.exe" >nul
if %errorlevel% neq 0 ( echo   *** ASan a signale une erreur memoire dans le zip *** & exit /b 1 )
echo   OK

echo === 5. Detection de fuites (tas de debogage CRT, /MDd) ===
REM Couvre TOUTES les couches, pont compris : c'est la seule verification qui
REM exerce les commandes de bout en bout, chemins d'erreur inclus.
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /MDd %INC% ^
   %SRC_ALL% core\leak_check.cpp /Fo"%OUT%\\" /Fe"%OUT%\leak.exe" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC compilation leak_check *** & exit /b 1 )
"%OUT%\leak.exe"
if %errorlevel% neq 0 ( echo   *** FUITE MEMOIRE DETECTEE *** & exit /b 1 )

echo === 6. Analyse statique du code (/analyze) ===
REM L'analyseur de MSVC raisonne sur les CHEMINS d'execution : il attrape des
REM defauts qu'aucun test n'exerce forcement (lecture de variable non
REM initialisee, depassement de tampon prouvable, dereferencement nul).
REM Complementaire des sanitizers, qui n'observent QUE ce que le test execute.
REM /external:W0 : on n'analyse pas les en-tetes de la bibliotheque standard.
REM /WX : le moindre signalement fait echouer l'audit.
if not exist "%OUT%\analyze" mkdir "%OUT%\analyze"
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /analyze /analyze:only ^
   /external:anglebrackets /external:W0 /analyze:external- %INC% ^
   %SRC_ALL% /Fo"%OUT%\analyze\\" >nul
if %errorlevel% neq 0 ( echo   *** L'analyseur statique a signale un probleme *** & exit /b 1 )
echo   OK

echo.
echo === AUDIT DE SECURITE : TOUT EST AU VERT ===
exit /b 0
