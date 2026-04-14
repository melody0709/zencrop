cmake_minimum_required(VERSION 3.15)
project(ZenCrop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 确保静态链接 C 运行库，缩小体积，避免缺少 dll
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(ZenCrop WIN32
    main.cpp
    Utils.h Utils.cpp
    OverlayWindow.h OverlayWindow.cpp
    ReparentWindow.h ReparentWindow.cpp
    ThumbnailWindow.h ThumbnailWindow.cpp
)

set_target_properties(ZenCrop PROPERTIES
    WIN32_EXECUTABLE YES
)

if(WIN32)
    set_target_properties(ZenCrop PROPERTIES
        RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
    )
endif()

target_link_libraries(ZenCrop
    user32
    gdi32
    dwmapi
    shcore
)
