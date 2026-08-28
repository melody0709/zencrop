@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

rem =============================================================================
rem ZenCrop build entry
rem   build.bat                 Incremental product build + exact runtime install
rem   build.bat --package       Build and create verified MSI + Portable artifacts
rem   build.bat --clean         Remove generated compile/runtime/test/log trees
rem   build.bat --rebuild       Clean, then build
rem   build.bat --stop-running  Accepted for compatibility; stopping is automatic
rem
rem CMake objects/tests: build\cmake\
rem Sole runnable tree:  build\run\x64-release\
rem Persistent data:     %%LOCALAPPDATA%%\ZenCrop\
rem =============================================================================

set "ZENCROP_CLEAN_ONLY=0"
set "ZENCROP_REBUILD=0"
set "ZENCROP_PACKAGE_ALL=0"
set "ZENCROP_PACKAGE_PORTABLE=0"
set "ZENCROP_PACKAGE_MSI=0"
set "ZENCROP_PACKAGE_MODE="
set "ZENCROP_REQUIRE_SIGNING=0"
set "ZENCROP_REJECT_CL=0"
set "BUILD_TREE=build\cmake"
set "RUNTIME_DIR=build\run\x64-release"
set "RUNTIME_ABS=%CD%\build\run\x64-release"
set "POWERSHELL_EXE=pwsh.exe"
for %%A in (%*) do (
    if /I "%%~A"=="--cl" set "ZENCROP_REJECT_CL=1"
    if /I "%%~A"=="--cmake" rem accepted; CMake is the only backend
    if /I "%%~A"=="--stop-running" rem accepted; repository runtime stopping is automatic
    if /I "%%~A"=="--clean" set "ZENCROP_CLEAN_ONLY=1"
    if /I "%%~A"=="--rebuild" set "ZENCROP_REBUILD=1"
    if /I "%%~A"=="--package" set "ZENCROP_PACKAGE_ALL=1"
    if /I "%%~A"=="--package-portable" set "ZENCROP_PACKAGE_PORTABLE=1"
    if /I "%%~A"=="--package-msi" set "ZENCROP_PACKAGE_MSI=1"
    if /I "%%~A"=="--require-signing" set "ZENCROP_REQUIRE_SIGNING=1"
    if /I "%%~A"=="/?" goto :usage
    if /I "%%~A"=="-h" goto :usage
    if /I "%%~A"=="--help" goto :usage
)

if "!ZENCROP_PACKAGE_ALL!"=="1" if "!ZENCROP_PACKAGE_PORTABLE!"=="1" goto :package_option_conflict
if "!ZENCROP_PACKAGE_ALL!"=="1" if "!ZENCROP_PACKAGE_MSI!"=="1" goto :package_option_conflict
if "!ZENCROP_PACKAGE_PORTABLE!"=="1" if "!ZENCROP_PACKAGE_MSI!"=="1" goto :package_option_conflict
if "!ZENCROP_PACKAGE_ALL!"=="1" set "ZENCROP_PACKAGE_MODE=All"
if "!ZENCROP_PACKAGE_PORTABLE!"=="1" set "ZENCROP_PACKAGE_MODE=Portable"
if "!ZENCROP_PACKAGE_MSI!"=="1" set "ZENCROP_PACKAGE_MODE=Msi"
if "!ZENCROP_REQUIRE_SIGNING!"=="1" if not defined ZENCROP_PACKAGE_MODE goto :signing_without_package

if "!ZENCROP_REJECT_CL!"=="1" (
    echo ERROR: build.bat --cl was removed. CMake is the only compile authority.
    echo Use: build.bat
    exit /b 2
)

where "!POWERSHELL_EXE!" >nul 2>&1
if errorlevel 1 (
    echo ERROR: PowerShell 7 not found on PATH: !POWERSHELL_EXE!
    exit /b 1
)

if "!ZENCROP_REQUIRE_SIGNING!"=="1" (
    call :check_signing_configuration
    if errorlevel 1 exit /b !ERRORLEVEL!
)

call :stop_runtime_instance
if errorlevel 1 exit /b !ERRORLEVEL!
call :validate_existing_layout
if errorlevel 1 exit /b !ERRORLEVEL!

if "!ZENCROP_CLEAN_ONLY!"=="1" (
    call :clean_generated
    exit /b !ERRORLEVEL!
)
if "!ZENCROP_REBUILD!"=="1" (
    call :clean_generated
    if errorlevel 1 exit /b !ERRORLEVEL!
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS_PATH=%%i"
) else (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
)

if not exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: Visual Studio 2022 not found at "!VS_PATH!"
    exit /b 1
)

call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b !ERRORLEVEL!

for /f "delims=" %%d in ('dir /b /ad /o-n "C:\Program Files (x86)\Windows Kits\10\Include\" 2^>nul') do (
    set "SDK_VER=%%d"
    goto :found_sdk
)
:found_sdk

if not defined SDK_VER (
    echo ERROR: Windows SDK not found.
    exit /b 1
)

echo Using SDK version: !SDK_VER!
echo Build backend: CMake/Ninja

set "SDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\!SDK_VER!"
set "SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\!SDK_VER!"
set "INCLUDE=!INCLUDE!;!SDK_INC!\ucrt;!SDK_INC!\um;!SDK_INC!\shared;!SDK_INC!\winrt;!SDK_INC!\cppwinrt"
set "LIB=!LIB!;!SDK_LIB!\ucrt\x64;!SDK_LIB!\um\x64"
set "PATH=!PATH!;C:\Program Files (x86)\Windows Kits\10\bin\!SDK_VER!\x64"

set "CMAKE_EXE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA_DIR=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if not exist "!CMAKE_EXE!" (
    echo ERROR: VS bundled CMake not found: !CMAKE_EXE!
    exit /b 1
)
if not exist "!NINJA_DIR!\ninja.exe" (
    echo ERROR: VS bundled Ninja not found: !NINJA_DIR!\ninja.exe
    exit /b 1
)
set "PATH=!NINJA_DIR!;!PATH!"

call :build_product
if errorlevel 1 exit /b !ERRORLEVEL!

call :read_product_version
if errorlevel 1 exit /b !ERRORLEVEL!

call :install_runtime
if errorlevel 1 exit /b !ERRORLEVEL!

call :write_layout_readme
if defined ZENCROP_PACKAGE_MODE (
    call :package_artifacts
    if errorlevel 1 exit /b !ERRORLEVEL!
)

echo Build Success
echo Runnable: !RUNTIME_DIR!\ZenCrop.exe
echo Manifest: !RUNTIME_DIR!\runtime-manifest.json
exit /b 0

:build_product
echo Configuring preset x64-release ^(binaryDir=!BUILD_TREE!^)...
"!CMAKE_EXE!" --preset x64-release
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

echo Building product target ZenCrop...
"!CMAKE_EXE!" --build --preset x64-release --target ZenCrop
if errorlevel 1 (
    echo ERROR: Product build failed.
    exit /b 1
)
if not exist "!BUILD_TREE!\ZenCrop.exe" (
    echo ERROR: !BUILD_TREE!\ZenCrop.exe is missing after the product build.
    exit /b 1
)
exit /b 0

:install_runtime
call :stop_runtime_instance
if errorlevel 1 exit /b !ERRORLEVEL!
echo Installing exact runtime payload into !RUNTIME_DIR!...
"!CMAKE_EXE!" --install "!BUILD_TREE!" --prefix "!RUNTIME_ABS!" --config Release --component Runtime
if errorlevel 1 (
    echo ERROR: Runtime install failed.
    echo Hint: close external tools holding files under !RUNTIME_DIR! and retry.
    exit /b 1
)
if not exist "!RUNTIME_DIR!\ZenCrop.exe" (
    echo ERROR: !RUNTIME_DIR!\ZenCrop.exe is missing after runtime install.
    exit /b 1
)
if not exist "!RUNTIME_DIR!\runtime-manifest.json" (
    echo ERROR: runtime-manifest.json is missing after runtime install.
    exit /b 1
)
call :validate_build_layout
if errorlevel 1 exit /b !ERRORLEVEL!
exit /b 0

:read_product_version
set "VERSION_FILE=!BUILD_TREE!\generated\zencrop-version.txt"
if not exist "!VERSION_FILE!" (
    echo ERROR: CMake did not generate !VERSION_FILE!.
    exit /b 1
)
set "VER="
for /f "usebackq delims=" %%v in ("!VERSION_FILE!") do set "VER=%%v"
if not defined VER (
    echo ERROR: Generated product version is empty: !VERSION_FILE!
    exit /b 1
)
exit /b 0

:check_signing_configuration
echo Checking production signing configuration before build...
"!POWERSHELL_EXE!" -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\package_zencrop.ps1" -CheckSigningConfiguration
if errorlevel 1 (
    echo ERROR: Production signing configuration is incomplete; no build or package was changed.
    exit /b 1
)
exit /b 0

:package_artifacts
call :validate_build_layout
if errorlevel 1 exit /b !ERRORLEVEL!
set "ZENCROP_SIGNING_ARGUMENT="
if "!ZENCROP_REQUIRE_SIGNING!"=="1" set "ZENCROP_SIGNING_ARGUMENT=-RequireSigning"
echo Packaging !ZENCROP_PACKAGE_MODE! payload from the installed runtime...
"!POWERSHELL_EXE!" -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\package_zencrop.ps1" -Mode "!ZENCROP_PACKAGE_MODE!" -BuildRoot "%CD%\build" -RuntimeDirectory "!RUNTIME_ABS!" -InstallManifest "%CD%\!BUILD_TREE!\install_manifest_Runtime.txt" -ProductVersion "!VER!" !ZENCROP_SIGNING_ARGUMENT!
if errorlevel 1 (
    echo ERROR: Package creation failed; existing packages were preserved.
    exit /b 1
)
call :validate_build_layout
if errorlevel 1 exit /b !ERRORLEVEL!
exit /b 0

:write_layout_readme
if not exist build mkdir build
(
    echo ZenCrop generated output
    echo.
    echo cmake\                    CMake cache, objects, and on-demand test binaries.
    echo run\x64-release\          Sole runnable development payload.
    echo artifacts\tests\         Generated test outputs.
    echo artifacts\diagnostics\   Generated diagnostics.
    echo logs\                     Explicit build/test logs.
    echo packages\                 Verified MSI and Portable package output only.
    echo.
    echo Persistent settings and OCR history live in %%LOCALAPPDATA%%\ZenCrop.
    echo Unknown build/runtime files fail validation instead of being deleted or packaged.
    echo WebView2 assets are verified against a manifest compiled into ZenCrop.exe.
) > build\README.txt
exit /b 0

:clean_generated
call :stop_runtime_instance
if errorlevel 1 exit /b !ERRORLEVEL!
echo Cleaning generated compile/runtime/test/log trees...
for %%D in ("build\cmake" "build\cmake-msvc" "build\run" "build\artifacts" "build\logs") do (
    if exist "%%~D" rmdir /S /Q "%%~D"
    if exist "%%~D" (
        echo ERROR: Failed to remove %%~D. Stop processes using that directory.
        exit /b 1
    )
)
if exist "build\README.txt" del /F /Q "build\README.txt"
echo Clean complete. build\packages was preserved.
exit /b 0

:stop_runtime_instance
"!POWERSHELL_EXE!" -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\stop_runtime_process.ps1" -ExecutablePath "!RUNTIME_ABS!\ZenCrop.exe"
exit /b !ERRORLEVEL!

:validate_existing_layout
if not exist "build" exit /b 0
if exist "!RUNTIME_DIR!" if exist "!BUILD_TREE!\install_manifest_Runtime.txt" (
    "!POWERSHELL_EXE!" -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\validate_build_layout.ps1" -BuildRoot "%CD%\build" -RuntimeDirectory "!RUNTIME_ABS!" -InstallManifest "%CD%\!BUILD_TREE!\install_manifest_Runtime.txt" -AllowIncompleteRuntime
    exit /b !ERRORLEVEL!
)
"!POWERSHELL_EXE!" -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\validate_build_layout.ps1" -BuildRoot "%CD%\build" -RuntimeDirectory "!RUNTIME_ABS!" -InstallManifest "%CD%\!BUILD_TREE!\install_manifest_Runtime.txt" -SkipRuntime
exit /b !ERRORLEVEL!

:validate_build_layout
"!POWERSHELL_EXE!" -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\validate_build_layout.ps1" -BuildRoot "%CD%\build" -RuntimeDirectory "!RUNTIME_ABS!" -InstallManifest "%CD%\!BUILD_TREE!\install_manifest_Runtime.txt"
exit /b !ERRORLEVEL!

:usage
echo Usage: build.bat [--cmake] [--stop-running] [--clean^|--rebuild] [--package^|--package-portable^|--package-msi] [--require-signing]
echo   default          Incremental ZenCrop build plus exact runtime install
echo   --stop-running   Compatibility flag; matching repository runtime stops automatically
echo   --clean          Remove generated trees; preserve build\packages
echo   --rebuild        Clean generated trees, then build
echo   --package-portable  Create or verify the high-compression Portable .7z from the installed runtime
echo   --package-msi       Create or verify the x64 per-machine MSI without installing it
echo   --package           Create or verify both MSI and Portable artifacts from one payload
echo   --require-signing   Require configured production signing before any product build
echo   --cl             REMOVED: CMake is the only compile authority
exit /b 0

:package_option_conflict
echo ERROR: Choose exactly one of --package, --package-portable, or --package-msi.
exit /b 2

:signing_without_package
echo ERROR: --require-signing requires --package, --package-portable, or --package-msi.
exit /b 2
