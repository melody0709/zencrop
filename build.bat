@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d D:\#GITHUB_melody0709\zencrop
if not exist build mkdir build
rc /fo build\app.res resources.rc
cl /O2 /EHsc /MT /std:c++17 /DUNICODE /D_UNICODE /Fobuild\ /Fe:build\ZenCrop.exe main.cpp Utils.cpp OverlayWindow.cpp ReparentWindow.cpp ThumbnailWindow.cpp ViewportWindow.cpp SmartDetector.cpp AlwaysOnTop.cpp Settings.cpp build\app.res user32.lib gdi32.lib dwmapi.lib shcore.lib shell32.lib ole32.lib oleaut32.lib shlwapi.lib comdlg32.lib advapi32.lib
if %ERRORLEVEL% equ 0 (
    echo Build Success!
    for /f "tokens=3 delims= " %%v in ('findstr /C:"ZENCROP_VERSION L" main.cpp') do set "VER=%%v"
    set "VER=%VER:"=%"
    copy /Y "build\ZenCrop.exe" "build\ZenCrop_v%VER%.exe" >nul
    echo Renamed to build\ZenCrop_v%VER%.exe
)
