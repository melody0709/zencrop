@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0\.."

rem =============================================================================
rem CTest wrapper. Product builds no longer compile test executables; this
rem wrapper configures the shared tree and builds test targets on demand.
rem
rem   tests\build_and_run.bat                  Build tests, run hermetic label
rem   tests\build_and_run.bat all              Build tests, run all CTest entries
rem   tests\build_and_run.bat runtime          Build tests, run runtime label
rem   tests\build_and_run.bat inventory        List/execute inventory skips only
rem   tests\build_and_run.bat test_<name>      Build target when present, then run
rem   tests\build_and_run.bat --list           Configure and list CTest inventory
rem =============================================================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS_PATH=%%i"
) else (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
)

set "CTEST_EXE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
set "CMAKE_EXE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA_DIR=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if not exist "!CTEST_EXE!" (
    echo ERROR: CTest not found: !CTEST_EXE!
    exit /b 1
)
if not exist "!CMAKE_EXE!" (
    echo ERROR: CMake not found: !CMAKE_EXE!
    exit /b 1
)
if not exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: Visual Studio environment not found under !VS_PATH!.
    exit /b 1
)

call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b !ERRORLEVEL!
set "PATH=!NINJA_DIR!;!PATH!"

set "BUILD_DIR=build\cmake"
set "ARTIFACT_ROOT=%CD%\build\artifacts\tests"
if not exist "!ARTIFACT_ROOT!" mkdir "!ARTIFACT_ROOT!"
set "ZENCROP_TEST_OUTPUT_ROOT=!ARTIFACT_ROOT!"
set "ZENCROP_DATA_DIR=!ARTIFACT_ROOT!\app-data"
if not exist "!ZENCROP_DATA_DIR!" mkdir "!ZENCROP_DATA_DIR!"

echo Configuring CMake test tree under !BUILD_DIR!...
"!CMAKE_EXE!" --preset x64-release
if errorlevel 1 exit /b !ERRORLEVEL!

set "ARG=%~1"
if "!ARG!"=="" set "ARG=hermetic"
if /I "!ARG!"=="--list" goto :list
if /I "!ARG!"=="-N" goto :list
if /I "!ARG!"=="list" goto :list
if /I "!ARG!"=="hermetic" goto :hermetic
if /I "!ARG!"=="all" goto :all
if /I "!ARG!"=="runtime" goto :runtime
if /I "!ARG!"=="inventory" goto :inventory
if /I "!ARG!"=="--help" goto :usage
if /I "!ARG!"=="-h" goto :usage
if /I "!ARG!"=="/?" goto :usage

call :build_exact_target "!ARG!"
if errorlevel 1 exit /b !ERRORLEVEL!
echo Running CTest -R "!ARG!" in !BUILD_DIR!...
pushd "!BUILD_DIR!"
"!CTEST_EXE!" -R "^%ARG%$" --no-tests=error --output-on-failure --output-junit "!ARTIFACT_ROOT!\!ARG!.xml"
set "ERR=!ERRORLEVEL!"
popd
exit /b !ERR!

:hermetic
echo Building registered test binaries on demand...
"!CMAKE_EXE!" --build "!BUILD_DIR!" --target zencrop_test_binaries
if errorlevel 1 exit /b !ERRORLEVEL!
echo Running CTest -L hermetic in !BUILD_DIR!...
pushd "!BUILD_DIR!"
"!CTEST_EXE!" -L hermetic --output-on-failure --output-junit "!ARTIFACT_ROOT!\hermetic.xml"
set "ERR=!ERRORLEVEL!"
popd
exit /b !ERR!

:all
echo Building registered test binaries on demand...
"!CMAKE_EXE!" --build "!BUILD_DIR!" --target zencrop_test_binaries
if errorlevel 1 exit /b !ERRORLEVEL!
echo Running full CTest suite in !BUILD_DIR!...
pushd "!BUILD_DIR!"
"!CTEST_EXE!" --output-on-failure --output-junit "!ARTIFACT_ROOT!\all.xml"
set "ERR=!ERRORLEVEL!"
popd
exit /b !ERR!

:runtime
echo Building registered test binaries on demand...
"!CMAKE_EXE!" --build "!BUILD_DIR!" --target zencrop_test_binaries
if errorlevel 1 exit /b !ERRORLEVEL!
echo Running CTest -L runtime in !BUILD_DIR!...
pushd "!BUILD_DIR!"
"!CTEST_EXE!" -L runtime --output-on-failure --output-junit "!ARTIFACT_ROOT!\runtime.xml"
set "ERR=!ERRORLEVEL!"
popd
exit /b !ERR!

:inventory
echo Running CTest -L inventory in !BUILD_DIR! ^(expect SKIP^)...
pushd "!BUILD_DIR!"
"!CTEST_EXE!" -L inventory --output-on-failure --output-junit "!ARTIFACT_ROOT!\inventory.xml"
set "ERR=!ERRORLEVEL!"
popd
exit /b !ERR!

:list
echo Inventory: %~dp0test_inventory.json
if not exist "%~dp0test_inventory.json" (
    echo ERROR: missing tests\test_inventory.json
    exit /b 1
)
pushd "!BUILD_DIR!"
"!CTEST_EXE!" -N
set "ERR=!ERRORLEVEL!"
popd
exit /b !ERR!

:build_exact_target
set "EXACT_TARGET=%~1"
"!CMAKE_EXE!" --build "!BUILD_DIR!" --target help 2>nul | findstr /R /X /C:"!EXACT_TARGET!: phony" >nul
if errorlevel 1 (
    echo No compiled target named !EXACT_TARGET!; running its CTest inventory entry directly.
    exit /b 0
)
echo Building test target !EXACT_TARGET!...
"!CMAKE_EXE!" --build "!BUILD_DIR!" --target "!EXACT_TARGET!"
exit /b !ERRORLEVEL!

:usage
echo Usage: tests\build_and_run.bat [hermetic^|all^|runtime^|inventory^|test_name^|--list]
echo Test reports: build\artifacts\tests\*.xml
exit /b 0
