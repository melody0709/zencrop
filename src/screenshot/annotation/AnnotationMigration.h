#pragma once

#include "screenshot/annotation/AnnotationItem.h"
#include "screenshot/ScreenshotAnnotationLegacy.h"

// Convert legacy ScreenshotAnnotation to new ScreenshotAnnotationItem.
// New annotations should carry a stable legacy id; index is only retained as a
// fallback and as a stack-position hint during the bridge migration.
ScreenshotAnnotationItem convertLegacyAnnotation(const ScreenshotAnnotation& legacy, int index = -1);
void EnsureLegacyAnnotationId(ScreenshotAnnotation& legacy);
ScreenshotAnnotation LegacyAnnotationFromSnapshot(const AnnotationSnapshot& snap, const std::wstring& fallbackId = L"");
