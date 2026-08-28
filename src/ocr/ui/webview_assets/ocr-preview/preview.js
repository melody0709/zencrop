(function () {
  "use strict";

  var preview = document.getElementById("preview");
  var currentRecordId = -1;
  var currentSourceMarkdown = "";
  var currentCanonicalSource = "markdown-body-lf";
  var currentOffsetUnit = "utf16-code-unit";
  var currentRevisionSha256 = "";
  var currentBlocks = [];
  var hoveredBlockId = "";
  var selectedBlockId = "";
  var floatingToolbar = null;
  var toolbarBlockId = "";
  var toolbarHovering = false;
  var toolbarHideTimer = 0;
  var currentRenderToken = "";
  var metricsFrame = 0;
  var lastMetricsKey = "";
  var overlayScrollbar = document.getElementById("preview-scrollbar");
  var overlayScrollbarThumb = document.getElementById("preview-scrollbar-thumb");
  var overlayScrollbarHideTimer = 0;
  var overlayScrollbarBoundaryHoverTimer = 0;
  var overlayScrollbarPreviewBoundaryHovered = false;
  var overlayScrollbarNativeBoundaryHovered = false;
  var overlayScrollbarPointerHovered = false;
  var overlayScrollbarDragPointerId = -1;
  var overlayScrollbarDragOffset = 0;

  var kOverlayScrollbarBoundaryWidth = 8;
  var kOverlayScrollbarBoundaryRevealDelay = 180;
  var kOverlayScrollbarHideDelay = 700;

  var renderGeneration = 0;

  function postMessage(message) {
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(message);
    }
  }

  function clearOverlayScrollbarHideTimer() {
    if (!overlayScrollbarHideTimer) return;
    window.clearTimeout(overlayScrollbarHideTimer);
    overlayScrollbarHideTimer = 0;
  }

  function clearOverlayScrollbarBoundaryHoverTimer() {
    if (!overlayScrollbarBoundaryHoverTimer) return;
    window.clearTimeout(overlayScrollbarBoundaryHoverTimer);
    overlayScrollbarBoundaryHoverTimer = 0;
  }

  function isOverlayScrollbarBoundaryHovered() {
    return overlayScrollbarPreviewBoundaryHovered ||
      overlayScrollbarNativeBoundaryHovered;
  }

  function isOverlayScrollbarInteractionActive() {
    return overlayScrollbarDragPointerId !== -1 ||
      overlayScrollbarPointerHovered ||
      isOverlayScrollbarBoundaryHovered();
  }

  function syncOverlayScrollbar() {
    if (!preview || !overlayScrollbar || !overlayScrollbarThumb) return;

    var viewportHeight = preview.clientHeight;
    var contentHeight = preview.scrollHeight;
    var maxScrollTop = Math.max(0, contentHeight - viewportHeight);
    if (viewportHeight <= 0 || maxScrollTop <= 0) {
      clearOverlayScrollbarHideTimer();
      clearOverlayScrollbarBoundaryHoverTimer();
      overlayScrollbarPointerHovered = false;
      cancelOverlayScrollbarDrag();
      overlayScrollbar.classList.remove("is-scrollable");
      overlayScrollbar.classList.remove("is-visible");
      overlayScrollbarThumb.style.height = "0px";
      overlayScrollbarThumb.style.transform = "translateY(0px)";
      return;
    }

    var trackHeight = overlayScrollbar.clientHeight;
    if (trackHeight <= 0) return;
    var thumbHeight = Math.max(24, Math.round(trackHeight * viewportHeight / contentHeight));
    thumbHeight = Math.min(trackHeight, thumbHeight);
    var maxThumbTop = Math.max(0, trackHeight - thumbHeight);
    var thumbTop = maxThumbTop > 0
      ? Math.round(Math.min(maxScrollTop, Math.max(0, preview.scrollTop)) / maxScrollTop * maxThumbTop)
      : 0;

    overlayScrollbar.classList.add("is-scrollable");
    overlayScrollbarThumb.style.height = thumbHeight + "px";
    overlayScrollbarThumb.style.transform = "translateY(" + thumbTop + "px)";
  }

  function revealOverlayScrollbar() {
    syncOverlayScrollbar();
    if (!overlayScrollbar || !overlayScrollbar.classList.contains("is-scrollable")) return;
    clearOverlayScrollbarBoundaryHoverTimer();
    clearOverlayScrollbarHideTimer();
    overlayScrollbar.classList.add("is-visible");
  }

  function deferOverlayScrollbarHide(delay) {
    if (!overlayScrollbar || overlayScrollbarDragPointerId !== -1 ||
        !overlayScrollbar.classList.contains("is-scrollable") ||
        isOverlayScrollbarBoundaryHovered() || overlayScrollbarPointerHovered) return;
    clearOverlayScrollbarHideTimer();
    overlayScrollbarHideTimer = window.setTimeout(function () {
      overlayScrollbarHideTimer = 0;
      if (!isOverlayScrollbarInteractionActive()) {
        overlayScrollbar.classList.remove("is-visible");
      }
    }, Math.max(0, delay || 1000));
  }

  function scheduleOverlayScrollbarReveal() {
    if (!overlayScrollbar || overlayScrollbarDragPointerId !== -1) return;
    syncOverlayScrollbar();
    if (!overlayScrollbar.classList.contains("is-scrollable")) return;
    clearOverlayScrollbarHideTimer();
    if (overlayScrollbar.classList.contains("is-visible") ||
        overlayScrollbarBoundaryHoverTimer) return;
    overlayScrollbarBoundaryHoverTimer = window.setTimeout(function () {
      overlayScrollbarBoundaryHoverTimer = 0;
      if (isOverlayScrollbarBoundaryHovered() &&
          overlayScrollbarDragPointerId === -1) {
        revealOverlayScrollbar();
      }
    }, kOverlayScrollbarBoundaryRevealDelay);
  }

  function setOverlayScrollbarBoundaryHover(source, hovered) {
    if (source === "native") {
      overlayScrollbarNativeBoundaryHovered = !!hovered;
    } else {
      overlayScrollbarPreviewBoundaryHovered = !!hovered;
    }
    if (hovered) {
      scheduleOverlayScrollbarReveal();
      return;
    }
    if (!isOverlayScrollbarBoundaryHovered()) {
      clearOverlayScrollbarBoundaryHoverTimer();
      deferOverlayScrollbarHide(kOverlayScrollbarHideDelay);
    }
  }

  function setPreviewScrollFromOverlayPointer(clientY, thumbOffset) {
    if (!preview || !overlayScrollbar || !overlayScrollbarThumb) return;
    var trackRect = overlayScrollbar.getBoundingClientRect();
    var thumbRect = overlayScrollbarThumb.getBoundingClientRect();
    var maxScrollTop = Math.max(0, preview.scrollHeight - preview.clientHeight);
    var maxThumbTop = Math.max(0, trackRect.height - thumbRect.height);
    if (maxScrollTop <= 0 || maxThumbTop <= 0) return;
    var thumbTop = clientY - trackRect.top - thumbOffset;
    thumbTop = Math.max(0, Math.min(maxThumbTop, thumbTop));
    preview.scrollTop = Math.round(thumbTop / maxThumbTop * maxScrollTop);
  }

  function releaseOverlayScrollbarPointerCapture(pointerId) {
    if (pointerId === -1) return;
    try {
      if (overlayScrollbar && overlayScrollbar.releasePointerCapture &&
          overlayScrollbar.hasPointerCapture && overlayScrollbar.hasPointerCapture(pointerId)) {
        overlayScrollbar.releasePointerCapture(pointerId);
      }
    } catch (_) {
      // The pointer may already have been cancelled or captured elsewhere.
    }
  }

  function cancelOverlayScrollbarDrag() {
    var pointerId = overlayScrollbarDragPointerId;
    if (pointerId === -1) return;
    releaseOverlayScrollbarPointerCapture(pointerId);
    overlayScrollbarDragPointerId = -1;
    overlayScrollbarDragOffset = 0;
  }

  function beginOverlayScrollbarDrag(event) {
    if (!overlayScrollbar || !overlayScrollbarThumb || event.button !== 0 ||
        !overlayScrollbar.classList.contains("is-scrollable")) return;
    overlayScrollbarPointerHovered = true;
    clearOverlayScrollbarBoundaryHoverTimer();
    clearOverlayScrollbarHideTimer();
    var thumbRect = overlayScrollbarThumb.getBoundingClientRect();
    var pressedThumb = event.target === overlayScrollbarThumb;
    overlayScrollbarDragPointerId = event.pointerId;
    overlayScrollbarDragOffset = pressedThumb
      ? event.clientY - thumbRect.top
      : thumbRect.height / 2;
    try {
      if (overlayScrollbar.setPointerCapture) {
        overlayScrollbar.setPointerCapture(event.pointerId);
      }
    } catch (_) {
      // Pointer capture is best-effort; the active pointer still receives
      // normal events in WebView2 when capture is unavailable.
    }
    event.preventDefault();
    event.stopPropagation();
    revealOverlayScrollbar();
    setPreviewScrollFromOverlayPointer(event.clientY, overlayScrollbarDragOffset);
  }

  function moveOverlayScrollbarDrag(event) {
    if (event.pointerId !== overlayScrollbarDragPointerId) return;
    event.preventDefault();
    setPreviewScrollFromOverlayPointer(event.clientY, overlayScrollbarDragOffset);
    revealOverlayScrollbar();
  }

  function endOverlayScrollbarDrag(event) {
    if (event.pointerId !== overlayScrollbarDragPointerId) return;
    releaseOverlayScrollbarPointerCapture(event.pointerId);
    overlayScrollbarDragPointerId = -1;
    overlayScrollbarDragOffset = 0;
    deferOverlayScrollbarHide(kOverlayScrollbarHideDelay);
  }

  function resetOverlayScrollbarForRender() {
    clearOverlayScrollbarHideTimer();
    clearOverlayScrollbarBoundaryHoverTimer();
    overlayScrollbarPointerHovered = false;
    cancelOverlayScrollbarDrag();
    if (overlayScrollbar) overlayScrollbar.classList.remove("is-visible");
  }

  function scheduleContentMetrics() {
    if (metricsFrame) return;
    var requestFrame = window.requestAnimationFrame || function (callback) {
      return window.setTimeout(callback, 16);
    };
    metricsFrame = requestFrame(function () {
      metricsFrame = 0;
      if (!preview || !preview.isConnected) return;
      var pixelRatio = Number(window.devicePixelRatio) || 1;
      // A scroll container reports at least its current client height. Measure
      // with a zero-height viewport so short documents report their intrinsic
      // Markdown height instead of feeding the current window size back into
      // the native auto-layout loop.
      var previousInlineHeight = preview.style.height;
      preview.style.height = "0px";
      var intrinsicScrollHeight = preview.scrollHeight;
      preview.style.height = previousInlineHeight;
      syncOverlayScrollbar();
      if (isOverlayScrollbarBoundaryHovered()) {
        scheduleOverlayScrollbarReveal();
      }
      if (!currentRenderToken) return;
      var scrollHeight = Math.ceil(intrinsicScrollHeight * pixelRatio);
      var scrollWidth = Math.ceil(preview.scrollWidth * pixelRatio);
      var clientWidth = Math.ceil(preview.clientWidth * pixelRatio);
      if (scrollHeight <= 0 || scrollWidth <= 0 || clientWidth <= 0) return;
      var key = currentRenderToken + ":" + scrollHeight + ":" +
        scrollWidth + ":" + clientWidth;
      if (key === lastMetricsKey) return;
      lastMetricsKey = key;
      postMessage({
        type: "previewContentMetrics",
        recordId: currentRecordId,
        renderToken: currentRenderToken,
        scrollHeight: scrollHeight,
        scrollWidth: scrollWidth,
        clientWidth: clientWidth
      });
    });
  }

  if (window.MutationObserver) {
    new MutationObserver(scheduleContentMetrics).observe(preview, {
      childList: true,
      subtree: true,
      characterData: true
    });
  }
  if (window.ResizeObserver) {
    new ResizeObserver(scheduleContentMetrics).observe(preview);
  }

  function setEmpty(text) {
    preview.className = "markdown-body empty";
    preview.innerHTML = "";
    var div = document.createElement("div");
    div.className = "empty-state";
    div.textContent = text || "No OCR record selected.";
    preview.appendChild(div);
    scheduleContentMetrics();
  }

  function setError(text) {
    preview.className = "markdown-body";
    preview.innerHTML = "";
    var div = document.createElement("div");
    div.className = "error-state";
    div.textContent = text || "Failed to render Markdown.";
    preview.appendChild(div);
    scheduleContentMetrics();
  }

  var security = window.ZenCropPreviewSecurity;
  if (!security) {
    throw new Error("Preview security asset is unavailable.");
  }
  var markdownAsset = window.ZenCropPreviewMarkdown;
  if (!markdownAsset || typeof markdownAsset.createRenderer !== "function") {
    throw new Error("Preview Markdown asset is unavailable.");
  }
  var markdownRenderer = markdownAsset.createRenderer({
    security: security,
    markdownIt: window.markdownit,
    katex: window.katex,
    mermaid: window.mermaid,
    Chart: window.Chart,
    isCurrentGeneration: function (generation) { return generation === renderGeneration; }
  });
  var blocksAsset = window.ZenCropPreviewBlocks;
  if (!blocksAsset || typeof blocksAsset.createBlockMapper !== "function") {
    throw new Error("Preview block mapping asset is unavailable.");
  }
  var blockMapper = blocksAsset.createBlockMapper({
    root: preview,
    markdown: markdownRenderer
  });
  var editorMarkdown = window.ZenCropPreviewEditorMarkdown;
  if (!editorMarkdown || typeof editorMarkdown.serialize !== "function" ||
      typeof editorMarkdown.destination !== "function") {
    throw new Error("Preview editor Markdown asset is unavailable.");
  }
  var transactionAsset = window.ZenCropPreviewEditTransaction;
  if (!transactionAsset || typeof transactionAsset.createTransaction !== "function") {
    throw new Error("Preview edit transaction asset is unavailable.");
  }
  var editTransaction = transactionAsset.createTransaction({
    postMessage: postMessage,
    sourceContext: function () {
      return {
        renderToken: currentRenderToken,
        canonicalSource: currentCanonicalSource,
        offsetUnit: currentOffsetUnit,
        revisionSha256: currentRevisionSha256
      };
    },
    onClosed: function () { applyBlockState(); }
  });
  var formulaAsset = window.ZenCropPreviewFormulaEditor;
  if (!formulaAsset || typeof formulaAsset.createFormulaEditor !== "function") {
    throw new Error("Preview formula editor asset is unavailable.");
  }
  var buildFormulaEditor = formulaAsset.createFormulaEditor({
    katex: window.katex,
    createStatus: editorStatus,
    setStatus: setEditorStatus,
    addActions: function (toolbar, block, readContent, canSave, validate) {
      return appendEditorActions(toolbar, editTransaction.activeId(), block, readContent, canSave, validate);
    }
  });

  preview.addEventListener("click", function (event) {
    var node = event.target;
    while (node && node !== preview && node.nodeName !== "A") node = node.parentNode;
    if (!node || node === preview) return;
    event.preventDefault();
    var href = node.getAttribute("href") || "";
    var url = security.resolveUrl(href);
    if (url && security.isSafeLinkHref(href)) {
      postMessage({ type: "openExternal", url: url.href });
    }
  });

  preview.addEventListener("dblclick", function (event) {
    if (isActionOrEditorTarget(event.target)) return;
    if (event.target && event.target.closest &&
        event.target.closest(".ocr-preview-linked-block")) return;
    event.preventDefault();
    postMessage({ type: "previewDocumentEdit" });
  });

  function acceleratorVirtualKey(event) {
    if (!event.ctrlKey || event.altKey || event.metaKey) return 0;
    switch (event.key) {
      case "ArrowUp": return 0x26;
      case "ArrowDown": return 0x28;
      case "Home": return 0x24;
      case "End": return 0x23;
      case "f":
      case "F": return 0x46;
      case "0": return 0x30;
      default: return 0;
    }
  }

  window.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && !editTransaction.activeId() && (selectedBlockId || hoveredBlockId)) {
      event.preventDefault();
      event.stopPropagation();
      clearToolbarHideTimer();
      toolbarHovering = false;
      setPreviewHover("");
      setPreviewSelection("", false);
      hideFloatingToolbar();
      postMessage({ type: "previewBlockHover", id: "" });
      postMessage({ type: "previewBlockSelect", id: "" });
      return;
    }
    var virtualKey = acceleratorVirtualKey(event);
    if (!virtualKey) return;
    event.preventDefault();
    postMessage({ type: "acceleratorKey", virtualKey: virtualKey, ctrlKey: true });
  });

  function findBlockById(id) {
    for (var i = 0; i < currentBlocks.length; i++) {
      if (currentBlocks[i].id === id) return currentBlocks[i];
    }
    return null;
  }

  function blockElementHasId(element, id) {
    if (!element || !id) return false;
    if (!element._ocrPreviewBlockIds) {
      var ids = [(element.getAttribute("data-block-id") || "")].concat(
        (element.getAttribute("data-linked-block-ids") || "").split("\n"));
      element._ocrPreviewBlockIds = new Set(ids.filter(function (value) { return !!value; }));
    }
    return element._ocrPreviewBlockIds.has(id);
  }

  function findBlockElements(id) {
    if (!id) return [];
    var nodes = preview.querySelectorAll(".ocr-preview-linked-block[data-block-id]");
    var matches = [];
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].classList.contains("ocr-preview-inline-editor")) continue;
      if (blockElementHasId(nodes[i], id)) matches.push(nodes[i]);
    }
    return matches;
  }

  function findBlockElement(id) {
    var nodes = findBlockElements(id);
    return nodes.length ? nodes[0] : null;
  }

  function isActionOrEditorTarget(target) {
    return !!(target && target.closest && (
      target.closest(".ocr-preview-editor") ||
      target.closest("a")
    ));
  }

  function applyBlockState(options) {
    options = options || {};
    var nodes = Array.prototype.slice.call(preview.querySelectorAll(".ocr-preview-linked-block[data-block-id]"));
    nodes.forEach(function (node) {
      var hovered = blockElementHasId(node, hoveredBlockId);
      var selected = blockElementHasId(node, selectedBlockId);
      var editing = blockElementHasId(node, editTransaction.activeId());
      node.classList.toggle("is-hovered", hovered);
      node.classList.toggle("is-selected", selected);
      node.classList.toggle("is-editing", editing);
    });

    if (options.ensureVisible && selectedBlockId) {
      var selectedNode = findBlockElement(selectedBlockId);
      if (selectedNode && typeof selectedNode.scrollIntoView === "function") {
        selectedNode.scrollIntoView({ block: "nearest", inline: "nearest" });
      }
    }
    syncFloatingToolbar();
  }

  function setPreviewHover(id) {
    hoveredBlockId = String(id || "");
    applyBlockState();
  }

  function setPreviewSelection(id, ensureVisible) {
    selectedBlockId = String(id || "");
    applyBlockState({ ensureVisible: !!ensureVisible });
  }

  function clearToolbarHideTimer() {
    if (toolbarHideTimer) {
      window.clearTimeout(toolbarHideTimer);
      toolbarHideTimer = 0;
    }
  }

  function clipboardWriteText(text) {
    text = String(text || "");
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).catch(function () {});
      return;
    }
    var textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.setAttribute("readonly", "readonly");
    textarea.style.position = "fixed";
    textarea.style.left = "-9999px";
    document.body.appendChild(textarea);
    textarea.select();
    try {
      document.execCommand("copy");
    } catch (_) {
      // Clipboard may be unavailable in locked-down WebView profiles.
    }
    document.body.removeChild(textarea);
  }

  function blockSupportsInlineEditing(block) {
    if (!block || block.editable === false) return false;
    var kind = blockMapper.kindOf(block);
    return kind === "text" || kind === "heading" || kind === "formula" || kind === "table" || kind === "image";
  }

  function ensureFloatingToolbar() {
    if (floatingToolbar) return floatingToolbar;
    var toolbar = document.createElement("div");
    toolbar.className = "ocr-preview-floating-toolbar";
    toolbar.setAttribute("role", "toolbar");
    toolbar.setAttribute("aria-label", "Preview block tools");

    var copyButton = document.createElement("button");
    copyButton.type = "button";
    copyButton.className = "ocr-preview-tool-button";
    copyButton.textContent = "Copy";
    copyButton.title = "Copy block content";
    copyButton.addEventListener("click", function (event) {
      event.preventDefault();
      event.stopPropagation();
      var block = findBlockById(toolbarBlockId);
      var element = findBlockElement(toolbarBlockId);
      clipboardWriteText(block && block.content ? block.content : (element ? element.innerText : ""));
    });

    var editButton = document.createElement("button");
    editButton.type = "button";
    editButton.className = "ocr-preview-tool-button is-primary";
    editButton.textContent = "Edit";
    editButton.title = "Edit block";
    editButton.addEventListener("click", function (event) {
      event.preventDefault();
      event.stopPropagation();
      requestPreviewEdit(toolbarBlockId);
    });

    toolbar.appendChild(copyButton);
    toolbar.appendChild(editButton);
    toolbar.addEventListener("mouseenter", function () {
      toolbarHovering = true;
      clearToolbarHideTimer();
      if (toolbarBlockId) {
        setPreviewHover(toolbarBlockId);
        postMessage({ type: "previewBlockHover", id: toolbarBlockId });
      }
    });
    toolbar.addEventListener("mouseleave", function () {
      toolbarHovering = false;
      if (!selectedBlockId || selectedBlockId !== toolbarBlockId) {
        setPreviewHover("");
        postMessage({ type: "previewBlockHover", id: "" });
      }
      syncFloatingToolbar();
    });
    preview.appendChild(toolbar);
    floatingToolbar = toolbar;
    return toolbar;
  }

  function positionFloatingToolbar(toolbar, element) {
    if (!toolbar || !element) return;
    toolbar.style.display = "flex";
    var previewRect = preview.getBoundingClientRect();
    var elementRect = element.getBoundingClientRect();
    var width = toolbar.offsetWidth || 96;
    var height = toolbar.offsetHeight || 32;
    var left = elementRect.right - previewRect.left + preview.scrollLeft - width;
    var top = elementRect.top - previewRect.top + preview.scrollTop - height - 6;
    var minLeft = preview.scrollLeft + 6;
    var maxLeft = preview.scrollLeft + preview.clientWidth - width - 6;
    var minTop = preview.scrollTop + 6;
    var maxTop = preview.scrollTop + preview.clientHeight - height - 6;
    if (top < minTop) top = elementRect.top - previewRect.top + preview.scrollTop + 6;
    if (maxLeft >= minLeft) left = Math.max(minLeft, Math.min(left, maxLeft));
    if (maxTop >= minTop) top = Math.max(minTop, Math.min(top, maxTop));
    toolbar.style.left = Math.max(0, left) + "px";
    toolbar.style.top = Math.max(0, top) + "px";
  }

  function hideFloatingToolbar() {
    if (!floatingToolbar) return;
    floatingToolbar.style.display = "none";
    toolbarBlockId = "";
  }

  function syncFloatingToolbar() {
    if (editTransaction.activeId()) {
      hideFloatingToolbar();
      return;
    }
    var requestedId = hoveredBlockId || selectedBlockId;
    var element = findBlockElement(requestedId);
    var primaryId = element ? (element.getAttribute("data-block-id") || requestedId) : requestedId;
    var block = findBlockById(primaryId);
    if (!requestedId || !element || !block) {
      if (!toolbarHovering) hideFloatingToolbar();
      return;
    }
    var toolbar = ensureFloatingToolbar();
    toolbarBlockId = primaryId;
    var editButton = toolbar.querySelector(".ocr-preview-tool-button.is-primary");
    if (editButton) editButton.style.display = blockSupportsInlineEditing(block) ? "" : "none";
    positionFloatingToolbar(toolbar, element);
  }

  function scheduleHoverClear(blockId) {
    clearToolbarHideTimer();
    toolbarHideTimer = window.setTimeout(function () {
      toolbarHideTimer = 0;
      if (toolbarHovering) return;
      if (hoveredBlockId === blockId) {
        setPreviewHover("");
        postMessage({ type: "previewBlockHover", id: "" });
      }
      syncFloatingToolbar();
    }, 90);
  }

  function setEditorHeading(editorBody, level) {
    editorBody.focus();
    if (level > 0) {
      document.execCommand("formatBlock", false, "H" + level);
    } else {
      document.execCommand("formatBlock", false, "P");
    }
  }

  function editorButton(text, title, onClick) {
    var button = document.createElement("button");
    button.type = "button";
    button.className = "ocr-preview-editor-button";
    button.textContent = text;
    button.title = title || text;
    button.addEventListener("mousedown", function (event) {
      event.preventDefault();
    });
    button.addEventListener("click", onClick);
    return button;
  }

  function finishInlineEditor(restoreOriginal) {
    editTransaction.close(restoreOriginal);
  }

  function editorStatus() {
    var status = document.createElement("div");
    status.className = "ocr-preview-editor-status";
    status.setAttribute("role", "status");
    status.setAttribute("aria-live", "polite");
    return status;
  }

  function setEditorStatus(status, message, isError) {
    if (!status) return;
    status.textContent = message || "";
    status.classList.toggle("is-error", !!isError);
  }

  function commitPreviewBlockSave(id, block, content) {
    return editTransaction.requestSave(id, block, content);
  }

  function commitPreviewBlockRestore(id, block) {
    return editTransaction.requestRestore(id, block);
  }

  function appendEditorActions(toolbar, id, block, readContent, canSave, validateBeforeSave) {
    var restore = null;
    if (block.edited) {
      restore = editorButton("Restore OCR",
        block.canRestoreOriginal
          ? "Restore the original OCR result"
          : "Original OCR was not retained for this older edit; run OCR again",
        function () {
        if (!restore.disabled) commitPreviewBlockRestore(id, block);
      });
      restore.classList.add("is-restore");
      toolbar.appendChild(restore);
    }
    var spacer = document.createElement("span");
    spacer.className = "ocr-preview-editor-spacer";
    toolbar.appendChild(spacer);

    var cancel = editorButton("Cancel", "Cancel (Esc)", function () {
      postMessage({ type: "previewBlockCancel", id: id, renderToken: currentRenderToken });
      finishInlineEditor(true);
    });
    toolbar.appendChild(cancel);

    var save = editorButton("Save", "Save (Ctrl+S)", function () {
      if (save.disabled) return;
      if (validateBeforeSave && !validateBeforeSave()) {
        refresh();
        return;
      }
      commitPreviewBlockSave(id, block, readContent());
    });
    save.classList.add("is-primary");
    toolbar.appendChild(save);

    function refresh() {
      var pending = editTransaction.hasPending();
      save.disabled = pending || (canSave ? !canSave() : false);
      if (restore) restore.disabled = pending || !block.canRestoreOriginal;
    }
    refresh();
    var control = {
      save: function () {
        refresh();
        if (save.disabled) return;
        if (validateBeforeSave && !validateBeforeSave()) {
          refresh();
          return;
        }
        commitPreviewBlockSave(id, block, readContent());
      },
      refresh: refresh,
      button: save
    };
    return control;
  }

  function selectionInside(root) {
    var selection = window.getSelection ? window.getSelection() : null;
    if (!selection || !selection.rangeCount) return null;
    var range = selection.getRangeAt(0);
    var container = range.commonAncestorContainer;
    if (container.nodeType === Node.TEXT_NODE) container = container.parentNode;
    return container && root.contains(container) ? range.cloneRange() : null;
  }

  function restoreSelectionInside(root, range) {
    if (!range || !window.getSelection) {
      root.focus();
      return;
    }
    var selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
  }

  function buildTextEditor(toolbar, bodyHost, block, element) {
    var originalSource = block.visibleSourceContent || block.content || element.innerText || "";
    var dirty = false;
    var heading = document.createElement("select");
    heading.className = "ocr-preview-editor-select";
    heading.title = "Heading";
    heading.setAttribute("aria-label", "Heading level");
    [["0", "Tt"], ["1", "H1"], ["2", "H2"], ["3", "H3"], ["4", "H4"], ["5", "H5"], ["6", "H6"]].forEach(function (item) {
      var option = document.createElement("option");
      option.value = item[0];
      option.textContent = item[1];
      heading.appendChild(option);
    });

    var body = document.createElement("div");
    body.className = "ocr-preview-rich-editor-body";
    body.setAttribute("contenteditable", "true");
    body.setAttribute("role", "textbox");
    body.setAttribute("aria-multiline", "true");
    markdownRenderer.renderInto(
      body,
      originalSource,
      renderGeneration);
    if (!body.innerHTML.trim()) body.innerHTML = "<p><br></p>";
    var firstTag = body.firstElementChild
      ? body.firstElementChild.nodeName.toLowerCase()
      : "";
    if (/^h[1-6]$/.test(firstTag)) heading.value = firstTag.slice(1);

    var savedRange = null;
    function rememberSelection() {
      savedRange = selectionInside(body) || savedRange;
    }
    function applyCommand(command) {
      restoreSelectionInside(body, savedRange);
      document.execCommand(command, false, null);
      dirty = true;
      rememberSelection();
    }

    body.addEventListener("mouseup", rememberSelection);
    body.addEventListener("keyup", rememberSelection);
    body.addEventListener("input", function () {
      dirty = true;
      rememberSelection();
    });
    body.addEventListener("keydown", function (event) {
      if (event.ctrlKey && event.shiftKey && event.key.toLowerCase() === "x") {
        event.preventDefault();
        applyCommand("strikeThrough");
      }
    });
    heading.addEventListener("mousedown", rememberSelection);
    heading.addEventListener("change", function () {
      restoreSelectionInside(body, savedRange);
      setEditorHeading(body, Number(heading.value || "0"));
      dirty = true;
      rememberSelection();
    });

    toolbar.appendChild(heading);
    toolbar.appendChild(editorButton("B", "Bold (Ctrl+B)", function () { applyCommand("bold"); }));
    toolbar.appendChild(editorButton("I", "Italic (Ctrl+I)", function () { applyCommand("italic"); }));
    toolbar.appendChild(editorButton("S", "Strikethrough (Ctrl+Shift+X)", function () { applyCommand("strikeThrough"); }));
    bodyHost.appendChild(body);

    return {
      focus: function () {
        body.focus();
        rememberSelection();
      },
      readContent: function () {
        return dirty ? editorMarkdown.serialize(body) : originalSource;
      },
      canSave: function () { return true; }
    };
  }

  function splitMarkdownTableRow(line) {
    var text = String(line || "").trim();
    if (text.charAt(0) === "|") text = text.slice(1);
    if (text.charAt(text.length - 1) === "|") text = text.slice(0, -1);
    var cells = [];
    var cell = "";
    var escaped = false;
    for (var i = 0; i < text.length; i++) {
      var ch = text.charAt(i);
      if (escaped) {
        cell += ch;
        escaped = false;
      } else if (ch === "\\") {
        cell += ch;
        escaped = true;
      } else if (ch === "|") {
        cells.push(cell.trim());
        cell = "";
      } else {
        cell += ch;
      }
    }
    cells.push(cell.trim());
    return cells;
  }

  function isMarkdownTableSeparator(line) {
    var cells = splitMarkdownTableRow(line);
    return cells.length > 0 && cells.every(function (cell) {
      return /^:?-+:?$/.test(cell.replace(/\s+/g, ""));
    });
  }

  function markdownTableAlignments(line) {
    return splitMarkdownTableRow(line).map(function (cell) {
      var compact = cell.replace(/\s+/g, "");
      if (compact.charAt(0) === ":" && compact.charAt(compact.length - 1) === ":") return "center";
      if (compact.charAt(compact.length - 1) === ":") return "right";
      if (compact.charAt(0) === ":") return "left";
      return "none";
    });
  }

  function findMarkdownTableSegment(content) {
    var normalized = String(content || "").replace(/\r\n?/g, "\n");
    var lines = normalized.split("\n");
    for (var i = 0; i + 1 < lines.length; i++) {
      if (lines[i].indexOf("|") === -1 || !isMarkdownTableSeparator(lines[i + 1])) continue;
      var end = i + 2;
      while (end < lines.length && lines[end].trim() && lines[end].indexOf("|") !== -1) end++;
      var rows = [splitMarkdownTableRow(lines[i])];
      for (var rowIndex = i + 2; rowIndex < end; rowIndex++) {
        rows.push(splitMarkdownTableRow(lines[rowIndex]));
      }
      return {
        format: "markdown",
        prefix: lines.slice(0, i).join("\n"),
        suffix: lines.slice(end).join("\n"),
        rows: rows,
        alignments: markdownTableAlignments(lines[i + 1])
      };
    }
    return null;
  }

  function findHtmlTableSegment(content) {
    var text = String(content || "");
    var match = /<table\b[\s\S]*?<\/table\s*>/i.exec(text);
    if (!match) return null;
    var container = document.createElement("div");
    container.innerHTML = security.sanitizeHtml(match[0]);
    var table = container.querySelector("table");
    if (!table) return null;
    return {
      format: "html",
      prefix: text.slice(0, match.index),
      suffix: text.slice(match.index + match[0].length),
      table: table
    };
  }

  function buildMarkdownGridTable(rows) {
    var table = document.createElement("table");
    var thead = document.createElement("thead");
    var tbody = document.createElement("tbody");
    table.appendChild(thead);
    table.appendChild(tbody);
    var columnCount = (rows || []).reduce(function (maximum, row) {
      return Math.max(maximum, (row || []).length);
    }, 1);
    (rows || []).forEach(function (row, rowIndex) {
      var tr = document.createElement("tr");
      var parent = rowIndex === 0 ? thead : tbody;
      for (var columnIndex = 0; columnIndex < columnCount; columnIndex++) {
        var value = (row || [])[columnIndex] || "";
        var cell = document.createElement(rowIndex === 0 ? "th" : "td");
        markdownRenderer.renderInto(cell, value, renderGeneration);
        tr.appendChild(cell);
      }
      parent.appendChild(tr);
    });
    if (!table.rows.length) {
      var emptyRow = document.createElement("tr");
      var emptyCell = document.createElement("th");
      emptyCell.textContent = "";
      emptyRow.appendChild(emptyCell);
      thead.appendChild(emptyRow);
    }
    return table;
  }

  function parseTableEditorState(content) {
    var markdown = findMarkdownTableSegment(content);
    if (markdown) {
      markdown.table = buildMarkdownGridTable(markdown.rows);
      return markdown;
    }
    var html = findHtmlTableSegment(content);
    if (html) return html;
    var fallback = document.createElement("div");
    markdownRenderer.renderInto(fallback, content || "", renderGeneration);
    var table = fallback.querySelector("table");
    if (!table) return null;
    return { format: "html", prefix: "", suffix: "", table: table };
  }

  function tableHasSpans(table) {
    return Array.prototype.slice.call(table.querySelectorAll("th,td")).some(function (cell) {
      return Number(cell.rowSpan || 1) > 1 || Number(cell.colSpan || 1) > 1;
    });
  }

  function tableMatrix(table) {
    var matrix = [];
    var entries = [];
    Array.prototype.slice.call(table.rows || []).forEach(function (row, rowIndex) {
      if (!matrix[rowIndex]) matrix[rowIndex] = [];
      var columnIndex = 0;
      Array.prototype.slice.call(row.cells || []).forEach(function (cell) {
        while (matrix[rowIndex][columnIndex]) columnIndex++;
        var rowSpan = Math.max(1, Number(cell.rowSpan || 1));
        var colSpan = Math.max(1, Number(cell.colSpan || 1));
        var entry = { cell: cell, row: rowIndex, column: columnIndex, rowSpan: rowSpan, colSpan: colSpan };
        entries.push(entry);
        for (var r = rowIndex; r < rowIndex + rowSpan; r++) {
          if (!matrix[r]) matrix[r] = [];
          for (var c = columnIndex; c < columnIndex + colSpan; c++) matrix[r][c] = entry;
        }
        columnIndex += colSpan;
      });
    });
    return { matrix: matrix, entries: entries };
  }

  function stripEditorTableAttributes(table) {
    var clone = table.cloneNode(true);
    Array.prototype.slice.call(clone.querySelectorAll("th,td")).forEach(function (cell) {
      cell.removeAttribute("contenteditable");
      cell.removeAttribute("tabindex");
      cell.classList.remove("is-active", "is-range-selected");
      if (!cell.className) cell.removeAttribute("class");
    });
    return clone;
  }

  function serializeTableAsHtml(table) {
    return security.sanitizeHtml(stripEditorTableAttributes(table).outerHTML).trim();
  }

  function serializeTableAsMarkdown(table, alignments) {
    if (tableHasSpans(table)) return serializeTableAsHtml(table);
    var rows = Array.prototype.slice.call(table.rows || []);
    if (!rows.length) return "";
    var columns = rows.reduce(function (maximum, row) {
      return Math.max(maximum, row.cells.length);
    }, 1);
    var serialized = rows.map(function (row) {
      var cells = [];
      for (var column = 0; column < columns; column++) {
        var cell = row.cells[column];
        cells.push(cell
          ? serializeChildrenMarkdown(cell).replace(/\s+$/g, "").replace(/\|/g, "\\|").replace(/\n+/g, "<br>").trim()
          : "");
      }
      return "| " + cells.join(" | ") + " |";
    });
    var separators = [];
    for (var i = 0; i < columns; i++) {
      var alignment = alignments && alignments[i];
      separators.push(alignment === "left" ? ":---" :
        (alignment === "center" ? ":---:" : (alignment === "right" ? "---:" : "---")));
    }
    serialized.splice(1, 0, "| " + separators.join(" | ") + " |");
    return serialized.join("\n");
  }

  function joinEditorDocumentParts(prefix, core, suffix) {
    var out = "";
    if (prefix) out += prefix.replace(/\s+$/g, "");
    if (out && core) out += "\n\n";
    out += core;
    if (suffix) {
      if (out) out += "\n\n";
      out += suffix.replace(/^\s+/g, "");
    }
    return out.trim();
  }

  function buildTableEditor(toolbar, bodyHost, block, element) {
    var original = block.visibleSourceContent || block.content || element.innerText || "";
    var state = parseTableEditorState(original);
    var valid = !!state;
    var forceHtml = state && state.format === "html";
    var activeCell = null;
    var anchorCell = null;
    var selectedCells = [];
    var sourceMode = false;
    var previewTimer = 0;
    var saveControl = null;
    var dirty = false;

    var modeBar = document.createElement("div");
    modeBar.className = "ocr-preview-editor-tabs";
    var gridTab = editorButton("Grid", "Edit table cells", function () { switchMode(false); });
    var sourceTab = editorButton("Markdown", "Edit table source", function () { switchMode(true); });
    gridTab.classList.add("is-active");
    modeBar.appendChild(gridTab);
    modeBar.appendChild(sourceTab);

    var gridPanel = document.createElement("div");
    gridPanel.className = "ocr-preview-table-grid-panel";
    var gridTools = document.createElement("div");
    gridTools.className = "ocr-preview-table-grid-tools";
    var gridScroll = document.createElement("div");
    gridScroll.className = "ocr-preview-table-grid-scroll";
    var sourcePanel = document.createElement("div");
    sourcePanel.className = "ocr-preview-table-source-panel";
    sourcePanel.hidden = true;
    var source = document.createElement("textarea");
    source.className = "ocr-preview-editor-textarea ocr-preview-table-source";
    source.setAttribute("spellcheck", "false");
    source.setAttribute("aria-label", "Table Markdown or HTML source");
    var rendered = document.createElement("div");
    rendered.className = "ocr-preview-table-live-preview markdown-body";
    var status = editorStatus();
    sourcePanel.appendChild(source);
    sourcePanel.appendChild(rendered);

    function currentTable() {
      return gridScroll.querySelector("table");
    }

    function currentContent() {
      if (!dirty) return original;
      if (sourceMode) return source.value.trim();
      var table = currentTable();
      if (!table || !state) return "";
      var tableSource = (forceHtml || state.format === "html")
        ? serializeTableAsHtml(table)
        : serializeTableAsMarkdown(table, state.alignments);
      return joinEditorDocumentParts(state.prefix, tableSource, state.suffix);
    }

    function refreshCellState() {
      var table = currentTable();
      if (!table) return;
      Array.prototype.slice.call(table.querySelectorAll("th,td")).forEach(function (cell) {
        cell.classList.toggle("is-active", cell === activeCell);
        cell.classList.toggle("is-range-selected", selectedCells.indexOf(cell) !== -1);
      });
    }

    function setSelection(cells, active) {
      selectedCells = cells || [];
      activeCell = active || selectedCells[0] || null;
      if (activeCell && !anchorCell) anchorCell = activeCell;
      refreshCellState();
    }

    function prepareGridTable(table) {
      Array.prototype.slice.call(table.querySelectorAll("th,td")).forEach(function (cell) {
        cell.setAttribute("contenteditable", "true");
        cell.setAttribute("tabindex", "0");
      });
      gridScroll.innerHTML = "";
      gridScroll.appendChild(table);
      var first = table.querySelector("th,td");
      anchorCell = first;
      setSelection(first ? [first] : [], first);
    }

    function selectRange(target) {
      var table = currentTable();
      var layout = tableMatrix(table);
      var from = layout.entries.filter(function (entry) { return entry.cell === anchorCell; })[0];
      var to = layout.entries.filter(function (entry) { return entry.cell === target; })[0];
      if (!from || !to) {
        anchorCell = target;
        setSelection([target], target);
        return;
      }
      var top = Math.min(from.row, to.row);
      var bottom = Math.max(from.row + from.rowSpan - 1, to.row + to.rowSpan - 1);
      var left = Math.min(from.column, to.column);
      var right = Math.max(from.column + from.colSpan - 1, to.column + to.colSpan - 1);
      var cells = layout.entries.filter(function (entry) {
        return entry.row <= bottom && entry.row + entry.rowSpan - 1 >= top &&
          entry.column <= right && entry.column + entry.colSpan - 1 >= left;
      }).map(function (entry) { return entry.cell; });
      setSelection(cells, target);
    }

    gridScroll.addEventListener("click", function (event) {
      var cell = event.target && event.target.closest ? event.target.closest("th,td") : null;
      if (!cell || !currentTable() || !currentTable().contains(cell)) return;
      if (event.shiftKey && anchorCell) selectRange(cell);
      else {
        anchorCell = cell;
        setSelection([cell], cell);
      }
    });
    gridScroll.addEventListener("focusin", function (event) {
      var cell = event.target && event.target.closest ? event.target.closest("th,td") : null;
      if (cell && currentTable() && currentTable().contains(cell)) {
        activeCell = cell;
        if (!selectedCells.length || selectedCells.indexOf(cell) === -1) {
          anchorCell = cell;
          selectedCells = [cell];
        }
        refreshCellState();
      }
    });

    function requireSimpleGrid() {
      if (!tableHasSpans(currentTable())) return true;
      setEditorStatus(status, "Split merged cells before changing rows or columns.", true);
      return false;
    }

    function addRow() {
      var table = currentTable();
      if (!table || !requireSimpleGrid()) return;
      var columnCount = Math.max(1, table.rows[0] ? table.rows[0].cells.length : 1);
      var rowIndex = activeCell && activeCell.parentElement ? activeCell.parentElement.rowIndex + 1 : table.rows.length;
      var row = table.insertRow(Math.min(rowIndex, table.rows.length));
      for (var i = 0; i < columnCount; i++) {
        var cell = row.insertCell(-1);
        cell.setAttribute("contenteditable", "true");
        cell.setAttribute("tabindex", "0");
        cell.innerHTML = "<br>";
      }
      anchorCell = row.cells[0];
      setSelection(anchorCell ? [anchorCell] : [], anchorCell);
      scheduleTablePreview();
    }

    function deleteRow() {
      var table = currentTable();
      if (!table || table.rows.length <= 1 || !requireSimpleGrid()) return;
      var rowIndex = activeCell && activeCell.parentElement ? activeCell.parentElement.rowIndex : table.rows.length - 1;
      table.deleteRow(rowIndex);
      var next = table.rows[Math.min(rowIndex, table.rows.length - 1)];
      anchorCell = next && next.cells[0];
      setSelection(anchorCell ? [anchorCell] : [], anchorCell);
      scheduleTablePreview();
    }

    function insertCellForRow(row, index) {
      var tag = row.cells[0] && row.cells[0].nodeName.toLowerCase() === "th" ? "th" : "td";
      var cell = document.createElement(tag);
      cell.setAttribute("contenteditable", "true");
      cell.setAttribute("tabindex", "0");
      cell.innerHTML = "<br>";
      row.insertBefore(cell, row.cells[index] || null);
      return cell;
    }

    function addColumn() {
      var table = currentTable();
      if (!table || !requireSimpleGrid()) return;
      var activeRowIndex = activeCell && activeCell.parentElement ? activeCell.parentElement.rowIndex : 0;
      var index = activeCell ? activeCell.cellIndex + 1 : (table.rows[0] ? table.rows[0].cells.length : 0);
      var selected = null;
      Array.prototype.slice.call(table.rows).forEach(function (row, rowIndex) {
        var cell = insertCellForRow(row, index);
        if (rowIndex === activeRowIndex) selected = cell;
      });
      if (state && state.alignments) state.alignments.splice(index, 0, "none");
      anchorCell = selected;
      setSelection(selected ? [selected] : [], selected);
      scheduleTablePreview();
    }

    function deleteColumn() {
      var table = currentTable();
      if (!table || !table.rows.length || table.rows[0].cells.length <= 1 || !requireSimpleGrid()) return;
      var index = activeCell ? activeCell.cellIndex : table.rows[0].cells.length - 1;
      Array.prototype.slice.call(table.rows).forEach(function (row) {
        if (row.cells[index]) row.deleteCell(index);
      });
      if (state && state.alignments) state.alignments.splice(index, 1);
      var firstRow = table.rows[0];
      anchorCell = firstRow && firstRow.cells[Math.min(index, firstRow.cells.length - 1)];
      setSelection(anchorCell ? [anchorCell] : [], anchorCell);
      scheduleTablePreview();
    }

    function mergeCells() {
      var table = currentTable();
      if (!table || selectedCells.length < 2) return;
      var layout = tableMatrix(table);
      var entries = layout.entries.filter(function (entry) {
        return selectedCells.indexOf(entry.cell) !== -1;
      });
      var top = Math.min.apply(Math, entries.map(function (entry) { return entry.row; }));
      var left = Math.min.apply(Math, entries.map(function (entry) { return entry.column; }));
      var bottom = Math.max.apply(Math, entries.map(function (entry) { return entry.row + entry.rowSpan - 1; }));
      var right = Math.max.apply(Math, entries.map(function (entry) { return entry.column + entry.colSpan - 1; }));
      for (var r = top; r <= bottom; r++) {
        for (var c = left; c <= right; c++) {
          var covered = layout.matrix[r] && layout.matrix[r][c];
          if (!covered || selectedCells.indexOf(covered.cell) === -1) {
            setEditorStatus(status, "Select a complete rectangular cell range to merge.", true);
            return;
          }
        }
      }
      var topLeft = layout.matrix[top][left].cell;
      var content = entries.map(function (entry) {
        return entry.cell.innerHTML.trim();
      }).filter(Boolean);
      topLeft.innerHTML = content.join("<br>") || "<br>";
      topLeft.rowSpan = bottom - top + 1;
      topLeft.colSpan = right - left + 1;
      entries.forEach(function (entry) {
        if (entry.cell !== topLeft && entry.cell.parentNode) entry.cell.parentNode.removeChild(entry.cell);
      });
      forceHtml = true;
      anchorCell = topLeft;
      setSelection([topLeft], topLeft);
      setEditorStatus(status, "Merged cells will be saved as an HTML table.", false);
      scheduleTablePreview();
    }

    function insertBlankCellAt(table, rowIndex, columnIndex, tagName) {
      var row = table.rows[rowIndex];
      if (!row) return null;
      var layout = tableMatrix(table);
      var before = null;
      layout.entries.some(function (entry) {
        if (entry.row === rowIndex && entry.column >= columnIndex) {
          before = entry.cell;
          return true;
        }
        return false;
      });
      var cell = document.createElement(tagName || "td");
      cell.setAttribute("contenteditable", "true");
      cell.setAttribute("tabindex", "0");
      cell.innerHTML = "<br>";
      row.insertBefore(cell, before);
      return cell;
    }

    function splitCell() {
      var table = currentTable();
      if (!table || !activeCell) return;
      var layout = tableMatrix(table);
      var entry = layout.entries.filter(function (item) { return item.cell === activeCell; })[0];
      if (!entry || (entry.rowSpan === 1 && entry.colSpan === 1)) return;
      var tag = activeCell.nodeName.toLowerCase();
      activeCell.rowSpan = 1;
      activeCell.colSpan = 1;
      for (var r = entry.row; r < entry.row + entry.rowSpan; r++) {
        for (var c = entry.column; c < entry.column + entry.colSpan; c++) {
          if (r === entry.row && c === entry.column) continue;
          insertBlankCellAt(table, r, c, tag);
        }
      }
      anchorCell = activeCell;
      setSelection([activeCell], activeCell);
      scheduleTablePreview();
    }

    function formatCell(command) {
      if (!activeCell) return;
      activeCell.focus();
      document.execCommand(command, false, null);
      scheduleTablePreview();
    }

    gridTools.appendChild(editorButton("+ Row", "Insert row", addRow));
    gridTools.appendChild(editorButton("- Row", "Delete row", deleteRow));
    gridTools.appendChild(editorButton("+ Col", "Insert column", addColumn));
    gridTools.appendChild(editorButton("- Col", "Delete column", deleteColumn));
    gridTools.appendChild(editorButton("Merge", "Merge selected cells", mergeCells));
    gridTools.appendChild(editorButton("Split", "Split active merged cell", splitCell));
    gridTools.appendChild(editorButton("B", "Bold cell text", function () { formatCell("bold"); }));
    gridTools.appendChild(editorButton("I", "Italic cell text", function () { formatCell("italic"); }));
    gridTools.appendChild(editorButton("S", "Strikethrough cell text", function () { formatCell("strikeThrough"); }));
    gridPanel.appendChild(gridTools);
    gridPanel.appendChild(gridScroll);

    function validateSource() {
      var parsed = parseTableEditorState(source.value);
      valid = !!parsed;
      if (valid) {
        markdownRenderer.renderInto(rendered, source.value, renderGeneration);
        setEditorStatus(status, "Table preview ready.", false);
      } else {
        rendered.innerHTML = "";
        setEditorStatus(status, "Source must contain a Markdown or HTML table.", true);
      }
      if (saveControl) saveControl.refresh();
    }

    function scheduleSourceValidation() {
      if (previewTimer) window.clearTimeout(previewTimer);
      dirty = true;
      valid = false;
      if (saveControl) saveControl.refresh();
      previewTimer = window.setTimeout(function () {
        previewTimer = 0;
        validateSource();
      }, 220);
    }

    function scheduleTablePreview() {
      if (previewTimer) window.clearTimeout(previewTimer);
      dirty = true;
      valid = !!currentTable();
      if (saveControl) saveControl.refresh();
      previewTimer = window.setTimeout(function () {
        previewTimer = 0;
        markdownRenderer.renderInto(rendered, currentContent(), renderGeneration);
        valid = !!currentTable();
        if (saveControl) saveControl.refresh();
      }, 120);
    }

    function validateCurrentTable() {
      if (previewTimer) {
        window.clearTimeout(previewTimer);
        previewTimer = 0;
      }
      if (sourceMode) {
        validateSource();
      } else {
        valid = !!currentTable() && !!parseTableEditorState(currentContent());
        if (saveControl) saveControl.refresh();
      }
      return valid;
    }

    function switchMode(toSource) {
      if (toSource === sourceMode) return;
      if (toSource) {
        source.value = currentContent();
        sourceMode = true;
        gridPanel.hidden = true;
        sourcePanel.hidden = false;
        gridTab.classList.remove("is-active");
        sourceTab.classList.add("is-active");
        validateSource();
        source.focus();
      } else {
        var parsed = parseTableEditorState(source.value);
        if (!parsed) {
          setEditorStatus(status, "Fix the table source before returning to Grid.", true);
          return;
        }
        state = parsed;
        forceHtml = state.format === "html";
        prepareGridTable(state.table.cloneNode(true));
        sourceMode = false;
        sourcePanel.hidden = true;
        gridPanel.hidden = false;
        sourceTab.classList.remove("is-active");
        gridTab.classList.add("is-active");
        valid = true;
        if (saveControl) saveControl.refresh();
        if (activeCell) activeCell.focus();
      }
    }

    source.addEventListener("input", scheduleSourceValidation);
    gridScroll.addEventListener("input", scheduleTablePreview);
    bodyHost.classList.add("ocr-preview-table-editor");
    bodyHost.appendChild(modeBar);
    bodyHost.appendChild(gridPanel);
    bodyHost.appendChild(sourcePanel);
    bodyHost.appendChild(status);

    if (state) {
      prepareGridTable(state.table.cloneNode(true));
      source.value = currentContent();
      markdownRenderer.renderInto(rendered, source.value, renderGeneration);
      setEditorStatus(status, "Shift-click cells to select a merge range.", false);
    } else {
      sourceMode = true;
      gridPanel.hidden = true;
      sourcePanel.hidden = false;
      gridTab.classList.remove("is-active");
      sourceTab.classList.add("is-active");
      source.value = original;
      setEditorStatus(status, "Source must contain a Markdown or HTML table.", true);
    }

    saveControl = appendEditorActions(
      toolbar,
      editTransaction.activeId(),
      block,
      currentContent,
      function () { return valid; },
      validateCurrentTable);
    return {
      actionsAdded: true,
      saveControl: saveControl,
      cleanup: function () { if (previewTimer) window.clearTimeout(previewTimer); },
      focus: function () {
        if (sourceMode) source.focus();
        else if (activeCell) activeCell.focus();
      },
      readContent: currentContent,
      canSave: function () { return valid; },
      validateBeforeSave: validateCurrentTable
    };
  }

  function parseImageEditorState(content, element) {
    var raw = String(content || "");
    var renderedImage = null;
    if (element) {
      renderedImage = element.nodeName && element.nodeName.toLowerCase() === "img"
        ? element
        : (element.querySelector ? element.querySelector("img") : null);
    }
    var state = {
      format: /<img\b/i.test(raw) ? "html" : "markdown",
      source: "",
      alt: renderedImage ? (renderedImage.getAttribute("alt") || "") : "",
      title: renderedImage ? (renderedImage.getAttribute("title") || "") : "",
      target: "",
      caption: "",
      renderedSource: renderedImage ? (renderedImage.currentSrc || renderedImage.src || "") : ""
    };

    var markdownImageFound = false;
    if (state.format === "markdown") {
      try {
        markdownRenderer.parseSource(raw, {}).some(function (token) {
          if (!token.children) return false;
          var activeLink = "";
          return token.children.some(function (child) {
            if (child.type === "link_open") activeLink = child.attrGet("href") || "";
            if (child.type === "link_close") activeLink = "";
            if (child.type !== "image") return false;
            state.alt = child.content || "";
            state.source = child.attrGet("src") || "";
            state.title = child.attrGet("title") || "";
            state.target = activeLink;
            markdownImageFound = true;
            return true;
          });
        });
      } catch (_) {
        markdownImageFound = false;
      }
    }
    if (!markdownImageFound && state.format === "markdown") {
      var imageMatch = /!\[([^\]]*)\]\(\s*(?:<([^>\r\n]+)>|([^\s)\r\n]+))(?:\s+["']([^"'\r\n]*)["'])?\s*\)/.exec(raw);
      if (imageMatch) {
        state.alt = imageMatch[1] || "";
        state.source = imageMatch[2] || imageMatch[3] || "";
        state.title = imageMatch[4] || "";
      }
    } else if (state.format === "html") {
      var holder = document.createElement("div");
      holder.innerHTML = security.sanitizeHtml(raw);
      var img = holder.querySelector("img");
      if (img) {
        state.source = img.getAttribute("src") || "";
        state.alt = img.hasAttribute("alt") ? (img.getAttribute("alt") || "") : state.alt;
        state.title = img.getAttribute("title") || "";
        var link = img.closest ? img.closest("a") : null;
        if (link) state.target = link.getAttribute("href") || "";
        var figure = img.closest ? img.closest("figure") : null;
        var figcaption = figure && figure.querySelector("figcaption");
        if (figcaption) state.caption = figcaption.textContent || "";
      }
      if (!state.source) {
        var srcMatch = /\bsrc\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i.exec(raw);
        if (srcMatch) state.source = srcMatch[1] || srcMatch[2] || srcMatch[3] || "";
      }
    }
    if (!state.source && renderedImage) state.source = renderedImage.getAttribute("src") || "";
    return state;
  }

  function markdownImageUrl(value) {
    var url = String(value || "").trim();
    return editorMarkdown.destination(url);
  }

  function markdownImageText(value) {
    return String(value || "").replace(/\\/g, "\\\\").replace(/\]/g, "\\]");
  }

  function buildImageEditor(toolbar, bodyHost, block, element) {
    var original = block.visibleSourceContent || block.content || "";
    var parsed = parseImageEditorState(original, element);
    var valid = false;
    var saveControl = null;

    var layout = document.createElement("div");
    layout.className = "ocr-preview-image-editor-layout";
    var visual = document.createElement("div");
    visual.className = "ocr-preview-image-editor-visual";
    var image = document.createElement("img");
    image.className = "ocr-preview-image-editor-preview";
    image.alt = parsed.alt || "Image preview";
    var previewCaption = document.createElement("div");
    previewCaption.className = "ocr-preview-image-editor-caption";
    visual.appendChild(image);
    visual.appendChild(previewCaption);

    var fields = document.createElement("div");
    fields.className = "ocr-preview-image-editor-fields";

    function field(labelText, value, multiline) {
      var label = document.createElement("label");
      label.className = "ocr-preview-editor-field";
      var name = document.createElement("span");
      name.textContent = labelText;
      var input = multiline ? document.createElement("textarea") : document.createElement("input");
      input.className = multiline
        ? "ocr-preview-editor-textarea ocr-preview-image-caption-input"
        : "ocr-preview-editor-input";
      input.value = value || "";
      label.appendChild(name);
      label.appendChild(input);
      fields.appendChild(label);
      return input;
    }

    var altInput = field("Alt text", parsed.alt, false);
    var sourceInput = field("Image source", parsed.source, false);
    var targetInput = field("Open link", parsed.target, false);
    var titleInput = field("Title", parsed.title, false);
    var captionInput = field("Caption", parsed.caption, true);
    var status = editorStatus();

    function sourceIsValid(value) {
      value = String(value || "").trim();
      if (!value) return false;
      if (value === parsed.source && parsed.renderedSource) return true;
      return security.isSafeImageSrc(value);
    }

    function targetIsValid(value) {
      value = String(value || "").trim();
      return !value || security.isSafeLinkHref(value);
    }

    function formattedContent() {
      var alt = altInput.value.trim();
      var src = sourceInput.value.trim();
      var target = targetInput.value.trim();
      var title = titleInput.value.trim();
      var caption = captionInput.value.trim();
      if (alt === parsed.alt && src === parsed.source && target === parsed.target &&
          title === parsed.title && caption === parsed.caption) {
        return original;
      }
      var core;
      if (parsed.format === "html") {
        var holder = document.createElement("div");
        holder.innerHTML = security.sanitizeHtml(original);
        var originalImage = holder.querySelector("img");
        if (!originalImage) return "";
        originalImage.setAttribute("src", src);
        originalImage.setAttribute("alt", alt);
        if (title) originalImage.setAttribute("title", title);
        else originalImage.removeAttribute("title");

        var link = originalImage.closest ? originalImage.closest("a") : null;
        if (target) {
          if (!link) {
            link = document.createElement("a");
            originalImage.parentNode.insertBefore(link, originalImage);
            link.appendChild(originalImage);
          }
          link.setAttribute("href", target);
        } else if (link && link.parentNode) {
          link.parentNode.insertBefore(originalImage, link);
          link.parentNode.removeChild(link);
          link = null;
        }

        var figure = originalImage.closest ? originalImage.closest("figure") : null;
        var figcaption = figure && figure.querySelector("figcaption");
        if (caption) {
          if (!figure) {
            var imageCore = link || originalImage;
            figure = document.createElement("figure");
            imageCore.parentNode.insertBefore(figure, imageCore);
            figure.appendChild(imageCore);
          }
          if (!figcaption) {
            figcaption = document.createElement("figcaption");
            figure.appendChild(figcaption);
          }
          figcaption.textContent = caption;
        } else if (figcaption && figcaption.parentNode) {
          figcaption.parentNode.removeChild(figcaption);
        }
        core = security.sanitizeHtml(holder.innerHTML).trim();
      } else {
        core = "![" + markdownImageText(alt) + "](" + markdownImageUrl(src);
        if (title) core += " \"" + title.replace(/"/g, "\\\"") + "\"";
        core += ")";
        if (target) core = "[" + core + "](" + markdownImageUrl(target) + ")";
        if (caption) core += "\n\n" + caption;
      }
      return core;
    }

    function refreshPreview() {
      var src = sourceInput.value.trim();
      var sourceOk = sourceIsValid(src);
      var targetOk = targetIsValid(targetInput.value);
      valid = sourceOk && targetOk;
      image.alt = altInput.value.trim() || "Image preview";
      previewCaption.textContent = captionInput.value.trim();
      if (sourceOk) {
        image.hidden = false;
        image.src = (src === parsed.source && parsed.renderedSource) ? parsed.renderedSource : src;
      } else {
        image.hidden = true;
        image.removeAttribute("src");
      }
      setEditorStatus(status,
        valid ? "Image preview ready." : (!sourceOk ? "Enter a safe image source." : "Open link must be an external HTTP(S) URL."),
        !valid);
      if (saveControl) saveControl.refresh();
    }

    [altInput, sourceInput, targetInput, titleInput, captionInput].forEach(function (input) {
      input.addEventListener("input", refreshPreview);
    });
    image.addEventListener("error", function () {
      setEditorStatus(status, "The image could not be previewed here; the safe URL can still be saved.", false);
      if (saveControl) saveControl.refresh();
    });
    image.addEventListener("load", function () {
      if (valid) setEditorStatus(status, "Image preview ready.", false);
    });

    layout.appendChild(visual);
    layout.appendChild(fields);
    bodyHost.classList.add("ocr-preview-image-editor");
    bodyHost.appendChild(layout);
    bodyHost.appendChild(status);
    saveControl = appendEditorActions(toolbar, editTransaction.activeId(), block, formattedContent, function () { return valid; });
    refreshPreview();

    return {
      actionsAdded: true,
      saveControl: saveControl,
      focus: function () { altInput.focus(); altInput.select(); },
      readContent: formattedContent,
      canSave: function () { return valid; }
    };
  }

  function startInlineEditor(id) {
    id = String(id || "");
    var block = findBlockById(id);
    var elements = findBlockElements(id);
    var element = elements.length ? elements[0] : null;
    if (!id || !block || !element || !blockSupportsInlineEditing(block)) {
      applyBlockState();
      postMessage({ type: "previewBlockEditFailed", id: id, renderToken: currentRenderToken });
      return;
    }
    if (editTransaction.activeId() === id && editTransaction.activeHost()) return;
    finishInlineEditor(false);
    selectedBlockId = id;
    hideFloatingToolbar();

    var host = document.createElement("div");
    host.className = "ocr-preview-inline-editor ocr-preview-linked-block is-editing";
    host.setAttribute("data-block-id", id);

    var toolbar = document.createElement("div");
    toolbar.className = "ocr-preview-inline-editor-toolbar";
    var bodyHost = document.createElement("div");
    bodyHost.className = "ocr-preview-editor";
    host.appendChild(toolbar);
    host.appendChild(bodyHost);

    var originalDisplays = elements.map(function (original) {
      return original.style.display || "";
    });
    editTransaction.open({
      id: id,
      block: block,
      host: host,
      originals: elements,
      originalDisplays: originalDisplays
    });

    var kind = blockMapper.kindOf(block);
    var editor;
    if (kind === "formula") editor = buildFormulaEditor(toolbar, bodyHost, block, element);
    else if (kind === "table") editor = buildTableEditor(toolbar, bodyHost, block, element);
    else if (kind === "image") editor = buildImageEditor(toolbar, bodyHost, block, element);
    else editor = buildTextEditor(toolbar, bodyHost, block, element);

    var actions = null;
    if (!editor.actionsAdded) {
      actions = appendEditorActions(toolbar, id, block, editor.readContent, editor.canSave);
    }
    var saveHandler = actions ? actions.save : function () {
      if (editor.validateBeforeSave && !editor.validateBeforeSave()) return;
      if (editor.canSave()) commitPreviewBlockSave(id, block, editor.readContent());
    };

    element.parentNode.insertBefore(host, element);
    editTransaction.setEditorHandle({
      id: id,
      cleanup: editor.cleanup,
      saveControl: actions || editor.saveControl,
      saveHandler: saveHandler
    });

    host.addEventListener("keydown", function (event) {
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        postMessage({ type: "previewBlockCancel", id: id, renderToken: currentRenderToken });
        finishInlineEditor(true);
      } else if (event.ctrlKey && !event.shiftKey && event.key.toLowerCase() === "s") {
        event.preventDefault();
        editTransaction.triggerSave();
      }
    });

    window.setTimeout(function () {
      if (editor.focus) editor.focus();
      applyBlockState({ ensureVisible: true });
    }, 0);
  }

  function requestPreviewEdit(id) {
    id = String(id || "");
    if (!id) return;
    setPreviewSelection(id, true);
    postMessage({ type: "previewBlockEdit", id: id, renderToken: currentRenderToken });
    startInlineEditor(id);
  }

  function setPreviewEditing(id) {
    id = String(id || "");
    if (!id) {
      finishInlineEditor(true);
      applyBlockState();
      return;
    }
    startInlineEditor(id);
  }

  function bindBlockElement(element, block) {
    element.addEventListener("mouseenter", function () {
      clearToolbarHideTimer();
      setPreviewHover(block.id);
      postMessage({ type: "previewBlockHover", id: block.id });
      syncFloatingToolbar();
    });
    element.addEventListener("mouseleave", function () {
      scheduleHoverClear(block.id);
    });
    element.addEventListener("click", function (event) {
      if (isActionOrEditorTarget(event.target)) return;
      setPreviewSelection(block.id, false);
      postMessage({ type: "previewBlockSelect", id: block.id });
      syncFloatingToolbar();
    });
    element.addEventListener("dblclick", function (event) {
      if (isActionOrEditorTarget(event.target)) return;
      if (!blockSupportsInlineEditing(block)) return;
      event.preventDefault();
      requestPreviewEdit(block.id);
    });
    element.addEventListener("keydown", function (event) {
      if ((event.key === "F2" || (event.ctrlKey && event.key === "Enter")) &&
          blockSupportsInlineEditing(block)) {
        event.preventDefault();
        requestPreviewEdit(block.id);
      } else if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        setPreviewSelection(block.id, false);
        postMessage({ type: "previewBlockSelect", id: block.id });
        syncFloatingToolbar();
      }
    });
  }

  function addBlockAlias(element, block) {
    if (!element || !block || !block.id) return;
    var aliases = (element.getAttribute("data-linked-block-ids") || "")
      .split("\n")
      .filter(function (id) { return !!id; });
    if (aliases.indexOf(block.id) === -1) aliases.push(block.id);
    element.setAttribute("data-linked-block-ids", aliases.join("\n"));
    element._ocrPreviewBlockIds = null;
  }

  function decorateBlockElement(element, block) {
    element.classList.add("ocr-preview-linked-block");
    if (block.edited) element.classList.add("is-edited");
    element.setAttribute("data-block-id", block.id);
    element._ocrPreviewBlockIds = new Set([block.id]);
    element.setAttribute("data-block-order", String(block.order || ""));
    element.setAttribute("data-block-label", block.displayLabel || block.label || "");
    if (!element.hasAttribute("tabindex")) element.setAttribute("tabindex", "0");
    if (!element.getAttribute("title")) {
      element.setAttribute("title", "#" + block.order + " " + (block.displayLabel || block.label || "Block"));
    }
    bindBlockElement(element, block);
  }

  function decorateRenderedMarkdownWithBlocks(blocks) {
    var mapping = blockMapper.map(blocks, currentSourceMarkdown);
    mapping.assignments.forEach(function (assignment) {
      assignment.block.visibleSourceContent = assignment.sourceContent;
      assignment.block.visibleSourceStart = assignment.sourceStart;
      assignment.block.visibleSourceEnd = assignment.sourceEnd;
      assignment.nodes.forEach(function (node) {
        decorateBlockElement(node, assignment.block);
      });
    });
    mapping.aliases.forEach(function (alias) {
      if (typeof alias.sourceContent === "string") {
        alias.block.visibleSourceContent = alias.sourceContent;
        alias.block.visibleSourceStart = alias.sourceStart;
        alias.block.visibleSourceEnd = alias.sourceEnd;
      }
      alias.nodes.forEach(function (node) { addBlockAlias(node, alias.block); });
    });
  }

  function render(payload) {
    if (!payload || payload.type !== "render") return;
    var generation = ++renderGeneration;
    // Set the token before validating the payload so every render failure can
    // be attributed to the render that produced it. The native host uses this
    // value to ignore stale errors from an older document.
    currentRenderToken = String(payload.renderToken || "");
    lastMetricsKey = "";
    markdownRenderer.destroy();
    finishInlineEditor(false);
    clearToolbarHideTimer();
    resetOverlayScrollbarForRender();
    floatingToolbar = null;
    toolbarBlockId = "";
    toolbarHovering = false;

    if (typeof payload.markdown !== "string") {
      setEmpty("No OCR record selected.");
      return;
    }
    if (payload.markdown.length > 2 * 1024 * 1024) {
      setError("Markdown is too large to preview.");
      postMessage({ type: "renderError", recordId: payload.recordId || -1,
        renderToken: currentRenderToken,
        message: "Markdown is too large to preview." });
      return;
    }

    currentRecordId = typeof payload.recordId === "number" ? payload.recordId : -1;
    currentSourceMarkdown = typeof payload.sourceMarkdown === "string"
      ? payload.sourceMarkdown
      : payload.markdown;
    currentSourceMarkdown = String(currentSourceMarkdown || "").replace(/\r\n?/g, "\n");
    currentCanonicalSource = String(payload.canonicalSource || "markdown-body-lf");
    currentOffsetUnit = String(payload.offsetUnit || "utf16-code-unit");
    currentRevisionSha256 = String(payload.revisionSha256 || "");
    currentBlocks = blockMapper.normalizeBlocks(payload.blocks);
    hoveredBlockId = String(payload.hoveredBlockId || "");
    selectedBlockId = String(payload.selectedBlockId || "");
    if (!payload.markdown.trim()) {
      setEmpty("This OCR record is empty.");
      return;
    }

    try {
      preview.className = "markdown-body" + (payload.compactLayout ? " compact-preview" : "");
      markdownRenderer.renderInto(preview, payload.markdown, generation);
      security.observeRenderedImages(
        preview, generation, currentRecordId, currentRenderToken,
        function (candidate) { return candidate === renderGeneration; },
        postMessage);
      if (payload.blocksTruncated) {
        var limitNotice = document.createElement("div");
        limitNotice.className = "ocr-preview-block-limit-notice";
        limitNotice.textContent = "Block interactions were limited for this large document; Markdown rendering is complete.";
        preview.insertBefore(limitNotice, preview.firstChild);
      }
      if (currentBlocks.length) {
        decorateRenderedMarkdownWithBlocks(currentBlocks);
        applyBlockState();
        if (payload.editingBlockId) setPreviewEditing(String(payload.editingBlockId));
      }
      preview.scrollTop = 0;
      scheduleContentMetrics();
    } catch (error) {
      var message = error && error.message ? error.message : String(error);
      setError(message);
      postMessage({ type: "renderError", recordId: currentRecordId,
        renderToken: currentRenderToken, message: message });
    }
  }

  function handleHostMessage(data) {
    if (!data || typeof data.type !== "string") return;
    if (data.type === "setPreviewFontSize") {
      var fontSize = Number(data.fontSize);
      if (!isFinite(fontSize)) fontSize = 14;
      fontSize = Math.max(8, Math.min(32, fontSize));
      preview.style.setProperty("--preview-font-size", fontSize + "px");
      scheduleContentMetrics();
      return;
    }
    if (data.type === "previewScrollbarBoundaryEnter") {
      setOverlayScrollbarBoundaryHover("native", true);
      return;
    }
    if (data.type === "previewScrollbarBoundaryLeave") {
      setOverlayScrollbarBoundaryHover("native", false);
      return;
    }
    if (data.type === "render") {
      render(data);
      return;
    }
    if (data.type === "setPreviewHover") {
      setPreviewHover(data.id || "");
      return;
    }
    if (data.type === "setPreviewSelection") {
      setPreviewSelection(data.id || "", !!data.ensureVisible);
      return;
    }
    if (data.type === "setPreviewEditing") {
      setPreviewEditing(data.id || "");
      return;
    }
    if (data.type === "previewBlockSaveResult") {
      var pending = editTransaction.takeSaveResult(data);
      if (!pending) return;
      if (data.success) {
        pending.block.content = pending.content;
        pending.block.searchText = blockMapper.normalizeText(pending.content);
        pending.block.visibleSourceContent = pending.content;
        pending.block.edited = true;
        finishInlineEditor(true);
      } else {
        var messages = {
          stale_target: "The preview changed before this save completed. Reopen the editor and try again.",
          invalid_request: "The edit no longer matches the source document.",
          persist_failed: "The edit could not be saved. Your text is still available in the editor.",
          rollback_failed: "Saving failed and automatic recovery was incomplete. Stop editing and inspect the output files."
        };
        editTransaction.setStatus(messages[data.errorCode] || messages.persist_failed, true);
        editTransaction.refreshControl();
      }
      return;
    }
    if (data.type === "previewBlockRestoreResult") {
      var restorePending = editTransaction.takeRestoreResult(data);
      if (!restorePending) return;
      if (data.success) {
        finishInlineEditor(true);
      } else {
        var restoreMessages = {
          stale_target: "The preview changed before the restore completed. Reopen the editor and try again.",
          restore_unavailable: "The original OCR result is not available for this block. Run OCR again to recreate it.",
          invalid_request: "The edited source changed before it could be restored. Reopen the editor and try again.",
          persist_failed: "The original OCR result could not be restored. Your edited content was preserved.",
          rollback_failed: "Restoring failed and automatic recovery was incomplete. Stop editing and inspect the output files."
        };
        editTransaction.setStatus(restoreMessages[data.errorCode] || restoreMessages.persist_failed, true);
        editTransaction.refreshControl();
      }
      return;
    }
  }

  preview.addEventListener("scroll", function () {
    syncFloatingToolbar();
    syncOverlayScrollbar();
  });
  preview.addEventListener("mousemove", function (event) {
    var rect = preview.getBoundingClientRect();
    setOverlayScrollbarBoundaryHover(
      "preview",
      event.clientX >= rect.right - kOverlayScrollbarBoundaryWidth);
  });
  preview.addEventListener("mouseleave", function () {
    setOverlayScrollbarBoundaryHover("preview", false);
  });
  if (overlayScrollbar) {
    overlayScrollbar.addEventListener("pointerdown", beginOverlayScrollbarDrag);
    overlayScrollbar.addEventListener("pointermove", moveOverlayScrollbarDrag);
    overlayScrollbar.addEventListener("pointerup", endOverlayScrollbarDrag);
    overlayScrollbar.addEventListener("pointercancel", endOverlayScrollbarDrag);
    overlayScrollbar.addEventListener("lostpointercapture", function (event) {
      if (event.pointerId !== overlayScrollbarDragPointerId) return;
      overlayScrollbarDragPointerId = -1;
      overlayScrollbarDragOffset = 0;
      deferOverlayScrollbarHide(kOverlayScrollbarHideDelay);
    });
    overlayScrollbar.addEventListener("pointerenter", function () {
      overlayScrollbarPointerHovered = true;
      clearOverlayScrollbarBoundaryHoverTimer();
      clearOverlayScrollbarHideTimer();
      revealOverlayScrollbar();
    });
    overlayScrollbar.addEventListener("pointerleave", function () {
      overlayScrollbarPointerHovered = false;
      deferOverlayScrollbarHide(kOverlayScrollbarHideDelay);
    });
  }
  window.addEventListener("resize", function () {
    syncFloatingToolbar();
    syncOverlayScrollbar();
    if (isOverlayScrollbarBoundaryHovered()) {
      scheduleOverlayScrollbarReveal();
    }
  });

  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.addEventListener("message", function (event) {
      handleHostMessage(event.data);
    });
    postMessage({ type: "ready" });
  }
})();
