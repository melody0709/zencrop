#include "screenshot/annotation/AnnotationProperty.h"
#include "screenshot/annotation/AnnotationValue.h"

// S-D-1: thin adapter — sole schema body is GetAnnotationPropertyKind.
PropertyValueType GetPropertyType(AnnotationProperty prop) {
    switch (GetAnnotationPropertyKind(prop)) {
    case AnnotationValueKind::Bool: return PropertyValueType::Bool;
    case AnnotationValueKind::Double: return PropertyValueType::Double;
    case AnnotationValueKind::Color: return PropertyValueType::Color;
    case AnnotationValueKind::String: return PropertyValueType::String;
    case AnnotationValueKind::Int:
    default: return PropertyValueType::Int;
    }
}
