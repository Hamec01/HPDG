@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ================================================================
REM  DRUMENGINE / HPDG - VST3 build script (JUCE + CMake + MSVC)
REM
REM  Usage:
REM    build_vst.bat            - Release build + install to VST3 folder
REM    build_vst.bat debug      - Debug build
REM    build_vst.bat clean      - delete build folder and rebuild
REM    build_vst.bat nocopy     - build only, don't install
REM    (arguments can be combined: build_vst.bat clean debug)
REM ================================================================

REM ---------- Settings (edit if needed) ----------
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%PROJECT_DIR%\build"
REM Target name from juce_add_plugin(...) + "_VST3"
set "PLUGIN=HPDG"
set "TARGET=%PLUGIN%_VST3"
set "CONFIG=Release"
set "VST3_DEST=%CommonProgramFiles%\VST3"
REM -----------------------------------------------

set "DO_CLEAN=0"
set "NO_COPY=0"
for %%A in (%*) do (
    if /I "%%A"=="clean"  set "DO_CLEAN=1"
    if /I "%%A"=="debug"  set "CONFIG=Debug"
    if /I "%%A"=="nocopy" set "NO_COPY=1"
)

echo.
echo ===== DRUMENGINE VST3 build [%CONFIG%] =====
echo Project: %PROJECT_DIR%
echo.

REM ---------- Checks ----------
where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake not found in PATH. Install CMake or add it to PATH.
    goto :fail
)
if not exist "%PROJECT_DIR%\CMakeLists.txt" (
    echo [ERROR] CMakeLists.txt not found next to this script.
    echo         Put build_vst.bat in the project root.
    goto :fail
)

REM ---------- Clean ----------
if "%DO_CLEAN%"=="1" (
    echo [1/4] Cleaning build folder...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
) else (
    echo [1/4] Clean skipped
)

REM ---------- Configure ----------
echo [2/4] Configuring CMake...
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -A x64
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    goto :fail
)

REM ---------- Build ----------
echo [3/4] Building %TARGET% [%CONFIG%]...
cmake --build "%BUILD_DIR%" --config %CONFIG% --target %TARGET% --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    goto :fail
)

REM ---------- Find the .vst3 bundle ----------
set "VST3_SRC=%BUILD_DIR%\%PLUGIN%_artefacts\%CONFIG%\VST3\%PLUGIN%.vst3"
if not exist "%VST3_SRC%\" (
    echo [ERROR] Built .vst3 bundle not found: %VST3_SRC%
    goto :fail
)
for %%F in ("%VST3_SRC%") do set "VST3_NAME=%%~nxF"
echo Built: %VST3_SRC%

REM ---------- Install ----------
if "%NO_COPY%"=="1" (
    echo [4/4] Install skipped ^(nocopy^)
    goto :done
)
echo [4/4] Installing to %VST3_DEST%\%VST3_NAME% ...
robocopy "%VST3_SRC%" "%VST3_DEST%\%VST3_NAME%" /MIR /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo [WARNING] Could not copy the plugin.
    echo           Run this script as Administrator and close the DAW
    echo           ^(it locks the .vst3 file while loaded^).
    goto :fail
)

:done
echo.
echo ===== SUCCESS =====
echo Rescan plugins in your DAW to pick up the new build.
echo.
pause
exit /b 0

:fail
echo.
echo ===== BUILD FAILED =====
echo.
pause
exit /b 1
