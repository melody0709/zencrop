# GenerateWebAssetsManifest.cmake -- deterministic WebView2 asset inventory.
# This is the sole asset enumerator for both the build-time trust header and
# the package-facing JSON manifest. Runtime verification never trusts the JSON.

cmake_minimum_required(VERSION 3.15)

foreach(_required IN ITEMS
    ZENCROP_WEB_ASSETS_ROOT
    ZENCROP_WEB_ASSETS_HEADER
    ZENCROP_WEB_ASSETS_JSON)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "GenerateWebAssetsManifest: ${_required} is required")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${ZENCROP_WEB_ASSETS_ROOT}")
    message(FATAL_ERROR
        "GenerateWebAssetsManifest: asset root does not exist: ${ZENCROP_WEB_ASSETS_ROOT}")
endif()

file(GLOB_RECURSE _absolute_files LIST_DIRECTORIES false
    "${ZENCROP_WEB_ASSETS_ROOT}/*")
if(NOT _absolute_files)
    message(FATAL_ERROR "GenerateWebAssetsManifest: asset root is empty")
endif()

set(_relative_paths "")
set(_folded_paths "")
foreach(_absolute_path IN LISTS _absolute_files)
    if(IS_SYMLINK "${_absolute_path}")
        message(FATAL_ERROR
            "GenerateWebAssetsManifest: symlink/reparse asset is not allowed: ${_absolute_path}")
    endif()
    if(IS_DIRECTORY "${_absolute_path}")
        continue()
    endif()

    file(RELATIVE_PATH _relative_path "${ZENCROP_WEB_ASSETS_ROOT}" "${_absolute_path}")
    file(TO_CMAKE_PATH "${_relative_path}" _relative_path)
    string(FIND "${_relative_path}" "\\" _backslash_index)
    string(FIND "${_relative_path}" "\"" _quote_index)
    string(HEX "${_relative_path}" _relative_path_hex)
    string(LENGTH "${_relative_path_hex}" _hex_length)
    set(_has_control_character FALSE)
    if(_hex_length GREATER 1)
        math(EXPR _last_hex_offset "${_hex_length} - 2")
        foreach(_hex_offset RANGE 0 ${_last_hex_offset} 2)
            string(SUBSTRING "${_relative_path_hex}" ${_hex_offset} 2 _hex_byte)
            if(_hex_byte MATCHES "^0[0-9a-f]$" OR _hex_byte MATCHES "^1[0-9a-f]$")
                set(_has_control_character TRUE)
            endif()
        endforeach()
    endif()
    if(_relative_path STREQUAL "" OR _relative_path MATCHES "^/" OR
       _relative_path MATCHES "(^|/)[.][.]?(/|$)" OR _has_control_character)
        message(FATAL_ERROR
            "GenerateWebAssetsManifest: invalid normalized relative asset path: ${_relative_path}")
    endif()
    if(NOT "${_backslash_index}" STREQUAL "-1" OR NOT "${_quote_index}" STREQUAL "-1")
        message(FATAL_ERROR
            "GenerateWebAssetsManifest: invalid normalized relative asset path: ${_relative_path}")
    endif()
    string(TOLOWER "${_relative_path}" _folded_path)
    list(FIND _folded_paths "${_folded_path}" _collision_index)
    if(NOT _collision_index EQUAL -1)
        message(FATAL_ERROR
            "GenerateWebAssetsManifest: case-insensitive path collision: ${_relative_path}")
    endif()
    list(APPEND _relative_paths "${_relative_path}")
    list(APPEND _folded_paths "${_folded_path}")
endforeach()

list(SORT _relative_paths)
set(_aggregate "")
set(_json_entries "")
set(_header_entries "")
set(_entry_index 0)
foreach(_relative_path IN LISTS _relative_paths)
    set(_absolute_path "${ZENCROP_WEB_ASSETS_ROOT}/${_relative_path}")
    file(SIZE "${_absolute_path}" _file_size)
    file(SHA256 "${_absolute_path}" _file_sha256)
    string(TOLOWER "${_file_sha256}" _file_sha256)
    string(APPEND _aggregate "${_file_sha256} ${_file_size} ${_relative_path}\n")

    if(_entry_index GREATER 0)
        string(APPEND _json_entries ",\n")
    endif()
    string(APPEND _json_entries
        "    { \"path\": \"${_relative_path}\", \"size\": ${_file_size}, \"sha256\": \"${_file_sha256}\" }")
    string(APPEND _header_entries
        "    { L\"${_relative_path}\", ${_file_size}ULL, L\"${_file_sha256}\" },\n")
    math(EXPR _entry_index "${_entry_index} + 1")
endforeach()

string(SHA256 _asset_set_sha256 "${_aggregate}")
string(TOLOWER "${_asset_set_sha256}" _asset_set_sha256)
string(SUBSTRING "${_asset_set_sha256}" 0 32 _asset_build_id)
set(_asset_host "zencrop-assets-${_asset_build_id}.invalid")
set(_preview_url "https://${_asset_host}/ocr-preview/index.html")
set(_preview_prefix "https://${_asset_host}/ocr-preview/")

get_filename_component(_header_dir "${ZENCROP_WEB_ASSETS_HEADER}" DIRECTORY)
get_filename_component(_json_dir "${ZENCROP_WEB_ASSETS_JSON}" DIRECTORY)
file(MAKE_DIRECTORY "${_header_dir}" "${_json_dir}")

function(zencrop_write_if_different _path _content)
    set(_temporary_path "${_path}.tmp")
    file(WRITE "${_temporary_path}" "${_content}")
    if(EXISTS "${_path}")
        file(SHA256 "${_path}" _existing_sha256)
        file(SHA256 "${_temporary_path}" _temporary_sha256)
        if(_existing_sha256 STREQUAL _temporary_sha256)
            file(REMOVE "${_temporary_path}")
            return()
        endif()
    endif()
    file(RENAME "${_temporary_path}" "${_path}")
endfunction()

set(_header "#pragma once\n\n")
string(APPEND _header "// Generated by cmake/GenerateWebAssetsManifest.cmake. Do not hand-edit.\n")
string(APPEND _header "#include <cstddef>\n#include <cstdint>\n\n")
string(APPEND _header "#define ZENCROP_WEB_ASSET_BUILD_ID_A \"${_asset_build_id}\"\n")
string(APPEND _header "#define ZENCROP_WEB_ASSET_SET_SHA256_A \"${_asset_set_sha256}\"\n\n")
string(APPEND _header "namespace ZenCrop::WebAssets {\n")
string(APPEND _header "inline constexpr std::uint32_t kSchemaVersion = 1;\n")
string(APPEND _header "inline constexpr wchar_t kAssetSetSha256[] = L\"${_asset_set_sha256}\";\n")
string(APPEND _header "inline constexpr wchar_t kAssetBuildId[] = L\"${_asset_build_id}\";\n")
string(APPEND _header "inline constexpr wchar_t kHostName[] = L\"${_asset_host}\";\n")
string(APPEND _header "inline constexpr wchar_t kPreviewUrl[] = L\"${_preview_url}\";\n")
string(APPEND _header "inline constexpr wchar_t kPreviewUrlPrefix[] = L\"${_preview_prefix}\";\n")
string(APPEND _header "struct AssetEntry { const wchar_t* path; std::uint64_t size; const wchar_t* sha256; };\n")
string(APPEND _header "inline constexpr AssetEntry kEntries[] = {\n${_header_entries}};\n")
string(APPEND _header "inline constexpr std::size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);\n")
string(APPEND _header "} // namespace ZenCrop::WebAssets\n")

set(_json "{\n")
string(APPEND _json "  \"schemaVersion\": 1,\n")
string(APPEND _json "  \"assetSetSha256\": \"${_asset_set_sha256}\",\n")
string(APPEND _json "  \"assetBuildId\": \"${_asset_build_id}\",\n")
string(APPEND _json "  \"hostName\": \"${_asset_host}\",\n")
string(APPEND _json "  \"previewUrl\": \"${_preview_url}\",\n")
string(APPEND _json "  \"files\": [\n${_json_entries}\n  ]\n}\n")

zencrop_write_if_different("${ZENCROP_WEB_ASSETS_HEADER}" "${_header}")
zencrop_write_if_different("${ZENCROP_WEB_ASSETS_JSON}" "${_json}")
message(STATUS "Generated ZenCrop WebView2 asset manifest: ${_entry_index} files, ${_asset_build_id}")
