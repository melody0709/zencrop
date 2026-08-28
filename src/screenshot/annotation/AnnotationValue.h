#pragma once
#include "screenshot/annotation/AnnotationProperty.h"
#include <variant>
#include <string>
#include <windows.h>

// S-D-1: sole property-kind schema + single-type-per-key contract.
// GetAnnotationPropertyKind is sole schema body; GetPropertyType is thin adapter.
// Item/Snapshot setters clear other typed maps so one key cannot dual-store.

enum class AnnotationValueKind {
    Int,
    Bool,
    Double,
    Color,
    String,
    Unknown
};

using AnnotationValue = std::variant<int, bool, double, COLORREF, std::wstring>;

// Canonical type for each property key (schema). Unknown => caller must not set.
AnnotationValueKind GetAnnotationPropertyKind(AnnotationProperty p);

// Returns false if kind mismatches schema.
bool AnnotationValueMatchesProperty(AnnotationProperty p, const AnnotationValue& v);
