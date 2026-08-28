#include "screenshot/annotation/AnnotationValue.h"
#include <iostream>

static int g_fail = 0;
static void Expect(bool c, const char* n) {
    if (!c) { std::cerr << "FAIL " << n << "\n"; ++g_fail; }
}

int main() {
    Expect(GetAnnotationPropertyKind(AnnotationProperty::PenWidth) == AnnotationValueKind::Int, "pen int");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::Filling) == AnnotationValueKind::Bool, "fill bool");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::Angle) == AnnotationValueKind::Double, "angle double");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::Color) == AnnotationValueKind::Color, "color");
    Expect(GetAnnotationPropertyKind(AnnotationProperty::PathPoints) == AnnotationValueKind::String, "path str");

    AnnotationValue vInt = 3;
    AnnotationValue vBool = true;
    Expect(AnnotationValueMatchesProperty(AnnotationProperty::PenWidth, vInt), "match pen");
    Expect(!AnnotationValueMatchesProperty(AnnotationProperty::PenWidth, vBool), "mismatch pen");
    Expect(AnnotationValueMatchesProperty(AnnotationProperty::Filling, vBool), "match fill");

    if (g_fail) return 1;
    std::cout << "ALL PASSED\n";
    return 0;
}
