@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /O2 /EHsc /MT /std:c++17 /DUNICODE /D_UNICODE /Fe:ZenCrop.exe main.cpp Utils.cpp OverlayWindow.cpp ReparentWindow.cpp ThumbnailWindow.cpp user32.lib gdi32.lib dwmapi.lib shcore.lib shell32.lib
if %ERRORLEVEL% equ 0 echo Build Success!
