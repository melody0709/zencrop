# ZenCropVersion.cmake -- project() is the sole product-version authority.

if(NOT PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "ZenCrop requires a three-part project version (major.minor.patch); got '${PROJECT_VERSION}'.")
endif()

# Windows Installer stores the public ProductVersion as three bounded integer
# fields.  Enforce that contract at the sole version authority so Portable
# packaging cannot publish a version that a future MSI could never upgrade.
if(PROJECT_VERSION_MAJOR GREATER 255 OR
   PROJECT_VERSION_MINOR GREATER 255 OR
   PROJECT_VERSION_PATCH GREATER 65535)
    message(FATAL_ERROR
        "ZenCrop version '${PROJECT_VERSION}' exceeds Windows Installer limits: "
        "major and minor must be <= 255; patch must be <= 65535.")
endif()

set(ZENCROP_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ZENCROP_GENERATED_INCLUDE_DIR}")

set(ZENCROP_VERSION_RESOURCE_REVISION 0)
set(ZENCROP_VERSION_FILE_STRING "${PROJECT_VERSION}.0")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/Version.generated.h.in"
    "${ZENCROP_GENERATED_INCLUDE_DIR}/Version.generated.h"
    @ONLY)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/VersionResource.generated.h.in"
    "${ZENCROP_GENERATED_INCLUDE_DIR}/VersionResource.generated.h"
    @ONLY)
file(WRITE "${ZENCROP_GENERATED_INCLUDE_DIR}/zencrop-version.txt" "${PROJECT_VERSION}\n")
