#include "screenshot/annotation/AnnotationEditSession.h"
#include "screenshot/annotation/AnnotationLegacyDocument.h"
#include "screenshot/ScreenshotTypes.h"

#include <iostream>

static int g_fail = 0;

static void Expect(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL " << name << "\n";
        ++g_fail;
    } else {
        std::cout << "PASS " << name << "\n";
    }
}

static ScreenshotAnnotation MakeAnnotation(
    const std::wstring& id,
    ScreenshotToolbarCommand type)
{
    ScreenshotAnnotation annotation;
    annotation.id = id;
    annotation.type = type;
    annotation.start = {0, 0};
    annotation.end = {10, 10};
    return annotation;
}

int main() {
    // Document is the sole committed store. Creation selects by stable id and
    // exposes only an ephemeral projection for rendering/layout.
    {
        AnnotationDocument document;
        ScreenshotEditorState state;
        ScreenshotAnnotation geometry =
            MakeAnnotation(L"geometry", ScreenshotToolbarCommand::ToolGeometry);
        geometry.penWidth = 6;

        const int index = ScreenshotAnnotationDocumentCreate(document, geometry, state);
        Expect(index == 0, "create index");
        Expect(document.count() == 1, "create document count");
        Expect(ScreenshotEditorSelectedAnnotationId(state) == L"geometry", "create selected id");
        Expect(document.activeItem() != nullptr && document.activeItem()->id() == L"geometry",
            "create active document item");

        const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(document);
        Expect(ordered.size() == 1 && ordered[0].id == L"geometry", "create ephemeral projection");
    }

    // Pending text has a committed item but no accidental selection authority.
    {
        AnnotationDocument document;
        ScreenshotEditorState state;
        ScreenshotAnnotation text = MakeAnnotation(L"pending-text", ScreenshotToolbarCommand::ToolText);

        const int index = ScreenshotAnnotationDocumentCreatePendingText(document, text, state);
        Expect(index == 0, "pending text index");
        Expect(document.count() == 1, "pending text document count");
        Expect(ScreenshotEditorAnnotationCount(state) == 1, "pending text editor count");
        Expect(ScreenshotEditorSelectedAnnotationId(state).empty(), "pending text does not select");
    }

    // A draft/local annotation commits through Document and history snapshots
    // read back from Document rather than a second mutable vector.
    {
        AnnotationDocument document;
        ScreenshotEditorState state;
        ScreenshotAnnotation text = MakeAnnotation(L"editable", ScreenshotToolbarCommand::ToolText);
        text.text = L"before";
        ScreenshotAnnotationDocumentCreate(document, text, state);

        const AnnotationSnapshot before =
            ScreenshotAnnotationDocumentCaptureBeforeSnapshot(document, text, 0);
        text.text = L"after";
        AnnotationSnapshot after;
        Expect(ScreenshotAnnotationDocumentCommitModify(
                   document, text, 0, ScreenshotEditorSelectedAnnotationId(state), after),
            "commit modify");
        Expect(before.text == L"before", "before snapshot from document");
        Expect(after.text == L"after", "after snapshot from document");

        ScreenshotAnnotation resolved;
        Expect(ScreenshotAnnotationDocumentTryLegacyById(document, L"editable", resolved),
            "resolve committed item");
        Expect(resolved.text == L"after", "resolved committed content");
    }

    // History restore works on Document snapshots without a Host-vector rebuild.
    {
        AnnotationDocument document;
        ScreenshotAnnotation original =
            MakeAnnotation(L"restore", ScreenshotToolbarCommand::ToolArrow);
        original.arrowShape = 2;
        ScreenshotEditorState state;
        ScreenshotAnnotationDocumentCreate(document, original, state);

        AnnotationSnapshot snapshot;
        Expect(ScreenshotAnnotationDocumentTakeSnapshotById(document, L"restore", snapshot),
            "take history snapshot");
        snapshot.text = L"restored";
        Expect(ScreenshotAnnotationDocumentReplaceFromSnapshotSole(document, L"restore", snapshot),
            "replace history snapshot");

        ScreenshotAnnotation restored;
        Expect(ScreenshotAnnotationDocumentTryLegacyById(document, L"restore", restored),
            "resolve restored item");
        Expect(restored.text == L"restored", "restored snapshot content");
    }

    // EditSession draft overlays only the active item in an ephemeral view.
    {
        AnnotationDocument document;
        ScreenshotEditorState state;
        ScreenshotAnnotation first = MakeAnnotation(L"first", ScreenshotToolbarCommand::ToolGeometry);
        ScreenshotAnnotation second = MakeAnnotation(L"second", ScreenshotToolbarCommand::ToolText);
        second.text = L"committed";
        ScreenshotAnnotationDocumentCreate(document, first, state);
        ScreenshotAnnotationDocumentCreate(document, second, state);

        ScreenshotAnnotation draft = second;
        draft.text = L"draft";
        const auto ordered = ScreenshotAnnotationDocumentProjectOrdered(document, L"second", &draft);
        Expect(ordered.size() == 2, "draft projection count");
        Expect(ordered[0].id == L"first", "draft projection preserves order");
        Expect(ordered[1].text == L"draft", "draft overlays matching item");

        ScreenshotAnnotation live;
        Expect(ScreenshotAnnotationDocumentResolveLiveAnn(document, L"second", &draft, live),
            "resolve live draft");
        Expect(live.text == L"draft", "resolved live draft content");
    }

    if (g_fail) {
        std::cerr << g_fail << " failures\n";
        return 1;
    }
    std::cout << "all passed\n";
    return 0;
}
