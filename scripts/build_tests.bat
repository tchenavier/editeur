@echo off
REM Compile et exécute toutes les suites de tests du projet éditeur.
REM
REM IMPORTANT : on teste le code de sortie avec « if %errorlevel% neq 0 » et
REM NON avec « if errorlevel 1 ». Un assert() qui échoue termine le processus
REM avec 0xC0000409, valeur NÉGATIVE en signé : « if errorlevel 1 » (test
REM signé >= 1) ne la détecte pas et laisserait passer un test en échec.

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if %errorlevel% neq 0 exit /b 1
cd /d "C:\Users\Dev\Documents\projet\editeur"
set OUT=%~dp0..\build\tests
if not exist "%OUT%" mkdir "%OUT%"
set INC=/Icore /Iio /Iutils /Inet /IBridge
set FLAGS=/nologo /std:c++17 /utf-8 /EHsc /W4

REM Sources partagees (voir sources.bat) : utilisees par la seule suite
REM d'INTEGRATION, test_bridge, qui assemble tout par nature.
REM
REM Les suites UNITAIRES gardent volontairement une liste minimale. Ce
REM n'est pas un oubli : c'est ce qui prouve le decoupage en couches du
REM cadrage — que core\ compile sans io\, et io\ sans le pont. Leur
REM donner SRC_ALL ferait disparaitre cette garantie, et triplerait le
REM temps de compilation des tests.
call "%~dp0sources.bat"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_model ===
cl %FLAGS% %INC% core\TextModel.cpp core\test_model.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_model.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_blocks ===
cl %FLAGS% %INC% core\TextModel.cpp core\test_blocks.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_blocks.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_undo ===
cl %FLAGS% %INC% core\TextModel.cpp core\UndoManager.cpp core\test_undo.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_undo.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_search ===
cl %FLAGS% %INC% core\TextModel.cpp core\Search.cpp core\test_search.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_search.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_cards ===
cl %FLAGS% %INC% core\TextModel.cpp core\Search.cpp core\CardModel.cpp utils\Utf8.cpp core\test_cards.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_cards.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_cardio ===
cl %FLAGS% %INC% core\CardModel.cpp core\Search.cpp core\TextModel.cpp ^
   utils\Utf8.cpp utils\PathUtf8.cpp io\Yaml.cpp io\CardIO.cpp io\test_cardio.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_cardio.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_markdown ===
cl %FLAGS% %INC% core\TextModel.cpp utils\Utf8.cpp utils\PathUtf8.cpp utils\Uuid.cpp ^
   io\MarkdownIO.cpp io\test_markdown.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_markdown.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_htmltext ===
cl %FLAGS% %INC% core\TextModel.cpp utils\Utf8.cpp io\HtmlText.cpp io\test_htmltext.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_htmltext.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_zip ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp io\Zip.cpp io\test_zip.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_zip.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_epub ===
cl %FLAGS% %INC% core\TextModel.cpp core\Search.cpp core\CardModel.cpp ^
   utils\Utf8.cpp utils\PathUtf8.cpp io\HtmlText.cpp io\Zip.cpp io\Book.cpp io\Epub.cpp ^
   io\test_epub.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_epub.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_utils ===
cl %FLAGS% %INC% utils\Sha256.cpp utils\Uuid.cpp utils\test_utils.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_utils.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_project ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp utils\Sha256.cpp ^
   io\ProjectIndex.cpp io\ChangeLog.cpp io\Pairing.cpp io\test_project.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_project.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_speller ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp ^
   io\AffixDictionary.cpp io\Thesaurus.cpp core\Speller.cpp core\test_speller.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_speller.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_diff ===
cl %FLAGS% %INC% utils\Utf8.cpp core\Speller.cpp core\Diff.cpp core\test_diff.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_diff.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_reflow ===
cl %FLAGS% %INC% utils\Utf8.cpp core\TextModel.cpp core\Speller.cpp core\Diff.cpp ^
   core\Reflow.cpp core\test_reflow.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_reflow.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_roadmap ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp io\Yaml.cpp io\Roadmap.cpp ^
   io\test_roadmap.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_roadmap.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_sync ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp utils\Sha256.cpp ^
   io\ProjectIndex.cpp io\SyncPlan.cpp io\test_sync.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_sync.exe"
if %errorlevel% neq 0 exit /b 1

REM test_net compile la couche reseau SANS le pont ni le metier : c'est ce
REM qui prouve que le client de protocole ne depend que du transport et de
REM l'inventaire, jamais de l'interface.
echo === Compilation test_net ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp utils\Sha256.cpp ^
   io\ProjectIndex.cpp net\Transport.cpp net\SyncClient.cpp net\test_net.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_net.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_runner ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp utils\Sha256.cpp ^
   io\ProjectIndex.cpp io\SyncPlan.cpp net\Transport.cpp net\SyncClient.cpp ^
   net\SyncRunner.cpp net\DiskFileStore.cpp net\test_runner.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_runner.exe"
if %errorlevel% neq 0 exit /b 1

REM test_http est la seule suite qui ouvre de vraies connexions : elle
REM lance son propre serveur jouet sur 127.0.0.1, sur un port attribue par
REM le systeme. Rien ne sort de la machine.
echo === Compilation test_http ===
cl %FLAGS% %INC% utils\Utf8.cpp utils\PathUtf8.cpp utils\Sha256.cpp ^
   io\ProjectIndex.cpp net\Transport.cpp net\SyncClient.cpp net\WinHttpTransport.cpp ^
   net\test_http.cpp /Fo"%OUT%\\" /Fe"%OUT%\test_http.exe"
if %errorlevel% neq 0 exit /b 1

echo === Compilation test_bridge ===
cl %FLAGS% %INC% %SRC_ALL% Bridge\test_bridge.cpp ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_bridge.exe"
if %errorlevel% neq 0 exit /b 1

echo.
echo === Execution ===
call :run test_model
call :run test_blocks
call :run test_undo
call :run test_search
call :run test_cards
call :run test_cardio
call :run test_markdown
call :run test_htmltext
call :run test_zip
call :run test_epub
call :run test_utils
call :run test_project
call :run test_speller
call :run test_diff
call :run test_reflow
call :run test_roadmap
call :run test_sync
call :run test_net
call :run test_runner
call :run test_http
call :run test_bridge

echo.
echo === TOUS LES TESTS SONT PASSES ===
exit /b 0

:run
"%OUT%\%~1.exe"
if %errorlevel% neq 0 (
    echo.
    echo *** ECHEC : %~1 ^(code %errorlevel%^) ***
    exit 1
)
exit /b 0
