@echo off
rem ============================================================================
rem  compile.cmd - rebuild AltTab.exe for the requested x64 configuration(s).
rem
rem  Usage:
rem    compile.cmd                 Build Release | x64  (default)
rem    compile.cmd Debug           Build Debug | x64
rem    compile.cmd ReleaseNoLogger Build ReleaseNoLogger | x64
rem    compile.cmd all             Build all three x64 configurations
rem
rem  MSBuild is located automatically (vswhere, with a fallback to the known
rem  Visual Studio 2022 install paths). The newest installed C++ PlatformToolset
rem  is used, so the script also works on machines that only ship newer build
rem  tools than the project's default (v143).
rem ============================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "CONFIG=Release"
if /I "%~1"=="all"                 set "CONFIG=all"
if /I "%~1"=="debug"               set "CONFIG=Debug"
if /I "%~1"=="releasenologger"     set "CONFIG=ReleaseNoLogger"
if not "%~1"=="" if /I not "%~1"=="all" if /I not "%~1"=="debug" if /I not "%~1"=="releasenologger" (
    echo [WARNING] Unknown argument "%~1" - building Release ^| x64.
)

rem ---------------------------------------------------------------------------
rem Locate MSBuild.exe
rem ---------------------------------------------------------------------------
set "MSBUILD="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$vs = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'; if (Test-Path $vs -PathType Leaf) { $p = & $vs -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null | Select-Object -First 1; if ($p) { $p } }"`) do set "MSBUILD=%%i"

if not defined MSBUILD (
    for %%m in (
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    ) do (
        if not defined MSBUILD if exist "%%~m" set "MSBUILD=%%~m"
    )
)

rem The amd64-hosted MSBuild is faster for x64 code, prefer it when present.
set "MSBUILD_AMD64="
if defined MSBUILD (
    for %%m in ("%MSBUILD%") do set "MSBUILD_AMD64=%%~dpmamd64\MSBuild.exe"
)
if defined MSBUILD_AMD64 if exist "%MSBUILD_AMD64%" set "MSBUILD=%MSBUILD_AMD64%"

if not defined MSBUILD (
    echo [ERROR] MSBuild.exe not found. Please install Visual Studio Build Tools.
    exit /b 1
)
echo Using MSBuild: %MSBUILD%

rem ---------------------------------------------------------------------------
rem Locate the newest installed C++ PlatformToolset
rem ---------------------------------------------------------------------------
set "TOOLSET="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$root = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio'; $t = Get-ChildItem $root -Directory -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Parent.Name -eq 'PlatformToolsets' -and $_.Name -match '^v\d+$' -and (Test-Path (Join-Path $_.FullName 'Toolset.props')) } | Sort-Object { [int]$_.Name.Substring(1) } -Descending | Select-Object -First 1; if ($t) { $t.Name }"`) do set "TOOLSET=%%i"

if defined TOOLSET (
    echo Using PlatformToolset: %TOOLSET%
) else (
    echo Using default PlatformToolset from AltTab.sln
)

rem ---------------------------------------------------------------------------
rem Build
rem ---------------------------------------------------------------------------
set "FAILED=0"

if /I "%CONFIG%"=="all" (
    set "CONFIGS=Release Debug ReleaseNoLogger"
) else (
    set "CONFIGS=%CONFIG%"
)

for %%c in (%CONFIGS%) do (
    echo.
    echo ============ Building %%c ^| x64 ============
    if defined TOOLSET (
        "%MSBUILD%" /m /p:Configuration=%%c /p:Platform=x64 /p:PlatformToolset=%TOOLSET% AltTab.sln
    ) else (
        "%MSBUILD%" /m /p:Configuration=%%c /p:Platform=x64 AltTab.sln
    )
    if errorlevel 1 (
        echo [ERROR] Build failed for %%c ^| x64.
        set "FAILED=1"
    )
)

if "%FAILED%"=="1" (
    echo.
    echo ============ BUILD FAILED ============
    pause
    exit /b 1
)

echo.
echo ============ Done. Executables are written under the x64 folder. ============
endlocal
exit /b 0