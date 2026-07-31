@echo off
REM ======================================================================
REM  CAMPAGNE DE FUZZING LONGUE (sous AddressSanitizer).
REM
REM  L'audit de sécurité lance 40 000 entrées : assez pour qu'il reste
REM  rapide et lançable à chaque jalon. Ce script-ci sert aux campagnes de
REM  fond, à lancer de temps en temps (ou avant une diffusion) : des
REM  millions d'entrées, donc des combinaisons que la passe courte
REM  n'atteint jamais.
REM
REM  USAGE :
REM      scripts\fuzz_long.bat                  REM 2 000 000, graine du jour
REM      scripts\fuzz_long.bat 5000000          REM nombre choisi
REM      scripts\fuzz_long.bat 5000000 987      REM nombre + graine
REM
REM  Compter environ 2 ms par entrée (ASan ralentit beaucoup, c'est le
REM  prix de la détection) : 2 000 000 d'entrées ≈ 70 minutes. Mesuré, pas
REM  estimé — et à REMESURER quand on ajoute un analyseur au fuzzer :
REM  l'arrivée de la comparaison et du redécoupage a fait passer le coût
REM  de 1,5 à 3,4 ms avant qu'on ne les limite à une itération sur quatre.
REM
REM  EN CAS DE PLANTAGE : la graine est affichée au démarrage et rappelée
REM  à la fin. Rejouer EXACTEMENT la même séquence avec :
REM      build\security\fuzz.exe <iterations> <graine>
REM  (depuis un invite « x64 Native Tools », sinon la DLL d'ASan est
REM  introuvable et le programme s'arrête avec 0xC0000135).
REM ======================================================================

setlocal
set ITER=%1
set SEED=%2
if "%ITER%"=="" set ITER=2000000
if "%SEED%"=="" set SEED=20260726

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if %errorlevel% neq 0 exit /b 1
cd /d "%~dp0.."
set OUT=%~dp0..\build\security
if not exist "%OUT%" mkdir "%OUT%"
set INC=/Icore /Iio /Iutils /IBridge

REM Sources partagees avec les autres scripts (voir sources.bat).
call "%~dp0sources.bat"
if %errorlevel% neq 0 exit /b 1

echo === Compilation du fuzzer sous AddressSanitizer ===
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /Zi /fsanitize=address %INC% ^
   %SRC_ALL% core\fuzz_parsers.cpp /Fo"%OUT%\\" /Fe"%OUT%\fuzz.exe" >nul
if %errorlevel% neq 0 ( echo   *** ECHEC de compilation *** & exit /b 1 )

echo === Campagne : %ITER% entrees, graine %SEED% ===
"%OUT%\fuzz.exe" %ITER% %SEED%
REM Test du code de sortie avec « neq 0 » et NON « if errorlevel 1 » : un
REM assert ou un arret ASan renvoie 0xC0000409, valeur NEGATIVE en signe,
REM que « if errorlevel 1 » ne detecterait pas.
if %errorlevel% neq 0 (
    echo.
    echo *** ECHEC : plantage ou erreur memoire detectee ***
    echo *** Rejouer avec : build\security\fuzz.exe %ITER% %SEED% ***
    exit /b 1
)

echo.
echo === CAMPAGNE TERMINEE SANS INCIDENT ===
exit /b 0
