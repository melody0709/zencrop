#pragma once

#include "TranslationProviderCatalog.h"

#include <windows.h>

#include <string>
#include <vector>

namespace translation {

// What the picker needs to render a provider's model catalogue.
//
// Three sources are deliberately kept apart instead of being merged into one
// list: an id the preset publishes cannot live in the user's pool (the pool
// contract drops catalog entries), an id the provider just reported can, and the
// pool is what the user already decided to keep.
//
// Entries carry the display name the listing reported next to the id. Only the id
// is a value: the picker never lets a name decide membership, order or the request.
struct ModelPickerRequest {
    std::vector<std::wstring> listed;      // preset catalogue ("Built-in")
    std::vector<ModelNameEntry> available; // just fetched from the provider
    std::vector<ModelNameEntry> pool;      // the user's saved custom models
    std::wstring active;                   // the profile's active model id
    size_t capacity = 0;                   // pool limit; 0 means unlimited
    // Longest id the typed-model field accepts (0 means unlimited). Passed in
    // rather than read from the settings contract so this dialog stays a view of
    // its request.
    size_t maxIdLength = 0;
    // Whether this profile may use an unlisted model at all. Asked here because
    // "Set active" on an unlisted id only joins the pool when the answer is yes, and
    // the capacity check has to know whether it will.
    bool allowsCustomModel = false;
};

struct ModelPickerResult {
    std::vector<ModelNameEntry> pool;
    std::wstring active;
    bool modified = false;
};

// Modal. Returns false when the user cancelled, in which case `result` is not
// touched.
bool ShowTranslationModelPicker(
    HWND owner, const ModelPickerRequest& request, ModelPickerResult& result);

} // namespace translation
