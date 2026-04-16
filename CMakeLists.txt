cmake_minimum_required(VERSION 3.15)
project(ZenCrop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(ZenCrop WIN32
    main.cpp
    Utils.h Utils.cpp
    OverlayWindow.h OverlayWindow.cpp
    ReparentWindow.h ReparentWindow.cpp
    ThumbnailWindow.h ThumbnailWindow.cpp
    SmartDetector.h SmartDetector.cpp
    AlwaysOnTop.h AlwaysOnTop.cpp
    Settings.h Settings.cpp
    resources.rc
)

add_compile_definitions(UNICODE _UNICODE)

set_target_properties(ZenCrop PROPERTIES
    WIN32_EXECUTABLE YES
)

target_link_libraries(ZenCrop
    user32
    gdi32
    dwmapi
    shcore
    shell32
    ole32
    oleaut32
    shlwapi
    comdlg32
    advapi32
)
