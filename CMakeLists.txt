cmake_minimum_required(VERSION 3.15)
project(ZenCrop VERSION 2.9.19)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ProductVersion has one authority: project(... VERSION ...). Generate the
# language-specific forms once per configure for source, VERSIONINFO, runtime
# diagnostics, and packaging.
include(${CMAKE_SOURCE_DIR}/cmake/ZenCropVersion.cmake)

# The WebView2 preview is intentionally an external, stable-path runtime
# payload. Its build-time manifest is compiled into ZenCrop.exe so an adjacent
# JSON file can remain diagnostics/package input rather than a trust root.
set(ZENCROP_WEB_ASSETS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/ocr/ui/webview_assets")
file(GLOB_RECURSE ZENCROP_WEB_ASSET_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${ZENCROP_WEB_ASSETS_SOURCE_DIR}/*")
if(NOT ZENCROP_WEB_ASSET_FILES)
    message(FATAL_ERROR "ZenCrop WebView2 assets are missing: ${ZENCROP_WEB_ASSETS_SOURCE_DIR}")
endif()

set(ZENCROP_WEB_ASSET_OUTPUT_DIR "${ZENCROP_GENERATED_INCLUDE_DIR}/web-assets")
set(ZENCROP_WEB_ASSET_HEADER "${ZENCROP_WEB_ASSET_OUTPUT_DIR}/WebAssetsManifest.generated.h")
set(ZENCROP_WEB_ASSET_JSON "${ZENCROP_WEB_ASSET_OUTPUT_DIR}/web-assets.manifest.json")
set(_zencrop_web_assets_generator "${CMAKE_SOURCE_DIR}/cmake/GenerateWebAssetsManifest.cmake")

function(zencrop_generate_web_assets_manifest)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DZENCROP_WEB_ASSETS_ROOT=${ZENCROP_WEB_ASSETS_SOURCE_DIR}"
            "-DZENCROP_WEB_ASSETS_HEADER=${ZENCROP_WEB_ASSET_HEADER}"
            "-DZENCROP_WEB_ASSETS_JSON=${ZENCROP_WEB_ASSET_JSON}"
            -P "${_zencrop_web_assets_generator}"
        RESULT_VARIABLE _zencrop_web_assets_result)
    if(NOT _zencrop_web_assets_result EQUAL 0)
        message(FATAL_ERROR "Failed to generate the ZenCrop web-asset manifest")
    endif()
endfunction()

# Run during configure so install-time diagnostics receive the same build id.
# The custom command below also tracks content changes during a direct build.
zencrop_generate_web_assets_manifest()
file(STRINGS "${ZENCROP_WEB_ASSET_HEADER}" _zencrop_web_asset_build_id_line
    REGEX "^#define ZENCROP_WEB_ASSET_BUILD_ID_A ")
file(STRINGS "${ZENCROP_WEB_ASSET_HEADER}" _zencrop_web_asset_set_sha_line
    REGEX "^#define ZENCROP_WEB_ASSET_SET_SHA256_A ")
if(NOT _zencrop_web_asset_build_id_line OR NOT _zencrop_web_asset_set_sha_line)
    message(FATAL_ERROR "Generated ZenCrop web-asset manifest has no build identity")
endif()
string(REPLACE "#define ZENCROP_WEB_ASSET_BUILD_ID_A \"" ""
    ZENCROP_WEB_ASSET_BUILD_ID "${_zencrop_web_asset_build_id_line}")
string(REPLACE "#define ZENCROP_WEB_ASSET_SET_SHA256_A \"" ""
    ZENCROP_WEB_ASSET_SET_SHA256 "${_zencrop_web_asset_set_sha_line}")
string(REPLACE "\"" "" ZENCROP_WEB_ASSET_BUILD_ID "${ZENCROP_WEB_ASSET_BUILD_ID}")
string(REPLACE "\"" "" ZENCROP_WEB_ASSET_SET_SHA256 "${ZENCROP_WEB_ASSET_SET_SHA256}")
set(ZENCROP_WEB_ASSET_MANIFEST_SCHEMA_VERSION 1)

add_custom_command(
    OUTPUT "${ZENCROP_WEB_ASSET_HEADER}" "${ZENCROP_WEB_ASSET_JSON}"
    COMMAND "${CMAKE_COMMAND}"
        "-DZENCROP_WEB_ASSETS_ROOT=${ZENCROP_WEB_ASSETS_SOURCE_DIR}"
        "-DZENCROP_WEB_ASSETS_HEADER=${ZENCROP_WEB_ASSET_HEADER}"
        "-DZENCROP_WEB_ASSETS_JSON=${ZENCROP_WEB_ASSET_JSON}"
        -P "${_zencrop_web_assets_generator}"
    DEPENDS ${ZENCROP_WEB_ASSET_FILES} "${_zencrop_web_assets_generator}"
    COMMENT "Generating ZenCrop WebView2 asset trust manifest"
    VERBATIM)
add_custom_target(zencrop_web_assets_manifest
    DEPENDS "${ZENCROP_WEB_ASSET_HEADER}" "${ZENCROP_WEB_ASSET_JSON}")

include(CTest)
option(ZENCROP_BUILD_TESTS "Build hermetic/contract CTest targets under tests/" ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(ZenCrop WIN32
    src/main.cpp
    src/core/Utils.h src/core/Utils.cpp
    src/core/Strings.h src/core/Strings.cpp
    src/core/Base64.h
    src/core/JsonUtils.h src/core/JsonUtils.cpp
    src/core/HotkeyEdit.h src/core/HotkeyEdit.cpp
    src/core/AppDataPaths.h src/core/AppDataPaths.cpp
    src/core/StartupRegistration.h src/core/StartupRegistration.cpp
    src/core/Settings.h src/core/Settings.cpp
    src/core/SettingsHotkeyDraft.h
    src/core/TranslationSettingsCodec.h src/core/TranslationSettingsCodec.cpp
    src/core/SettingsDialog.h src/core/SettingsDialog.cpp
    src/image/BitmapCodec.h src/image/BitmapCodec.cpp
    src/detect/SmartDetector.h src/detect/SmartDetector.cpp
    src/detect/SmartDetectorThread.h src/detect/SmartDetectorThread.cpp
    src/window/OverlayWindow.h src/window/OverlayWindow.cpp
    src/window/ReparentWindow.h src/window/ReparentWindow.cpp
    src/window/ThumbnailWindow.h src/window/ThumbnailWindow.cpp
    src/window/ViewportWindow.h src/window/ViewportWindow.cpp
    src/window/AlwaysOnTop.h src/window/AlwaysOnTop.cpp
    src/ocr/OcrUtils.h src/ocr/OcrUtils.cpp
    src/ocr/BitmapUtils.h src/ocr/BitmapUtils.cpp
    src/ocr/OcrTableUtils.h src/ocr/OcrTableUtils.cpp
    src/ocr/batch/BatchOcrTypes.h
    src/ocr/batch/BatchOcrController.h src/ocr/batch/BatchOcrController.cpp
    src/ocr/batch/BatchOcrManifest.h src/ocr/batch/BatchOcrManifest.cpp
    src/ocr/batch/BatchOcrImageLinks.h src/ocr/batch/BatchOcrImageLinks.cpp
    src/ocr/batch/PageRange.h src/ocr/batch/PageRange.cpp
    src/ocr/batch/PdfPageRenderer.h src/ocr/batch/PdfPageRenderer.cpp
    src/ocr/batch/BatchOcrWriter.h src/ocr/batch/BatchOcrWriter.cpp
    src/ocr/batch/BatchOcrOutputArtifacts.cpp
    src/ocr/OcrDocumentTypes.h src/ocr/OcrDocumentTypes.cpp
    src/ocr/OcrDocumentAlignment.h src/ocr/OcrDocumentAlignment.cpp
    src/ocr/PaddleCloudHttpClient.h
    src/ocr/document/PaddleCloudDocumentNormalizer.h src/ocr/document/PaddleCloudDocumentNormalizer.cpp
    src/ocr/document/PaddleCloudDocumentProtocol.h src/ocr/document/PaddleCloudDocumentProtocol.cpp
    src/ocr/document/PaddleCloudDocumentTransport.h src/ocr/document/PaddleCloudDocumentTransport.cpp
    src/ocr/document/PaddleCloudImageOcrWorkflow.h src/ocr/document/PaddleCloudImageOcrWorkflow.cpp
    # Stage3 3-A-2: Materializer ownership in batch (document↛batch reverse deleted).
    src/ocr/batch/PaddleCloudDocumentMaterializer.h src/ocr/batch/PaddleCloudDocumentMaterializer.cpp
    src/ocr/engine/OcrEngine.h src/ocr/engine/OcrEngine.cpp
    src/ocr/engine/OcrEngine_Local.h src/ocr/engine/OcrEngine_Local.cpp
    src/ocr/engine/OcrEngine_PaddleOCR_Cloud.h src/ocr/engine/OcrEngine_PaddleOCR_Cloud.cpp
    src/ocr/engine/OcrEngine_PaddleOCR_Local.h src/ocr/engine/OcrEngine_PaddleOCR_Local.cpp
    src/ocr/engine/OcrEngine_PaddleOCR_Doc.h src/ocr/engine/OcrEngine_PaddleOCR_Doc.cpp
    src/ocr/engine/PPOcrV6OrtSession.h src/ocr/engine/PPOcrV6OrtSession.cpp
    src/ocr/engine/PPOcrV6DetectorPostprocess.cpp
    src/ocr/engine/PaddleDocRegionGrouping.h src/ocr/engine/PaddleDocRegionGrouping.cpp
    src/ocr/engine/PaddleDocRecognitionImage.h src/ocr/engine/PaddleDocRecognitionImage.cpp
    src/ocr/engine/PaddleDocEnginePolicy.h
    src/ocr/engine/PaddleVlLlamaClient.h src/ocr/engine/PaddleVlLlamaClient.cpp
    src/ocr/engine/OcrEngine_PPOCRv6_ONNX.h src/ocr/engine/OcrEngine_PPOCRv6_ONNX.cpp
    third_party/clipper2/src/clipper.engine.cpp
    third_party/clipper2/src/clipper.offset.cpp
    third_party/clipper2/src/clipper.rectclip.cpp
    third_party/clipper2/src/clipper.triangulation.cpp
    src/ocr/LocalRaster.h src/ocr/LocalRaster.cpp
    src/ocr/layout/LayoutEngine.h src/ocr/layout/LayoutEngine.cpp
    src/ocr/layout/PaddleDocLayoutPreprocessor.h src/ocr/layout/PaddleDocLayoutPreprocessor.cpp
    src/ocr/layout/PaddleDocLayoutProfile.h
    src/ocr/layout/PaddleDocLayoutPostprocess.h src/ocr/layout/PaddleDocLayoutPostprocess.cpp
    src/ocr/ui/OcrResultWindow.h src/ocr/ui/OcrResultWindow.cpp
    src/ocr/ui/OcrProgressWindow.h src/ocr/ui/OcrProgressWindow.cpp
    src/ocr/ui/OcrCopyToastWindow.h src/ocr/ui/OcrCopyToastWindow.cpp
    src/ocr/ui/OcrModelDownloadDialog.h src/ocr/ui/OcrModelDownloadDialog.cpp
    src/ocr/model_download/OcrModelDownloadTypes.h
    src/ocr/model_download/OcrModelDownloadCatalog.h src/ocr/model_download/OcrModelDownloadCatalog.cpp
    src/ocr/model_download/OcrModelInstaller.h src/ocr/model_download/OcrModelInstaller.cpp
    src/ocr/model_download/OcrModelDownloadService.h src/ocr/model_download/OcrModelDownloadService.cpp
    src/ocr/ui/DashboardLayoutState.h
    src/ocr/ui/DashboardModels.h
    src/ocr/ui/OcrDashboardWindow.h src/ocr/ui/OcrDashboardWindow.cpp
    # D-I-4: all former class-method .inl are real TUs.
    src/ocr/ui/dashboard/OcrDashboardWindow.StateAndHelpers.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.EntryPoints.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.Lifecycle.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.Layout.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.SourceRail.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.ImagePreview.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.Import.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.Blocks.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.Batch.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.Messages.cpp
    src/ocr/ui/dashboard/OcrDashboardWindow.HistoryPaint.cpp
    src/ocr/ui/dashboard/DashboardTheme.h
    src/ocr/ui/dashboard/DashboardHostIds.h
    src/ocr/ui/dashboard/DashboardHostTypes.h
    src/ocr/ui/dashboard/DashboardHostInternals.h
    src/ocr/ui/dashboard/DashboardHostUtils.h
    src/ocr/ui/dashboard/DashboardHostUtils.cpp
    src/ocr/ui/dashboard/DashboardFileTypes.h src/ocr/ui/dashboard/DashboardFileTypes.cpp
    src/ocr/ui/dashboard/DashboardOleDropTarget.h src/ocr/ui/dashboard/DashboardOleDropTarget.cpp
    src/ocr/ui/dashboard/DashboardDialogLayout.h
    src/ocr/ui/dashboard/DashboardPdfPasswordDialog.h src/ocr/ui/dashboard/DashboardPdfPasswordDialog.cpp
    src/ocr/ui/dashboard/DashboardFolderImportOptionsDialog.h src/ocr/ui/dashboard/DashboardFolderImportOptionsDialog.cpp
    src/ocr/ui/dashboard/DashboardOutputArtifactOptionsDialog.h src/ocr/ui/dashboard/DashboardOutputArtifactOptionsDialog.cpp
    src/ocr/ui/dashboard/DashboardPdfOptionsDialog.h
    src/ocr/ui/dashboard/DashboardPdfOptionsDialogInternals.h
    src/ocr/ui/dashboard/DashboardPdfOptionsDialog.cpp
    src/ocr/ui/dashboard/DashboardPdfOptionsPreflight.cpp
    src/ocr/ui/dashboard/DashboardPdfOptionsPreview.cpp
    src/ocr/ui/dashboard/DashboardHistoryStore.h src/ocr/ui/dashboard/DashboardHistoryStore.cpp
    src/ocr/ui/dashboard/DashboardHistoryRepository.h src/ocr/ui/dashboard/DashboardHistoryRepository.cpp
    src/ocr/ui/dashboard/DashboardHistoryCache.h src/ocr/ui/dashboard/DashboardHistoryCache.cpp
    src/ocr/ui/dashboard/DashboardResultProjection.h src/ocr/ui/dashboard/DashboardResultProjection.cpp
    src/ocr/ui/dashboard/DashboardTranslationCache.h src/ocr/ui/dashboard/DashboardTranslationCache.cpp
    src/ocr/ui/dashboard/DashboardCommand.h
    src/ocr/ui/dashboard/DashboardEvent.h
    src/ocr/ui/dashboard/DashboardController.h src/ocr/ui/dashboard/DashboardController.cpp
    src/ocr/ui/dashboard/DashboardBatchCoordinator.h
    src/ocr/ui/dashboard/DashboardSourceRailModel.h src/ocr/ui/dashboard/DashboardSourceRailModel.cpp
    src/ocr/ui/dashboard/DashboardCanvasModel.h
    src/ocr/ui/dashboard/DashboardCanvasMath.h
    src/ocr/ui/dashboard/DashboardPreviewCoordinator.h
    src/ocr/ui/dashboard/DashboardPreviewSecurity.h
    src/ocr/ui/DashboardHistory.cpp
    src/ocr/ui/WebAssetGuard.h src/ocr/ui/WebAssetGuard.cpp
    src/ocr/ui/OcrMarkdownPreviewHost.h src/ocr/ui/OcrMarkdownPreviewHost.cpp
    src/ocr/ui/OcrMarkdownPreviewProtocol.h src/ocr/ui/OcrMarkdownPreviewProtocol.cpp
    src/core/HttpTransport.h src/net/Network.cpp
    src/selection/SelectionTypes.h src/selection/SelectionTypes.cpp
    src/selection/ClipboardCopyPolicy.h src/selection/ClipboardCopyPolicy.cpp
    src/selection/ClipboardDataSnapshot.h src/selection/ClipboardDataSnapshot.cpp
    src/selection/ClipboardCopyTransaction.h src/selection/ClipboardCopyTransaction.cpp
    src/selection/SelectionTextAcquirer.h src/selection/SelectionTextAcquirer.cpp
    src/selection/SelectionTranslationToastWindow.h src/selection/SelectionTranslationToastWindow.cpp
    src/selection/SelectionTranslationController.h src/selection/SelectionTranslationController.cpp
    src/translation/TranslationTypes.h
    src/translation/TranslationLaunchContext.h
    src/translation/TranslationPreflight.h src/translation/TranslationPreflight.cpp
    src/translation/TranslationEngine.h
    src/translation/TranslationCredentialStore.h src/translation/TranslationCredentialStore.cpp
    src/translation/AsyncHttpTransport.h src/translation/AsyncHttpTransport.cpp
    src/translation/LlmModelPolicy.h src/translation/LlmModelPolicy.cpp
    src/translation/TranslationProviderCatalog.h src/translation/TranslationProviderCatalog.cpp
    src/translation/TranslationPromptComposer.h src/translation/TranslationPromptComposer.cpp
    src/translation/OpenAICompatibleTranslationEngine.h src/translation/OpenAICompatibleTranslationEngine.cpp
    src/translation/MachineTranslationEngine.h src/translation/MachineTranslationEngine.cpp
    src/translation/TranslationEngineFactory.h src/translation/TranslationEngineFactory.cpp
    src/translation/DeepSeekTranslationEngine.h src/translation/DeepSeekTranslationEngine.cpp
    src/translation/TranslationSettingsPage.h src/translation/TranslationSettingsPage.cpp
    src/translation/TranslationProviderSettingsPage.h src/translation/TranslationProviderSettingsPage.cpp
    src/translation/TranslationPromptSettingsPage.h src/translation/TranslationPromptSettingsPage.cpp
    src/translation/TranslationResultWindow.h src/translation/TranslationResultWindow.cpp
    src/translation/TranslationCoordinator.h src/translation/TranslationCoordinator.cpp
    src/net/WinHttpFileDownloader.h src/net/WinHttpFileDownloader.cpp
    src/net/TcpHelper.h src/net/TcpHelper.cpp
    src/net/LlamaServerManager.h src/net/LlamaServerManager.cpp
    src/net/MiniHttpServer.h src/net/MiniHttpServer.cpp
    src/core/Sha256.h src/core/Sha256.cpp
    # Stage3 3-A-3: platform clipboard sole in core (ocr_ui↛screenshot reverse).
    src/core/ClipboardUtils.h src/core/ClipboardUtils.cpp
    src/screenshot/ScreenshotTypes.h
    src/screenshot/ScreenshotUtils.h src/screenshot/ScreenshotUtils.cpp
    src/screenshot/ScreenshotEditorWindow.h src/screenshot/ScreenshotEditorWindow.cpp
    src/screenshot/PinnedImageWindow.h src/screenshot/PinnedImageWindow.cpp
    src/screenshot/ScreenshotSession.h src/screenshot/ScreenshotSession.cpp
    src/screenshot/longshot/LongShotTypes.h
    src/screenshot/longshot/LongShotScrollInjector.h src/screenshot/longshot/LongShotScrollInjector.cpp
    src/screenshot/longshot/LongShotImage.h src/screenshot/longshot/LongShotImage.cpp
    src/screenshot/longshot/LongShotStitcher.h src/screenshot/longshot/LongShotStitcher.cpp
    src/screenshot/longshot/LongShotExport.h src/screenshot/longshot/LongShotExport.cpp
    src/screenshot/longshot/LongShotSession.h src/screenshot/longshot/LongShotSession.cpp
    src/screenshot/ToolbarIconRenderer.h src/screenshot/ToolbarIconRenderer.cpp
    src/screenshot/ScreenshotPixelUtils.h src/screenshot/ScreenshotPixelUtils.cpp
    src/screenshot/ScreenshotImageUtils.h src/screenshot/ScreenshotImageUtils.cpp
    src/screenshot/ScreenshotAnnotationLegacy.h
    src/screenshot/ScreenshotAnnotationGeometry.h src/screenshot/ScreenshotAnnotationGeometry.cpp
    src/screenshot/ScreenshotAnnotationHelpers.h src/screenshot/ScreenshotAnnotationHelpers.cpp
    src/screenshot/ScreenshotToolbarText.h src/screenshot/ScreenshotToolbarText.cpp
    src/screenshot/ScreenshotOverlayRuntime.h src/screenshot/ScreenshotOverlayRuntime.cpp
    src/screenshot/ScreenshotColorFormat.h src/screenshot/ScreenshotColorFormat.cpp
    src/screenshot/ScreenshotKeyboardShortcuts.h
    src/screenshot/editor/ScreenshotToolbarPanelState.cpp
    src/screenshot/editor/ScreenshotToolbarSliderMutation.h src/screenshot/editor/ScreenshotToolbarSliderMutation.cpp
    src/screenshot/editor/ScreenshotToolbarColorMutation.h src/screenshot/editor/ScreenshotToolbarColorMutation.cpp
    src/screenshot/HoverMagnifierWidget.h src/screenshot/HoverMagnifierWidget.cpp
    src/screenshot/CropAdjustMath.h src/screenshot/CropAdjustMath.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.h src/screenshot/overlay/OverlayWindowScreenshot.ArrowGeometry.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.AnnotationHitTest.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.Surface.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.Export.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.AnnotationRender.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.Settings.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.ToolbarRender.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.AnnotationEdit.cpp
    src/screenshot/overlay/OverlayWindowScreenshot.ColorPickerDialog.cpp
    src/screenshot/OverlayWindowScreenshot.cpp
    src/screenshot/annotation/AnnotationTypes.h src/screenshot/annotation/AnnotationTypes.cpp
    src/screenshot/annotation/AnnotationProperty.h src/screenshot/annotation/AnnotationProperty.cpp
    src/screenshot/annotation/AnnotationValue.h src/screenshot/annotation/AnnotationValue.cpp
    src/screenshot/annotation/AnnotationItem.h src/screenshot/annotation/AnnotationItem.cpp
    src/screenshot/annotation/AnnotationModel.h src/screenshot/annotation/AnnotationModel.cpp
    src/screenshot/annotation/AnnotationMigration.h src/screenshot/annotation/AnnotationMigration.cpp
    src/screenshot/annotation/AnnotationHistory.h src/screenshot/annotation/AnnotationHistory.cpp
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
    src/resources.rc
)

target_compile_definitions(ZenCrop PRIVATE
    UNICODE
    _UNICODE
    WINRT_LEAN_AND_MEAN
    MINIZ_NO_ARCHIVE_WRITING_APIS=1
)

target_include_directories(ZenCrop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
add_dependencies(ZenCrop zencrop_web_assets_manifest)

if(MSVC)
    target_compile_options(ZenCrop PRIVATE /await /utf-8)
    target_link_options(ZenCrop PRIVATE /MANIFEST:NO)
endif()

target_include_directories(ZenCrop PRIVATE
    ${ZENCROP_GENERATED_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/detect
    ${CMAKE_CURRENT_SOURCE_DIR}/src/window
    ${CMAKE_CURRENT_SOURCE_DIR}/src/image
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ocr
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ocr/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ocr/layout
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ocr/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ocr/batch
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ocr/document
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
    ${CMAKE_CURRENT_SOURCE_DIR}/src/screenshot
    ${CMAKE_CURRENT_SOURCE_DIR}/src/screenshot/overlay
    ${CMAKE_CURRENT_SOURCE_DIR}/src/screenshot/annotation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/screenshot/render
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imagecodecs/libwebp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ocr/onnxruntime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/clipper2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)

set_target_properties(ZenCrop PROPERTIES
    WIN32_EXECUTABLE YES
)

target_link_libraries(ZenCrop PRIVATE
    user32
    gdi32
    imm32
    gdiplus
    dwmapi
    shcore
    shell32
    ole32
    oleaut32
    oleacc
    uiautomationcore
    shlwapi
    comdlg32
    advapi32
    bcrypt
    windowsapp
    winhttp
    ws2_32
    uxtheme
    windowscodecs
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/x64/WebView2Loader.dll.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imagecodecs/libwebp/lib/libwebp.lib
    onnxruntime
)

target_link_directories(ZenCrop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ocr/onnxruntime/lib
)

set(ZENCROP_LOCAL_OPENCV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv")
if(EXISTS "${ZENCROP_LOCAL_OPENCV_ROOT}/OpenCVConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${ZENCROP_LOCAL_OPENCV_ROOT}")
elseif(EXISTS "${ZENCROP_LOCAL_OPENCV_ROOT}/build/OpenCVConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${ZENCROP_LOCAL_OPENCV_ROOT}/build")
endif()

# Release-style presets (CMakePresets x64-release) set ZENCROP_REQUIRE_OPENCV=ON
# so OpenCV cannot be silently dropped. Raw CMake without this option may still
# configure with OpenCV optional (diagnostic / experimental only).
option(ZENCROP_ENABLE_OPENCV_DBPOST "Enable OpenCV-backed PP-OCRv6 DBPostProcess when OpenCV is available" ON)
option(ZENCROP_REQUIRE_OPENCV "Fail configure if OpenCV is missing (release builds must set ON)" OFF)
set(ZENCROP_OPENCV_FOUND FALSE)
if(ZENCROP_ENABLE_OPENCV_DBPOST)
    find_package(OpenCV QUIET COMPONENTS core geometry imgproc)
    if(OpenCV_FOUND)
        set(ZENCROP_OPENCV_FOUND TRUE)
        target_compile_definitions(ZenCrop PRIVATE
            ZENCROP_WITH_OPENCV_DBPOST=1
            ZENCROP_WITH_OPENCV_LAYOUT=1
        )
        target_include_directories(ZenCrop PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(ZenCrop PRIVATE ${OpenCV_LIBS})
        message(STATUS "PP-OCRv6 OpenCV DBPostProcess enabled: ${OpenCV_VERSION}")

        set(ZENCROP_OPENCV_BIN_DIR "")
        foreach(_opencv_vc vc18 vc17 vc16 vc15 vc14)
            if(NOT ZENCROP_OPENCV_BIN_DIR AND EXISTS "${ZENCROP_LOCAL_OPENCV_ROOT}/x64/${_opencv_vc}/bin")
                set(ZENCROP_OPENCV_BIN_DIR "${ZENCROP_LOCAL_OPENCV_ROOT}/x64/${_opencv_vc}/bin")
            endif()
        endforeach()
        if(ZENCROP_OPENCV_BIN_DIR)
            file(GLOB ZENCROP_OPENCV_RUNTIME_DLLS
                "${ZENCROP_OPENCV_BIN_DIR}/opencv_world*.dll"
                "${ZENCROP_OPENCV_BIN_DIR}/opencv_core*.dll"
                "${ZENCROP_OPENCV_BIN_DIR}/opencv_geometry*.dll"
                "${ZENCROP_OPENCV_BIN_DIR}/opencv_imgproc*.dll"
            )
            list(FILTER ZENCROP_OPENCV_RUNTIME_DLLS EXCLUDE REGEX "d\\.dll$")
        endif()
    else()
        if(ZENCROP_REQUIRE_OPENCV)
            message(FATAL_ERROR
                "OpenCV is required (ZENCROP_REQUIRE_OPENCV=ON) but was not found. "
                "Install under third_party/opencv or set CMAKE_PREFIX_PATH. "
                "Do not use a silent non-OpenCV release build.")
        endif()
        message(STATUS "PP-OCRv6 OpenCV DBPostProcess disabled: OpenCV not found")
    endif()
elseif(ZENCROP_REQUIRE_OPENCV)
    message(FATAL_ERROR "ZENCROP_REQUIRE_OPENCV=ON but ZENCROP_ENABLE_OPENCV_DBPOST is OFF")
endif()

# Canonical runtime install rules. build.bat installs these rules into the one
# supported runnable tree after every incremental product build.
include(${CMAKE_SOURCE_DIR}/cmake/ZenCropRuntime.cmake)
zencrop_install_runtime(ZenCrop)


# Stage 0-C: hermetic contract tests (subset; full 48 targets later)
if(ZENCROP_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
