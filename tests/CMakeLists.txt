# Hermetic / lightweight contract tests (Stage 0-C slice 1).
# Full 48-target migration continues in later sessions; this covers annotation
# + preset packs that Stage 0-E already gates via build_and_run.bat.

cmake_minimum_required(VERSION 3.20)

set(ZENCROP_ANNOTATION_SOURCES
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationProperty.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationValue.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationItem.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationModel.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationMigration.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.cpp
)

set(ZENCROP_TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/window
    ${CMAKE_SOURCE_DIR}/src/screenshot
    ${CMAKE_SOURCE_DIR}/src/detect
)

function(zencrop_add_annotation_test name)
    add_executable(${name}
        ${CMAKE_SOURCE_DIR}/tests/${name}.cpp
        ${ZENCROP_ANNOTATION_SOURCES}
    )
    target_include_directories(${name} PRIVATE ${ZENCROP_TEST_INCLUDE_DIRS})
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE WINRT_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /EHsc)
    endif()
    target_link_libraries(${name} PRIVATE gdiplus gdi32 user32)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "hermetic")
endfunction()

zencrop_add_annotation_test(test_annotation_enums)
zencrop_add_annotation_test(test_annotation_migration)
zencrop_add_annotation_test(test_annotation_item)
zencrop_add_annotation_test(test_annotation_history)
zencrop_add_annotation_test(test_annotation_model)
zencrop_add_annotation_test(test_annotation_property)
zencrop_add_annotation_test(test_annotation_document_contract)

# PP-OCRv6 preset packs live in Settings.h (header-only logic for this contract).
add_executable(test_ppocrv6_preset_contract
    ${CMAKE_SOURCE_DIR}/tests/test_ppocrv6_preset_contract.cpp
)
target_include_directories(test_ppocrv6_preset_contract PRIVATE ${ZENCROP_TEST_INCLUDE_DIRS})
target_compile_definitions(test_ppocrv6_preset_contract PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(test_ppocrv6_preset_contract PRIVATE /utf-8 /EHsc)
endif()
set_target_properties(test_ppocrv6_preset_contract PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
)
add_test(NAME test_ppocrv6_preset_contract COMMAND test_ppocrv6_preset_contract)
set_tests_properties(test_ppocrv6_preset_contract PROPERTIES LABELS "hermetic")

# LongShot constants + signed stitch placement (synthetic GDI bitmaps; OpenCV when available).
# Screenshot::GetBitmapSize is stubbed inside the test TU (avoids full ScreenshotUtils graph).
add_executable(test_longshot_stitch_contract
    ${CMAKE_SOURCE_DIR}/tests/test_longshot_stitch_contract.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/longshot/LongShotStitcher.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/longshot/LongShotImage.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/longshot/LongShotExport.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/longshot/LongShotScrollInjector.cpp
)
target_include_directories(test_longshot_stitch_contract PRIVATE ${ZENCROP_TEST_INCLUDE_DIRS})
target_compile_definitions(test_longshot_stitch_contract PRIVATE UNICODE _UNICODE WINRT_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(test_longshot_stitch_contract PRIVATE /utf-8 /EHsc)
endif()
# Mirror product OpenCV flags when the main tree found OpenCV.
if(ZENCROP_OPENCV_FOUND OR OpenCV_FOUND)
    target_compile_definitions(test_longshot_stitch_contract PRIVATE
        ZENCROP_WITH_OPENCV_DBPOST=1
        ZENCROP_WITH_OPENCV_LAYOUT=1
    )
    if(OpenCV_INCLUDE_DIRS)
        target_include_directories(test_longshot_stitch_contract PRIVATE ${OpenCV_INCLUDE_DIRS})
    endif()
    if(OpenCV_LIBS)
        target_link_libraries(test_longshot_stitch_contract PRIVATE ${OpenCV_LIBS})
    elseif(OpenCV_LIBRARIES)
        target_link_libraries(test_longshot_stitch_contract PRIVATE ${OpenCV_LIBRARIES})
    endif()
endif()
target_link_libraries(test_longshot_stitch_contract PRIVATE gdi32 user32 ole32 oleaut32 windowscodecs)
set_target_properties(test_longshot_stitch_contract PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
)
add_test(NAME test_longshot_stitch_contract COMMAND test_longshot_stitch_contract)
set_tests_properties(test_longshot_stitch_contract PROPERTIES LABELS "hermetic")

# --- S0C-2: additional hermetic contracts (header-light / few TU) ---

function(zencrop_set_test_output target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
    )
endfunction()

add_executable(test_page_range
    ${CMAKE_SOURCE_DIR}/tests/test_page_range.cpp
    ${CMAKE_SOURCE_DIR}/src/ocr/batch/PageRange.cpp
)
target_include_directories(test_page_range PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_page_range PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(test_page_range PRIVATE /utf-8 /EHsc)
endif()
zencrop_set_test_output(test_page_range)
add_test(NAME test_page_range COMMAND test_page_range)
set_tests_properties(test_page_range PROPERTIES LABELS "hermetic")

add_executable(test_paddle_doc_layout_profile
    ${CMAKE_SOURCE_DIR}/tests/test_paddle_doc_layout_profile.cpp
)
target_include_directories(test_paddle_doc_layout_profile PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_paddle_doc_layout_profile PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(test_paddle_doc_layout_profile PRIVATE /utf-8 /EHsc)
endif()
target_link_libraries(test_paddle_doc_layout_profile PRIVATE user32)
zencrop_set_test_output(test_paddle_doc_layout_profile)
add_test(NAME test_paddle_doc_layout_profile COMMAND test_paddle_doc_layout_profile)
set_tests_properties(test_paddle_doc_layout_profile PROPERTIES LABELS "hermetic")

add_executable(test_magnifier_geometry
    ${CMAKE_SOURCE_DIR}/tests/test_magnifier_geometry.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.cpp
)
target_include_directories(test_magnifier_geometry PRIVATE ${ZENCROP_TEST_INCLUDE_DIRS})
target_compile_definitions(test_magnifier_geometry PRIVATE UNICODE _UNICODE WINRT_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(test_magnifier_geometry PRIVATE /utf-8 /EHsc)
endif()
target_link_libraries(test_magnifier_geometry PRIVATE gdiplus gdi32 user32)
zencrop_set_test_output(test_magnifier_geometry)
add_test(NAME test_magnifier_geometry COMMAND test_magnifier_geometry)
set_tests_properties(test_magnifier_geometry PROPERTIES LABELS "hermetic")

# --- S0C-3: more hermetic / header-light contracts ---

function(zencrop_add_simple_hermetic name)
    # Usage: zencrop_add_simple_hermetic(name [SOURCES a.cpp b.cpp] [LIBS ...] [DEFINES ...] [LABEL hermetic])
    cmake_parse_arguments(ARG "" "LABEL" "SOURCES;LIBS;DEFINES;INCLUDES" ${ARGN})
    if(NOT ARG_LABEL)
        set(ARG_LABEL hermetic)
    endif()
    add_executable(${name}
        ${CMAKE_SOURCE_DIR}/tests/${name}.cpp
        ${ARG_SOURCES}
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${ARG_INCLUDES}
    )
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE WINRT_LEAN_AND_MEAN ${ARG_DEFINES})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /EHsc)
    endif()
    if(ARG_LIBS)
        target_link_libraries(${name} PRIVATE ${ARG_LIBS})
    endif()
    zencrop_set_test_output(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "${ARG_LABEL}")
endfunction()

# Header-only / single-TU hermetics
zencrop_add_simple_hermetic(test_ppocrv6_ctc_decode_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr ${CMAKE_SOURCE_DIR}/src/ocr/engine
)
zencrop_add_simple_hermetic(test_startup_registration_contract
    SOURCES ${CMAKE_SOURCE_DIR}/src/core/StartupRegistration.cpp
    LIBS advapi32
)
zencrop_add_simple_hermetic(test_ppocrv6_rec_batch_plan_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr ${CMAKE_SOURCE_DIR}/src/ocr/engine
)
zencrop_add_simple_hermetic(test_ppocrv6_block_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr ${CMAKE_SOURCE_DIR}/src/ocr/engine ${CMAKE_SOURCE_DIR}/src/ocr/ui
    LIBS gdiplus gdi32 user32
)
zencrop_add_simple_hermetic(test_paddle_vl_block_json_contract
    SOURCES ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/ocr
    LIBS user32
)
zencrop_add_simple_hermetic(test_dashboard_textmode_persistence_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/ocr ${CMAKE_SOURCE_DIR}/src/ocr/ui
    LIBS user32
)
zencrop_add_simple_hermetic(test_paddle_doc_region_grouping
    SOURCES ${CMAKE_SOURCE_DIR}/src/ocr/engine/PaddleDocRegionGrouping.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr ${CMAKE_SOURCE_DIR}/src/ocr/engine ${CMAKE_SOURCE_DIR}/src/ocr/layout
    LIBS user32
)
zencrop_add_simple_hermetic(test_local_raster_contract
    SOURCES ${CMAKE_SOURCE_DIR}/src/ocr/LocalRaster.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS gdiplus gdi32 user32
)
zencrop_add_simple_hermetic(test_paddle_doc_layout_postprocess
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/layout/PaddleDocLayoutPostprocess.cpp
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.engine.cpp
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.offset.cpp
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.rectclip.cpp
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.triangulation.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/ocr/layout
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/include
    LIBS user32
)
zencrop_add_annotation_test(test_property_sync)

zencrop_add_simple_hermetic(test_dashboard_file_types
    SOURCES ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardFileTypes.cpp
    # OWN-74: FileTypes thin-wraps core/WideStringUtils.h
    INCLUDES ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/ocr/ui ${CMAKE_SOURCE_DIR}/src/core
)

# OWN-74: pure WideStringUtils contract (header-only).
zencrop_add_simple_hermetic(test_wide_string_utils_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/core
)

zencrop_add_simple_hermetic(test_translation_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/core/Settings.cpp
        ${CMAKE_SOURCE_DIR}/src/core/TranslationSettingsCodec.cpp
        ${CMAKE_SOURCE_DIR}/src/core/HotkeyEdit.cpp
        ${CMAKE_SOURCE_DIR}/src/core/AppDataPaths.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Strings.cpp
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/core/ClipboardUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationCredentialStore.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationPreflight.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/AsyncHttpTransport.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/LlmModelPolicy.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationProviderCatalog.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationPromptComposer.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/OpenAICompatibleTranslationEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/MachineTranslationEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationEngineFactory.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/DeepSeekTranslationEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationSettingsPage.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationProviderSettingsPage.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationPromptSettingsPage.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationResultWindow.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationResultWindow.StructuredSelection.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationCoordinator.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationCoordinator.StructuredSelection.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/OcrMarkdownPreviewHost.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/OcrMarkdownPreviewProtocol.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardTranslationCache.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/WebAssetGuard.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Sha256.cpp
        ${CMAKE_SOURCE_DIR}/src/window/AlwaysOnTop.cpp
        ${CMAKE_SOURCE_DIR}/src/selection/SelectionTypes.cpp
        ${CMAKE_SOURCE_DIR}/src/selection/SelectionStructuredContent.cpp
        ${CMAKE_SOURCE_DIR}/src/selection/ClipboardCopyPolicy.cpp
        ${CMAKE_SOURCE_DIR}/src/selection/ClipboardDataSnapshot.cpp
        ${CMAKE_SOURCE_DIR}/src/selection/ClipboardStructuredContentReader.cpp
        ${CMAKE_SOURCE_DIR}/src/selection/ClipboardCopyTransaction.cpp
        ${CMAKE_SOURCE_DIR}/src/selection/SelectionTextAcquirer.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/core
             ${CMAKE_SOURCE_DIR}/src/ocr ${CMAKE_SOURCE_DIR}/src/ocr/engine
             ${CMAKE_SOURCE_DIR}/src/ocr/ui ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard
             ${CMAKE_SOURCE_DIR}/src/translation
             ${CMAKE_SOURCE_DIR}/third_party
             ${CMAKE_SOURCE_DIR}/third_party/webview2/include
             ${CMAKE_BINARY_DIR}/generated
    LIBS user32 shell32 shlwapi comctl32 advapi32 gdiplus dwmapi shcore uxtheme
         ole32 oleaut32 bcrypt uiautomationcore
         ${CMAKE_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll.lib
)

zencrop_add_simple_hermetic(test_deepseek_protocol_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/translation/DeepSeekTranslationEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/AsyncHttpTransport.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationCredentialStore.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/LlmModelPolicy.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationProviderCatalog.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/TranslationPromptComposer.cpp
        ${CMAKE_SOURCE_DIR}/src/translation/OpenAICompatibleTranslationEngine.cpp
        ${CMAKE_SOURCE_DIR}/src/core/AppDataPaths.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/src/translation ${CMAKE_SOURCE_DIR}/src/core
             ${CMAKE_SOURCE_DIR}/third_party
    LIBS winhttp advapi32 shell32 shlwapi user32 ws2_32
)

# Header-only dashboard contracts (no window)
zencrop_add_simple_hermetic(test_dashboard_ocr_routing
    INCLUDES ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/ocr
    LIBS user32
)
zencrop_add_simple_hermetic(test_dashboard_optimization_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/ocr
    LIBS user32
)

# --- S0C-4: OpenCV-backed layout contracts (same discovery as product CMake) ---
# Reuse parent product OpenCV result when available; otherwise find_package with
# the same third_party/opencv prefix path. Do not hardcode vc16/opencv_world500.
if(NOT ZENCROP_OPENCV_FOUND)
    set(ZENCROP_LOCAL_OPENCV_ROOT "${CMAKE_SOURCE_DIR}/third_party/opencv")
    if(EXISTS "${ZENCROP_LOCAL_OPENCV_ROOT}/OpenCVConfig.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${ZENCROP_LOCAL_OPENCV_ROOT}")
    elseif(EXISTS "${ZENCROP_LOCAL_OPENCV_ROOT}/build/OpenCVConfig.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${ZENCROP_LOCAL_OPENCV_ROOT}/build")
    endif()
    find_package(OpenCV QUIET COMPONENTS core geometry imgproc)
    if(OpenCV_FOUND)
        set(ZENCROP_OPENCV_FOUND TRUE)
        if(NOT ZENCROP_OPENCV_BIN_DIR)
            foreach(_opencv_vc vc18 vc17 vc16 vc15 vc14)
                if(NOT ZENCROP_OPENCV_BIN_DIR AND EXISTS "${ZENCROP_LOCAL_OPENCV_ROOT}/x64/${_opencv_vc}/bin")
                    set(ZENCROP_OPENCV_BIN_DIR "${ZENCROP_LOCAL_OPENCV_ROOT}/x64/${_opencv_vc}/bin")
                endif()
            endforeach()
        endif()
    endif()
endif()

if(ZENCROP_OPENCV_FOUND)
    set(ZENCROP_CLIPPER_SOURCES
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.engine.cpp
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.offset.cpp
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.rectclip.cpp
        ${CMAKE_SOURCE_DIR}/third_party/clipper2/src/clipper.triangulation.cpp
    )

    function(zencrop_add_opencv_layout_test name)
        cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
        add_executable(${name}
            ${CMAKE_SOURCE_DIR}/tests/${name}.cpp
            ${ARG_SOURCES}
        )
        target_include_directories(${name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/ocr
            ${CMAKE_SOURCE_DIR}/src/ocr/layout
            ${CMAKE_SOURCE_DIR}/src/ocr/engine
            ${CMAKE_SOURCE_DIR}/third_party/clipper2/include
            ${OpenCV_INCLUDE_DIRS}
        )
        target_compile_definitions(${name} PRIVATE
            UNICODE _UNICODE WINRT_LEAN_AND_MEAN
            ZENCROP_WITH_OPENCV_LAYOUT=1
        )
        if(MSVC)
            target_compile_options(${name} PRIVATE /utf-8 /EHsc)
        endif()
        target_link_libraries(${name} PRIVATE ${OpenCV_LIBS} user32 gdi32)
        zencrop_set_test_output(${name})
        add_test(NAME ${name} COMMAND ${name})
        if(ZENCROP_OPENCV_BIN_DIR)
            set_tests_properties(${name} PROPERTIES
                LABELS "hermetic;opencv"
                ENVIRONMENT "PATH=${ZENCROP_OPENCV_BIN_DIR};$ENV{PATH}"
            )
        else()
            set_tests_properties(${name} PROPERTIES
                LABELS "hermetic;opencv"
            )
        endif()
    endfunction()

    zencrop_add_opencv_layout_test(test_paddle_doc_layout_preprocessor
        SOURCES ${CMAKE_SOURCE_DIR}/src/ocr/layout/PaddleDocLayoutPreprocessor.cpp
    )
    zencrop_add_opencv_layout_test(test_paddle_doc_recognition_image
        SOURCES ${CMAKE_SOURCE_DIR}/src/ocr/engine/PaddleDocRecognitionImage.cpp
    )
    zencrop_add_opencv_layout_test(test_paddle_doc_layout_mask_postprocess
        SOURCES
            ${CMAKE_SOURCE_DIR}/src/ocr/layout/PaddleDocLayoutPostprocess.cpp
            ${ZENCROP_CLIPPER_SOURCES}
    )
else()
    message(STATUS "Skipping OpenCV layout hermetic tests: OpenCV not found via find_package")
endif()

# Screenshot toolbar command taxonomy (Stage 2 S-A/S-C characterization seed)
zencrop_add_simple_hermetic(test_screenshot_toolbar_command_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationTypes.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
)

# S-D/S-F-CLOSE-1: AnnotationRenderContext typed seed (header-only)
zencrop_add_simple_hermetic(test_annotation_render_context_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
)

# S-A-CLOSE-1: free-helper sole-path hermetic (HighLight fill + RenderContext purpose)
# Needs annotation pack + helpers for IsRotatableGeometry used by MakeHighLightRenderInfo.
add_executable(test_annotation_product_draw_free_helper_contract
    ${CMAKE_SOURCE_DIR}/tests/test_annotation_product_draw_free_helper_contract.cpp
    ${ZENCROP_ANNOTATION_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotAnnotationHelpers.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotAnnotationGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotImageUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotPixelUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/CropAdjustMath.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.cpp
)
target_include_directories(test_annotation_product_draw_free_helper_contract PRIVATE
    ${ZENCROP_TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/screenshot/render
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
    ${CMAKE_SOURCE_DIR}/src/screenshot/overlay
)
target_compile_definitions(test_annotation_product_draw_free_helper_contract PRIVATE UNICODE _UNICODE WINRT_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(test_annotation_product_draw_free_helper_contract PRIVATE /utf-8 /EHsc)
endif()
target_link_libraries(test_annotation_product_draw_free_helper_contract PRIVATE gdiplus gdi32 user32 shcore)
zencrop_set_test_output(test_annotation_product_draw_free_helper_contract)
add_test(NAME test_annotation_product_draw_free_helper_contract COMMAND test_annotation_product_draw_free_helper_contract)
set_tests_properties(test_annotation_product_draw_free_helper_contract PROPERTIES LABELS "hermetic")

# S-A-EXIT: fixed-DPI Preview/Export render-context ladder (100/125/150/200%)
zencrop_add_simple_hermetic(test_screenshot_fixed_dpi_render_context_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
)

# S-A-EXIT: P95-GDI method (percentile index + dual-threshold Gate rule)
zencrop_add_simple_hermetic(test_screenshot_p95_method_contract)

# S-D/S-F-EXIT: shared renderer registry + vector-tool dispatch
# Needs annotation pack + helpers (registry includes GeometryRenderer free helpers).
add_executable(test_annotation_renderer_registry_contract
    ${CMAKE_SOURCE_DIR}/tests/test_annotation_renderer_registry_contract.cpp
    ${ZENCROP_ANNOTATION_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotAnnotationHelpers.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotAnnotationGeometry.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotPixelUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotImageUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/screenshot/CropAdjustMath.cpp
)
target_include_directories(test_annotation_renderer_registry_contract PRIVATE
    ${ZENCROP_TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/screenshot/render
    ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
    ${CMAKE_SOURCE_DIR}/src/screenshot/overlay
)
target_compile_definitions(test_annotation_renderer_registry_contract PRIVATE UNICODE _UNICODE WINRT_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(test_annotation_renderer_registry_contract PRIVATE /utf-8 /EHsc)
endif()
target_link_libraries(test_annotation_renderer_registry_contract PRIVATE gdiplus gdi32 user32 shcore)
zencrop_set_test_output(test_annotation_renderer_registry_contract)
add_test(NAME test_annotation_renderer_registry_contract COMMAND test_annotation_renderer_registry_contract)
set_tests_properties(test_annotation_renderer_registry_contract PROPERTIES LABELS "hermetic")

# S-G-CLOSE-1: pure Toolbar hit-test free helper (header-only)
zencrop_add_simple_hermetic(test_screenshot_toolbar_hit_test_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
    LIBS user32 gdi32
)

# S-G-CLOSE-3: pure main-toolbar fixed slot catalog (header-only)
zencrop_add_simple_hermetic(test_screenshot_main_toolbar_catalog_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
)

# S-C/S-G-EXIT: pure main-toolbar model builder (catalog+sticky+history+function+More)
zencrop_add_simple_hermetic(test_screenshot_main_toolbar_model_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
)

# --- S0C-5: more contracts + runtime SKIP=77 stubs ---

zencrop_add_simple_hermetic(test_image_codec_webp_avif
    SOURCES ${CMAKE_SOURCE_DIR}/src/image/BitmapCodec.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/image
        ${CMAKE_SOURCE_DIR}/third_party/imagecodecs/libwebp/include
    LIBS
        ${CMAKE_SOURCE_DIR}/third_party/imagecodecs/libwebp/lib/libwebp.lib
        user32 gdi32 gdiplus shell32 shlwapi windowscodecs ole32
)

# Copy avif tools next to test binary for runtime discovery
add_custom_command(TARGET test_image_codec_webp_avif POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:test_image_codec_webp_avif>/imagecodecs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/third_party/imagecodecs/bin
        $<TARGET_FILE_DIR:test_image_codec_webp_avif>/imagecodecs
)

zencrop_add_simple_hermetic(test_cloud_native_pdf_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Sha256.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/OcrDocumentTypes.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/OcrDocumentAlignment.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/document/PaddleCloudDocumentNormalizer.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/document/PaddleCloudDocumentProtocol.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/document/PaddleCloudDocumentTransport.cpp
        ${CMAKE_SOURCE_DIR}/src/net/Network.cpp
        # OWN-73/74: Normalizer/Protocol thin-wrap pure DashboardFileTypes + WideStringUtils.
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardFileTypes.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/document
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/net
    LIBS user32 bcrypt winhttp
)



# Runtime/fixture tests: build always; run with SKIP=77 when fixtures missing via wrapper.
function(zencrop_add_runtime_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;INCLUDES;DEFINES" ${ARGN})
    add_executable(${name}
        ${CMAKE_SOURCE_DIR}/tests/${name}.cpp
        ${ARG_SOURCES}
    )
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${ARG_INCLUDES})
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE WINRT_LEAN_AND_MEAN ${ARG_DEFINES})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /EHsc)
    endif()
    if(ARG_LIBS)
        target_link_libraries(${name} PRIVATE ${ARG_LIBS})
    endif()
    zencrop_set_test_output(${name})
    # Wrapper: if ZENCROP_RUN_RUNTIME_TESTS!=1, exit 77 (CTest SKIP)
    add_test(NAME ${name}
        COMMAND ${CMAKE_SOURCE_DIR}/tests/cmake/run_runtime_test.cmd
            $<TARGET_FILE:${name}>
    )
    set_tests_properties(${name} PROPERTIES
        LABELS "runtime"
        SKIP_RETURN_CODE 77
    )
endfunction()

zencrop_add_runtime_test(test_ppocrv6_writer_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/image/BitmapCodec.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/batch/BatchOcrController.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/batch/BatchOcrWriter.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/batch/BatchOcrOutputArtifacts.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/OcrDocumentTypes.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/OcrDocumentAlignment.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/batch/BatchOcrManifest.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/batch/BatchOcrImageLinks.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/batch/PageRange.cpp
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Sha256.cpp
        # OWN-72/74: BatchOcrController thin-wraps pure DashboardFileTypes + WideStringUtils.
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardFileTypes.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/third_party/imagecodecs/libwebp/include
    LIBS
        user32 gdi32 gdiplus shell32 shlwapi bcrypt windowsapp windowscodecs ole32
        ${CMAKE_SOURCE_DIR}/third_party/imagecodecs/libwebp/lib/libwebp.lib
)

# This existing contract is intentionally promoted rather than replaced: it
# validates the same production host used by ZenCrop, including its compiled
# web-asset trust manifest. Its post-build fixture is test output only.
zencrop_add_runtime_test(test_webview2_preview_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/OcrMarkdownPreviewProtocol.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/WebAssetGuard.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/OcrMarkdownPreviewHost.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardFileTypes.cpp
    INCLUDES
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard
        ${CMAKE_SOURCE_DIR}/third_party/webview2/include
    LIBS
        user32 shell32 shlwapi ole32 oleaut32 bcrypt
        ${CMAKE_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll.lib
    DEFINES ZENCROP_PREVIEW_HOST_TESTS
)
add_dependencies(test_webview2_preview_contract zencrop_web_assets_manifest)
add_custom_command(TARGET test_webview2_preview_contract POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_FILE_DIR:test_webview2_preview_contract>/webview_assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/ocr/ui/webview_assets"
        "$<TARGET_FILE_DIR:test_webview2_preview_contract>/webview_assets"
    COMMENT "Refreshing test WebView2 asset fixture"
    VERBATIM)
set_tests_properties(test_webview2_preview_contract PROPERTIES
    ENVIRONMENT "ZENCROP_RUN_RUNTIME_TESTS=1")

# PDF / model / window contracts: build target only when requested; default SKIP via wrapper.
# (Heavy dashboard/window tests remain bat-driven until D-A harness is fully host-based.)

zencrop_add_simple_hermetic(test_annotation_value_kind_contract
    SOURCES ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationValue.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
)

zencrop_add_simple_hermetic(test_dashboard_history_store_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardHistoryStore.cpp
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32
)

zencrop_add_simple_hermetic(test_screenshot_annotation_role_contract
    SOURCES ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationTypes.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
)

zencrop_add_simple_hermetic(test_dashboard_history_repository_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardHistoryRepository.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardHistoryStore.cpp
        ${CMAKE_SOURCE_DIR}/src/core/AppDataPaths.cpp
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32 shell32 shlwapi
)

zencrop_add_simple_hermetic(test_dashboard_state_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32
)

zencrop_add_simple_hermetic(test_annotation_value_schema_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationValue.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationProperty.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationItem.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation/AnnotationTypes.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
)

zencrop_add_simple_hermetic(test_dashboard_batch_projection_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
    LIBS user32 ole32
)

zencrop_add_simple_hermetic(test_dashboard_source_rail_projection_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
        ${CMAKE_SOURCE_DIR}/src/ocr/document
    LIBS user32 ole32
)

zencrop_add_simple_hermetic(test_dashboard_canvas_math_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr/ui
    LIBS user32
)
zencrop_add_simple_hermetic(test_dashboard_preview_security_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr/ui
    LIBS user32
)
zencrop_add_simple_hermetic(test_dashboard_history_model_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardHistoryRepository.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardHistoryStore.cpp
        ${CMAKE_SOURCE_DIR}/src/core/AppDataPaths.cpp
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32 shell32 shlwapi
)

# D-D-1: pure Controller ApplyFilter / ApplyTextMode / Dispatch (no HWND).
zencrop_add_simple_hermetic(test_dashboard_controller_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardController.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32
)

# D-E-1: BatchCoordinator drop queue ownership + generation State sole.
zencrop_add_simple_hermetic(test_dashboard_batch_coordinator_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
        ${CMAKE_SOURCE_DIR}/src/ocr/document
    LIBS user32 ole32
)

# D-F-1/2: free SourceRail model types + pure BuildTaskRows / filters.
zencrop_add_simple_hermetic(test_dashboard_source_rail_model_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardSourceRailModel.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardFileTypes.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
        ${CMAKE_SOURCE_DIR}/src/ocr/document
    LIBS user32 ole32
)

# D-G-1: CanvasModel block ownership + pure hit-test (no HWND).
zencrop_add_simple_hermetic(test_dashboard_canvas_model_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32
)

# D-H-1: pure Preview protocol decisions + security seed (no HWND / WebView2).
zencrop_add_simple_hermetic(test_dashboard_preview_coordinator_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32
)

zencrop_add_simple_hermetic(test_dashboard_history_cache_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardHistoryCache.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
    LIBS user32 shlwapi
)

zencrop_add_simple_hermetic(test_dashboard_result_projection_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardResultProjection.cpp
        ${CMAKE_SOURCE_DIR}/src/core/JsonUtils.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
        ${CMAKE_SOURCE_DIR}/src/ocr/document
    LIBS user32 ole32
)

# D-B-CLOSE-3: PDF options domain hermetic (preflight/validate/format; heavy PDF stubbed).
zencrop_add_simple_hermetic(test_dashboard_pdf_options_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardPdfOptionsPreflight.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/ui/dashboard/DashboardFileTypes.cpp
        ${CMAKE_SOURCE_DIR}/src/ocr/batch/PageRange.cpp
        ${CMAKE_SOURCE_DIR}/tests/support/dashboard_pdf_options_hermetic_stubs.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/ocr
        ${CMAKE_SOURCE_DIR}/src/ocr/ui
        ${CMAKE_SOURCE_DIR}/src/ocr/batch
        ${CMAKE_SOURCE_DIR}/src/ocr/document
    LIBS user32 shlwapi
)

zencrop_add_simple_hermetic(test_dashboard_layout_resolve_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr/ui
    LIBS user32
)

zencrop_add_simple_hermetic(test_dashboard_message_route_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr/ui
    LIBS user32
)

zencrop_add_simple_hermetic(test_screenshot_editor_state_contract
    SOURCES
        ${ZENCROP_ANNOTATION_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotAnnotationGeometry.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotImageUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotPixelUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor/ScreenshotToolbarPanelState.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor/ScreenshotToolbarSliderMutation.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor/ScreenshotToolbarColorMutation.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
    LIBS gdiplus gdi32 user32 shcore
)

zencrop_add_simple_hermetic(test_screenshot_toolbar_text_style_mutation_contract
    SOURCES
        ${ZENCROP_ANNOTATION_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotAnnotationGeometry.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotImageUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/ScreenshotPixelUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor/ScreenshotToolbarPanelState.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor/ScreenshotToolbarSliderMutation.cpp
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor/ScreenshotToolbarColorMutation.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/annotation
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
    LIBS gdiplus gdi32 user32 shcore
)

zencrop_add_simple_hermetic(test_screenshot_toolbar_groups_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
)

zencrop_add_simple_hermetic(test_dashboard_batch_queue_projection_contract
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr/ui
)

zencrop_add_simple_hermetic(test_screenshot_annotation_selection_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
)

# Stage 2 S-C: command kind classifier
zencrop_add_simple_hermetic(test_screenshot_command_kind_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
)

# Stage 4-B seed: model registry dry-run
zencrop_add_simple_hermetic(test_ocr_model_registry_contract
    SOURCES
        ${CMAKE_SOURCE_DIR}/src/ocr/model_download/OcrModelDownloadCatalog.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Sha256.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/src/ocr
    LIBS bcrypt
)

# Stage 2 S-G seed: pure action catalog
zencrop_add_simple_hermetic(test_screenshot_action_catalog_contract
    INCLUDES
        ${CMAKE_SOURCE_DIR}/src/screenshot
        ${CMAKE_SOURCE_DIR}/src/screenshot/editor
)

# --- R0-S0B: inventory_skip entries for test_*.cpp not yet CMake executables ---
# Machine-readable inventory: tests/test_inventory.json
# These always SKIP (77) until a later slice promotes them to hermetic/runtime targets.
function(zencrop_add_inventory_skip name)
    cmake_parse_arguments(ARG "" "" "LABELS;REASON" ${ARGN})
    if(NOT ARG_LABELS)
        set(ARG_LABELS "inventory;manual_bat")
    endif()
    # Join labels for set_tests_properties (semicolon-separated list)
    set(_labels "${ARG_LABELS}")
    add_test(NAME ${name}
        COMMAND ${CMAKE_SOURCE_DIR}/tests/cmake/skip_inventory_test.cmd
            ${name}
            ${ARG_REASON}
    )
    set_tests_properties(${name} PROPERTIES
        LABELS "${_labels}"
        SKIP_RETURN_CODE 77
    )
endfunction()

zencrop_add_inventory_skip(test_batch_pdf_mixed_status_contract
    LABELS fixture;pdf;manual_bat;inventory
    REASON "PDF fixtures + multi-TU product link")
zencrop_add_inventory_skip(test_batch_pdf_output_contract
    LABELS fixture;pdf;manual_bat;inventory
    REASON "PDF fixtures + BatchOcrWriter")
zencrop_add_inventory_skip(test_batch_pdf_page_range_contract
    LABELS fixture;pdf;manual_bat;inventory
    REASON "PDF + page range fixtures")
zencrop_add_inventory_skip(test_batch_pdf_password_contract
    LABELS fixture;pdf;manual_bat;inventory
    REASON "password-protected PDF fixture")
zencrop_add_inventory_skip(test_cloud_native_pdf_materializer_contract
    LABELS fixture;pdf;manual_bat;inventory
    REASON "materializer fixtures")
zencrop_add_inventory_skip(test_cloud_provider_images_contract
    LABELS fixture;network;manual_bat;inventory
    REASON "provider images / optional network")
zencrop_add_inventory_skip(test_dashboard_pdf_flow_contract
    LABELS fixture;pdf;window;manual_bat;inventory
    REASON "Dashboard PDF flow host")
zencrop_add_inventory_skip(test_dashboard_runtime_contract
    LABELS runtime;window;manual_bat;inventory
    REASON "Dashboard HWND runtime")
zencrop_add_inventory_skip(test_dashboard_window_contract
    LABELS runtime;window;manual_bat;inventory
    REASON "window message contract")
zencrop_add_inventory_skip(test_ocr_engine_runtime_contract
    LABELS runtime;ocr;manual_bat;inventory
    REASON "engine runtime + models")
zencrop_add_inventory_skip(test_ocr_multiengine_runtime_contract
    LABELS runtime;ocr;manual_bat;inventory
    REASON "multi-engine runtime")
zencrop_add_inventory_skip(test_paddle_doc_crop_encoding_ab
    LABELS fixture;integration;manual_bat;inventory
    REASON "crop encoding A/B samples")
zencrop_add_inventory_skip(test_paddle_doc_engine_policy
    LABELS fixture;manual_bat;inventory
    REASON "engine policy fixtures")
zencrop_add_inventory_skip(test_paddle_doc_full_integration
    LABELS integration;fixture;manual_bat;inventory
    REASON "full doc pipeline")
zencrop_add_inventory_skip(test_paddle_doc_layout_engine_integration
    LABELS integration;opencv;manual_bat;inventory
    REASON "layout engine integration")
zencrop_add_inventory_skip(test_paddle_doc_layout_tile_reconciliation
    LABELS fixture;manual_bat;inventory
    REASON "tile reconciliation fixtures")
zencrop_add_inventory_skip(test_paddle_doc_sample_matrix
    LABELS fixture;matrix;manual_bat;inventory
    REASON "sample matrix driver")
zencrop_add_inventory_skip(test_paddle_doc_vlm_request
    LABELS fixture;network;manual_bat;inventory
    REASON "VLM request / optional net")
zencrop_add_inventory_skip(test_pdf_page_renderer
    LABELS fixture;pdf;manual_bat;inventory
    REASON "PDF page render stack")
zencrop_add_inventory_skip(test_pdf_render_benchmark
    LABELS benchmark;pdf;manual_bat;inventory
    REASON "benchmark not gate")
# Product builds target ZenCrop only. This explicit aggregate lets the test
# wrapper compile registered test binaries on demand without making them part
# of the normal build.bat path.
get_property(_zencrop_test_binary_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
list(FILTER _zencrop_test_binary_targets INCLUDE REGEX "^test_")
add_custom_target(zencrop_test_binaries)
if(_zencrop_test_binary_targets)
    add_dependencies(zencrop_test_binaries ${_zencrop_test_binary_targets})
endif()
