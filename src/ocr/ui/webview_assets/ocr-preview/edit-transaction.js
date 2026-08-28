(function () {
  "use strict";

  function createTransaction(options) {
    if (!options || typeof options.postMessage !== "function" ||
        typeof options.sourceContext !== "function" || typeof options.onClosed !== "function") {
      throw new Error("Preview edit transaction dependencies are unavailable.");
    }

    var active = null;
    var pendingSave = null;
    var pendingRestore = null;

    function statusNode() {
      if (!active || !active.host) return null;
      var status = active.host.querySelector(".ocr-preview-editor-status");
      if (!status) {
        status = document.createElement("div");
        status.className = "ocr-preview-editor-status";
        status.setAttribute("role", "status");
        status.setAttribute("aria-live", "polite");
        active.host.appendChild(status);
      }
      return status;
    }

    function setStatus(message, isError) {
      var status = statusNode();
      if (!status) return;
      status.textContent = message || "";
      status.classList.toggle("is-error", !!isError);
    }

    function refreshControl() {
      if (active && active.saveControl && active.saveControl.refresh) active.saveControl.refresh();
    }

    function close(restoreOriginal) {
      if (!active) return;
      if (active.cleanup) {
        try { active.cleanup(); } catch (_) { /* best-effort editor cleanup */ }
      }
      (active.originals || []).forEach(function (original, index) {
        if (original) original.style.display = active.originalDisplays[index] || "";
      });
      if (active.host && active.host.parentNode) active.host.parentNode.removeChild(active.host);
      active = null;
      pendingSave = null;
      pendingRestore = null;
      options.onClosed(restoreOriginal);
    }

    function open(handle) {
      close(false);
      active = {
        id: handle.id,
        block: handle.block,
        host: handle.host,
        originals: handle.originals || [],
        originalDisplays: handle.originalDisplays || [],
        cleanup: handle.cleanup || null,
        saveControl: handle.saveControl || null,
        saveHandler: handle.saveHandler || null
      };
      active.originals.forEach(function (original) { if (original) original.style.display = "none"; });
    }

    function setEditorHandle(handle) {
      if (!active || active.id !== handle.id) return;
      active.cleanup = handle.cleanup || null;
      active.saveControl = handle.saveControl || null;
      active.saveHandler = handle.saveHandler || null;
      refreshControl();
    }

    function hasPending() { return !!pendingSave || !!pendingRestore; }
    function activeId() { return active ? active.id : ""; }
    function activeHost() { return active ? active.host : null; }
    function triggerSave() { if (active && active.saveHandler) active.saveHandler(); }

    function validSource(block) {
      if (!block || typeof block.visibleSourceContent !== "string" || !block.visibleSourceContent) return null;
      var start = Number(block.visibleSourceStart);
      var end = Number(block.visibleSourceEnd);
      var context = options.sourceContext();
      if (!Number.isFinite(start) || !Number.isFinite(end) || start < 0 || end <= start ||
          Math.floor(start) !== start || Math.floor(end) !== end || !context.revisionSha256) return null;
      return { start: start, end: end, context: context };
    }

    function requestSave(id, block, content) {
      content = String(content == null ? "" : content);
      if (hasPending()) return false;
      var source = validSource(block);
      if (!source) {
        setStatus("This block is no longer aligned with the source. Reopen the editor and try again.", true);
        return false;
      }
      pendingSave = { id: id, token: source.context.renderToken, content: content, block: block };
      refreshControl();
      options.postMessage({
        type: "previewBlockSave", id: id, renderToken: source.context.renderToken, content: content,
        canonicalSource: source.context.canonicalSource, offsetUnit: source.context.offsetUnit,
        sourceStart: source.start, sourceEnd: source.end, revisionSha256: source.context.revisionSha256,
        expectedSource: block.visibleSourceContent
      });
      return true;
    }

    function requestRestore(id, block) {
      if (hasPending() || !block || !block.canRestoreOriginal) return false;
      var source = validSource(block);
      if (!source) return false;
      pendingRestore = { id: id, token: source.context.renderToken, block: block };
      refreshControl();
      options.postMessage({
        type: "previewBlockRestore", id: id, renderToken: source.context.renderToken,
        canonicalSource: source.context.canonicalSource, offsetUnit: source.context.offsetUnit,
        sourceStart: source.start, sourceEnd: source.end, revisionSha256: source.context.revisionSha256,
        expectedSource: block.visibleSourceContent
      });
      return true;
    }

    function takeSaveResult(data) {
      if (!pendingSave || String(data.renderToken || "") !== pendingSave.token || String(data.id || "") !== pendingSave.id) return null;
      var result = pendingSave;
      pendingSave = null;
      refreshControl();
      return result;
    }

    function takeRestoreResult(data) {
      if (!pendingRestore || String(data.renderToken || "") !== pendingRestore.token || String(data.id || "") !== pendingRestore.id) return null;
      var result = pendingRestore;
      pendingRestore = null;
      refreshControl();
      return result;
    }

    return {
      open: open, close: close, setEditorHandle: setEditorHandle,
      activeId: activeId, activeHost: activeHost, hasPending: hasPending, triggerSave: triggerSave,
      setStatus: setStatus, refreshControl: refreshControl,
      requestSave: requestSave, requestRestore: requestRestore,
      takeSaveResult: takeSaveResult, takeRestoreResult: takeRestoreResult
    };
  }

  window.ZenCropPreviewEditTransaction = { createTransaction: createTransaction };
}());
