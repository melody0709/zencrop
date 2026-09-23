#pragma once

// OWN-74: pure wide-string helpers (umbrella aggregator).
// Header-only foundation for core, OCR, and UI layers.
// Sub-headers split for granular inclusion:
//   - WideTextOps.h:       Case, trim, equals, search, URL & auth tokens
//   - WidePathUtils.h:     Path manipulation, extension helpers, directory checks
//   - WideColorUtils.h:    Color parsing (hex/RGB), packing, UI cycling
//   - WideFormatUtils.h:   Pure presentation formatters (Win32, sizes, times, labels)
//   - WideJsonUtils.h:     JSON traversal, extraction, escaping, serialization
//   - WideMarkdownUtils.h: Markdown image scanning and text projection

#include "core/WideTextOps.h"
#include "core/WidePathUtils.h"
#include "core/WideColorUtils.h"
#include "core/WideFormatUtils.h"
#include "core/WideJsonUtils.h"
#include "core/WideMarkdownUtils.h"
