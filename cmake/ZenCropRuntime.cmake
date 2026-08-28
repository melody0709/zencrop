# ZenCropRuntime.cmake — single authority for the immutable runtime payload.
# Product compilation remains in the CMake binary tree. `cmake --install` is
# invoked on every build.bat run so external assets are refreshed even when
# Ninja correctly decides that ZenCrop.exe does not need to be relinked.

function(zencrop_install_runtime target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "zencrop_install_runtime: target '${target}' does not exist")
    endif()

    set(_src "${CMAKE_SOURCE_DIR}")
    set(_path_table "${_src}/src/assets/icons/PATH_TABLE.tsv")
    set(_webview_loader "${_src}/third_party/webview2/x64/WebView2Loader.dll")
    set(_onnx_runtime "${_src}/ocr/onnxruntime/lib/onnxruntime.dll")
    set(_avif_encoder "${_src}/third_party/imagecodecs/bin/avifenc.exe")
    set(_avif_decoder "${_src}/third_party/imagecodecs/bin/avifdec.exe")
    set(_web_assets "${_src}/src/ocr/ui/webview_assets")
    set(_chat_template "${_src}/src/ocr/chat_templates/paddleocr-vl-1.6.jinja")
    set(_ppocrv6_dict "${_src}/src/ocr/templates/ppocrv6_rec_dict.txt")

    # The runtime manifest is diagnostic only. W2 replaces the build-id
    # placeholder with the generated web-asset trust root.
    if(NOT DEFINED ZENCROP_WEB_ASSET_MANIFEST_SCHEMA_VERSION)
        set(ZENCROP_WEB_ASSET_MANIFEST_SCHEMA_VERSION 0)
    endif()
    if(NOT DEFINED ZENCROP_WEB_ASSET_BUILD_ID)
        set(ZENCROP_WEB_ASSET_BUILD_ID "not-generated")
    endif()

    foreach(_required IN ITEMS
        "${_path_table}"
        "${_webview_loader}"
        "${_onnx_runtime}"
        "${_avif_encoder}"
        "${_avif_decoder}"
        "${_web_assets}/ocr-preview/index.html"
        "${_chat_template}"
        "${_ppocrv6_dict}"
    )
        if(NOT EXISTS "${_required}")
            message(FATAL_ERROR "Missing required ZenCrop runtime input: ${_required}")
        endif()
    endforeach()

    if(ZENCROP_REQUIRE_OPENCV AND NOT ZENCROP_OPENCV_RUNTIME_DLLS)
        message(FATAL_ERROR
            "OpenCV is required but no release runtime DLL was discovered. "
            "Check the OpenCV bin layout instead of emitting a partial runtime.")
    endif()

    # The runtime manifest is the concise install report; avoid one console
    # line per vendored font or script on every exact refresh.
    set(CMAKE_INSTALL_MESSAGE NEVER)

    # An install is an exact refresh of files owned by the runtime payload.
    # Mutable application data is stored under LocalAppData and is never here.
    install(CODE [=[
        set(_runtime_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
        file(REMOVE_RECURSE
            "${_runtime_root}/imagecodecs"
            "${_runtime_root}/ocr_templates"
            "${_runtime_root}/webview_assets"
            "${_runtime_root}/scripts")
        file(GLOB _old_opencv LIST_DIRECTORIES false "${_runtime_root}/opencv_*.dll")
        if(_old_opencv)
            file(REMOVE ${_old_opencv})
        endif()
        file(REMOVE
            "${_runtime_root}/ZenCrop.exe"
            "${_runtime_root}/WebView2Loader.dll"
            "${_runtime_root}/onnxruntime.dll"
            "${_runtime_root}/PATH_TABLE.tsv"
            "${_runtime_root}/runtime-manifest.json"
            "${_runtime_root}/runtime-manifest.json.tmp")
    ]=] COMPONENT Runtime)

    install(TARGETS ${target}
        RUNTIME DESTINATION "."
        COMPONENT Runtime)
    install(FILES
        "${_webview_loader}"
        "${_onnx_runtime}"
        "${_path_table}"
        ${ZENCROP_OPENCV_RUNTIME_DLLS}
        DESTINATION "."
        COMPONENT Runtime)
    install(PROGRAMS
        "${_avif_encoder}"
        "${_avif_decoder}"
        DESTINATION "imagecodecs"
        COMPONENT Runtime)
    install(DIRECTORY "${_web_assets}/"
        DESTINATION "webview_assets"
        COMPONENT Runtime)
    install(FILES
        "${_chat_template}"
        "${_ppocrv6_dict}"
        DESTINATION "ocr_templates"
        COMPONENT Runtime)

    find_package(Git QUIET)
    if(GIT_FOUND)
        set(ZENCROP_GIT_EXECUTABLE "${GIT_EXECUTABLE}")
    else()
        set(ZENCROP_GIT_EXECUTABLE "git")
    endif()
    set(_manifest_script "${CMAKE_CURRENT_BINARY_DIR}/WriteRuntimeManifest.cmake")
    configure_file(
        "${_src}/cmake/WriteRuntimeManifest.cmake.in"
        "${_manifest_script}"
        @ONLY)
    install(SCRIPT "${_manifest_script}" COMPONENT Runtime)
endfunction()
