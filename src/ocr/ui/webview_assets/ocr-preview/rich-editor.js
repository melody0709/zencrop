(function () {
  "use strict";

  function createEditor(options) {
    var uiAsset = window.ZenCropPreviewRichEditorUi;
    var tableAsset = window.ZenCropPreviewEditorTable;
    if (!options || !options.bodyHost || typeof options.renderInto !== "function" ||
        typeof options.serialize !== "function" || !uiAsset || typeof uiAsset.create !== "function" ||
        !tableAsset || typeof tableAsset.create !== "function") {
      throw new Error("Rich editor dependencies are unavailable.");
    }

    var originalSource = String(options.source || "");
    var body = document.createElement("div");
    body.className = "ocr-preview-rich-editor-body";
    body.setAttribute("contenteditable", "true");
    body.setAttribute("role", "textbox");
    body.setAttribute("aria-multiline", "true");
    body.setAttribute("spellcheck", "true");
    options.renderInto(body, originalSource);
    if (!body.innerHTML.trim()) body.innerHTML = "<p><br></p>";
    options.bodyHost.appendChild(body);

    var destroyed = false;
    var composing = false;
    var applyingSnapshot = false;
    var dirty = false;
    var savedBookmark = null;
    var snapshotTimer = 0;
    var undoStack = [];
    var redoStack = [];
    var listeners = [];
    var actionConfig = null;
    var activeTable = null;
    var activeCell = null;
    var tableAnchorCell = null;
    var selectedTableCells = [];
    var tableDragging = false;
    var kUndoLimit = 50;
    var kInputMergeMs = 350;

    function listen(target, type, handler, capture) {
      target.addEventListener(type, handler, !!capture);
      listeners.push(function () { target.removeEventListener(type, handler, !!capture); });
    }

    function closestElement(node, selector) {
      if (node && node.nodeType === Node.TEXT_NODE) node = node.parentNode;
      return node && node.closest ? node.closest(selector) : null;
    }

    function pathFromRoot(node) {
      var path = [];
      while (node && node !== body) {
        var parent = node.parentNode;
        if (!parent) return null;
        path.unshift(Array.prototype.indexOf.call(parent.childNodes, node));
        node = parent;
      }
      return node === body ? path : null;
    }

    function nodeFromPath(path) {
      var node = body;
      for (var i = 0; i < path.length; i++) {
        if (!node || !node.childNodes || path[i] < 0 || path[i] >= node.childNodes.length) return null;
        node = node.childNodes[path[i]];
      }
      return node;
    }

    function clampOffset(node, offset) {
      if (!node) return 0;
      var length = node.nodeType === Node.TEXT_NODE
        ? String(node.nodeValue || "").length
        : (node.childNodes ? node.childNodes.length : 0);
      return Math.max(0, Math.min(Number(offset) || 0, length));
    }

    function captureBookmark() {
      var selection = window.getSelection ? window.getSelection() : null;
      if (!selection || !selection.rangeCount) return savedBookmark;
      var range = selection.getRangeAt(0);
      var container = range.commonAncestorContainer;
      if (container.nodeType === Node.TEXT_NODE) container = container.parentNode;
      if (!container || !body.contains(container)) return savedBookmark;
      var startPath = pathFromRoot(range.startContainer);
      var endPath = pathFromRoot(range.endContainer);
      if (!startPath || !endPath) return savedBookmark;
      return {
        startPath: startPath,
        startOffset: range.startOffset,
        endPath: endPath,
        endOffset: range.endOffset
      };
    }

    function restoreBookmark(bookmark) {
      body.focus();
      if (!bookmark || !window.getSelection) return false;
      var start = nodeFromPath(bookmark.startPath || []);
      var end = nodeFromPath(bookmark.endPath || []);
      if (!start || !end) return false;
      try {
        var range = document.createRange();
        range.setStart(start, clampOffset(start, bookmark.startOffset));
        range.setEnd(end, clampOffset(end, bookmark.endOffset));
        var selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        savedBookmark = captureBookmark();
        return true;
      } catch (_) {
        return false;
      }
    }

    function activeRange() {
      var selection = window.getSelection ? window.getSelection() : null;
      if (!selection || !selection.rangeCount) return null;
      var range = selection.getRangeAt(0);
      var container = range.commonAncestorContainer;
      if (container.nodeType === Node.TEXT_NODE) container = container.parentNode;
      return container && body.contains(container) ? range : null;
    }

    function activeCollapsedRange() {
      var range = activeRange();
      return range && range.collapsed ? range : null;
    }

    function canSerialize() {
      return typeof options.canSerialize !== "function" || options.canSerialize(body);
    }

    function currentMarkdown() {
      return dirty && canSerialize() ? options.serialize(body) : originalSource;
    }

    function makeSnapshot() {
      return {
        html: body.innerHTML,
        markdown: currentMarkdown(),
        bookmark: captureBookmark() || savedBookmark,
        dirty: dirty
      };
    }

    function clearSnapshotTimer() {
      if (!snapshotTimer) return;
      window.clearTimeout(snapshotTimer);
      snapshotTimer = 0;
    }

    function refreshActions(canSave) {
      if (!ui) return;
      if (typeof canSave !== "boolean") canSave = canSerialize();
      ui.refreshActions({
        dirty: dirty,
        composing: composing,
        canSave: canSave,
        pending: !!(actionConfig && actionConfig.isPending && actionConfig.isPending()),
        canRestore: !!(actionConfig && actionConfig.canRestore)
      });
    }

    function notifyState() {
      var canSave = canSerialize();
      if (typeof options.onStateChanged === "function") {
        options.onStateChanged({
          dirty: dirty,
          composing: composing,
          canSave: canSave
        });
      }
      refreshActions(canSave);
    }

    function setDirty(value) {
      value = !!value;
      if (dirty === value) {
        notifyState();
        return;
      }
      dirty = value;
      notifyState();
    }

    function recordSnapshot() {
      clearSnapshotTimer();
      if (destroyed || composing || applyingSnapshot) return;
      var next = makeSnapshot();
      var previous = undoStack.length ? undoStack[undoStack.length - 1] : null;
      if (previous && previous.html === next.html && previous.markdown === next.markdown) {
        previous.bookmark = next.bookmark;
        previous.dirty = next.dirty;
        return;
      }
      undoStack.push(next);
      if (undoStack.length > kUndoLimit) undoStack.shift();
      redoStack = [];
    }

    function scheduleSnapshot() {
      clearSnapshotTimer();
      if (destroyed || composing || applyingSnapshot) return;
      snapshotTimer = window.setTimeout(recordSnapshot, kInputMergeMs);
    }

    function prepareTables() {
      Array.prototype.forEach.call(body.querySelectorAll("table"), function (table) {
        Array.prototype.forEach.call(table.querySelectorAll("th,td"), function (cell) {
          cell.setAttribute("tabindex", "0");
        });
      });
    }

    function clearTableSelection() {
      selectedTableCells.forEach(function (cell) {
        cell.classList.remove("is-active", "is-range-selected");
      });
      selectedTableCells = [];
    }

    function setTableSelection(table, cells, selected) {
      clearTableSelection();
      activeTable = table || null;
      activeCell = selected || (cells && cells[0]) || null;
      selectedTableCells = cells || [];
      selectedTableCells.forEach(function (cell) {
        cell.classList.toggle("is-active", cell === activeCell);
        cell.classList.toggle("is-range-selected", cell !== activeCell);
      });
      if (activeCell && !tableAnchorCell) tableAnchorCell = activeCell;
    }

    function applySnapshot(snapshot) {
      if (!snapshot) return;
      clearSnapshotTimer();
      applyingSnapshot = true;
      clearTableSelection();
      activeTable = null;
      activeCell = null;
      tableAnchorCell = null;
      body.innerHTML = snapshot.html || "<p><br></p>";
      dirty = !!snapshot.dirty;
      savedBookmark = snapshot.bookmark || null;
      prepareTables();
      applyingSnapshot = false;
      restoreBookmark(savedBookmark);
      notifyState();
      updateContextUi();
    }

    function undo() {
      if (composing) return;
      recordSnapshot();
      if (undoStack.length < 2) return;
      redoStack.push(undoStack.pop());
      applySnapshot(undoStack[undoStack.length - 1]);
    }

    function redo() {
      if (composing || !redoStack.length) return;
      var snapshot = redoStack.pop();
      undoStack.push(snapshot);
      applySnapshot(snapshot);
    }

    function rememberSelection() {
      var bookmark = captureBookmark();
      if (bookmark) savedBookmark = bookmark;
      updateContextUi();
    }

    function markChanged(immediate) {
      if (applyingSnapshot || composing) return;
      setDirty(true);
      savedBookmark = captureBookmark() || savedBookmark;
      if (immediate) recordSnapshot();
      else scheduleSnapshot();
    }

    function normalizeInputText(value) {
      return String(value || "").replace(/\u00a0/g, " ").replace(/\u200b/g, "");
    }

    function ensureEditable(target) {
      if (!target.textContent && !target.querySelector("br")) target.appendChild(document.createElement("br"));
    }

    function placeCaret(target, atEnd) {
      body.focus();
      var range = document.createRange();
      range.selectNodeContents(target);
      range.collapse(!atEnd);
      var selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      savedBookmark = captureBookmark();
    }

    function topLevelBlock(range) {
      if (!range) return null;
      var node = range.startContainer;
      if (node.nodeType === Node.TEXT_NODE) node = node.parentNode;
      while (node && node.parentNode !== body) node = node.parentNode;
      return node && node !== body ? node : null;
    }

    function topLevelTextBlock(range) {
      var block = topLevelBlock(range);
      return block && /^(p|div)$/i.test(block.nodeName || "") ? block : null;
    }

    function textBeforeCaret(block, range) {
      try {
        var prefix = document.createRange();
        prefix.selectNodeContents(block);
        prefix.setEnd(range.startContainer, range.startOffset);
        return normalizeInputText(prefix.toString());
      } catch (_) {
        return "";
      }
    }

    function moveChildren(source, target) {
      while (source.firstChild) target.appendChild(source.firstChild);
      ensureEditable(target);
    }

    function deleteToCaret(block, range) {
      var marker = document.createRange();
      marker.selectNodeContents(block);
      marker.setEnd(range.startContainer, range.startOffset);
      marker.deleteContents();
    }

    function replaceBlock(block, rule) {
      var replacement = null;
      var caretTarget = null;
      if (rule.type === "paragraph") {
        replacement = document.createElement("p");
        moveChildren(block, replacement);
        caretTarget = replacement;
      } else if (rule.type === "heading") {
        replacement = document.createElement("h" + rule.level);
        moveChildren(block, replacement);
        caretTarget = replacement;
      } else if (rule.type === "quote") {
        replacement = document.createElement("blockquote");
        caretTarget = document.createElement("p");
        moveChildren(block, caretTarget);
        replacement.appendChild(caretTarget);
      } else if (rule.type === "list") {
        replacement = document.createElement(rule.ordered ? "ol" : "ul");
        if (rule.ordered && rule.start !== 1) replacement.setAttribute("start", String(rule.start));
        caretTarget = document.createElement("li");
        moveChildren(block, caretTarget);
        replacement.appendChild(caretTarget);
      } else if (rule.type === "code") {
        replacement = document.createElement("pre");
        caretTarget = document.createElement("code");
        if (rule.language) caretTarget.className = "language-" + rule.language;
        moveChildren(block, caretTarget);
        replacement.appendChild(caretTarget);
      } else if (rule.type === "rule") {
        replacement = document.createElement("hr");
        caretTarget = document.createElement("p");
        ensureEditable(caretTarget);
        block.parentNode.insertBefore(caretTarget, block.nextSibling);
      }
      if (!replacement || !caretTarget || !block.parentNode) return false;
      block.parentNode.replaceChild(replacement, block);
      placeCaret(caretTarget, false);
      return true;
    }

    function blockRuleForText(text, allowFenceLanguage) {
      var match = /^(#{1,6}) $/.exec(text);
      if (match) return { type: "heading", level: match[1].length };
      if (text === "> ") return { type: "quote" };
      if (/^[-+*] $/.test(text)) return { type: "list", ordered: false, start: 1 };
      match = /^(\d{1,9})\. $/.exec(text);
      if (match) return { type: "list", ordered: true, start: Number(match[1]) };
      match = allowFenceLanguage
        ? /^(?:`{3,}|~{3,})([a-z0-9_+.-]*)\s*$/i.exec(text)
        : /^(?:`{3}|~{3})$/.exec(text);
      if (match) return { type: "code", language: allowFenceLanguage ? match[1] : "" };
      if (allowFenceLanguage && /^(?:-{3,}|\*{3,}|_{3,})$/.test(text)) return { type: "rule" };
      return null;
    }

    function applyBlockInputRule(block, range, rule) {
      recordSnapshot();
      deleteToCaret(block, range);
      if (!replaceBlock(block, rule)) return false;
      markChanged(true);
      return true;
    }

    function tryBlockInputRule(event) {
      if (!event || event.inputType !== "insertText") return false;
      var range = activeCollapsedRange();
      var block = topLevelTextBlock(range);
      if (!block || closestElement(range.startContainer, "pre,code,a,table")) return false;
      var rule = blockRuleForText(textBeforeCaret(block, range), false);
      return rule ? applyBlockInputRule(block, range, rule) : false;
    }

    function replaceInlineMarker(node, start, end, element) {
      recordSnapshot();
      var range = document.createRange();
      range.setStart(node, start);
      range.setEnd(node, end);
      range.deleteContents();
      range.insertNode(element);
      range.setStartAfter(element);
      range.collapse(true);
      var selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      savedBookmark = captureBookmark();
      markChanged(true);
      return true;
    }

    function safeLink(value) {
      value = String(value || "").trim();
      if (!value) return "";
      if (typeof options.isSafeLinkHref === "function") return options.isSafeLinkHref(value) ? value : "";
      return /^https?:\/\//i.test(value) ? value : "";
    }

    function tryInlineInputRule(event) {
      if (!event || event.inputType !== "insertText" || !/[\*_~`)]/.test(String(event.data || ""))) return false;
      var range = activeCollapsedRange();
      var node = range && range.startContainer;
      if (!node || node.nodeType !== Node.TEXT_NODE || closestElement(node, "pre,code,a,table")) return false;
      var end = range.startOffset;
      var before = String(node.nodeValue || "").slice(0, end);
      var match = null;
      var element = null;
      var markerLength = 0;
      if (event.data === ")") {
        match = /(^|[\s([{])\[([^\]\n]+)\]\(([^)\n]+)\)$/.exec(before);
        var href = match ? safeLink(match[3]) : "";
        if (match && href) {
          element = document.createElement("a");
          element.setAttribute("href", href);
          element.textContent = match[2];
          markerLength = match[2].length + match[3].length + 4;
        }
      } else if (event.data === "~") {
        match = /(^|[\s([{])~~(\S(?:[^\n]*?\S)?)~~$/.exec(before);
        if (match) {
          element = document.createElement("s");
          element.textContent = match[2];
          markerLength = match[2].length + 4;
        }
      } else if (event.data === "`") {
        match = /(^|[\s([{])(`+)(\S(?:[^`\n]*?\S)?)\2$/.exec(before);
        if (match) {
          element = document.createElement("code");
          element.textContent = match[3];
          markerLength = match[3].length + match[2].length * 2;
        }
      } else {
        match = /(^|[\s([{])(\*\*|__)(\S(?:[^\n]*?\S)?)\2$/.exec(before);
        if (match) {
          element = document.createElement("strong");
          element.textContent = match[3];
          markerLength = match[3].length + match[2].length * 2;
        } else {
          match = /(^|[\s([{])([*_])(\S(?:[^\n]*?\S)?)\2$/.exec(before);
          if (match) {
            element = document.createElement("em");
            element.textContent = match[3];
            markerLength = match[3].length + 2;
          }
        }
      }
      return element ? replaceInlineMarker(node, end - markerLength, end, element) : false;
    }

    function isEmptyElement(element) {
      return !normalizeInputText(element && element.textContent).trim();
    }

    function caretAtEdge(element, range, atEnd) {
      try {
        var edge = document.createRange();
        edge.selectNodeContents(element);
        if (atEnd) edge.setStart(range.startContainer, range.startOffset);
        else edge.setEnd(range.startContainer, range.startOffset);
        return normalizeInputText(edge.toString()).length === 0;
      } catch (_) {
        return false;
      }
    }

    function exitEmptyListItem(item) {
      var list = item && item.parentElement;
      if (!list || !/^(ul|ol)$/i.test(list.nodeName || "")) return false;
      recordSnapshot();
      var paragraph = document.createElement("p");
      ensureEditable(paragraph);
      var following = item.nextElementSibling;
      if (following) {
        var trailing = list.cloneNode(false);
        if (list.nodeName.toLowerCase() === "ol") {
          var first = list.hasAttribute("start") ? Number(list.getAttribute("start")) : 1;
          var index = Array.prototype.indexOf.call(list.children, item);
          trailing.setAttribute("start", String((Number.isFinite(first) ? first : 1) + index + 1));
        }
        while (following) {
          var next = following.nextElementSibling;
          trailing.appendChild(following);
          following = next;
        }
        list.parentNode.insertBefore(trailing, list.nextSibling);
      }
      list.parentNode.insertBefore(paragraph, list.nextSibling);
      item.remove();
      if (!list.children.length) list.remove();
      placeCaret(paragraph, false);
      markChanged(true);
      return true;
    }

    function exitCodeBlock(pre) {
      var target = pre && pre.nextElementSibling;
      if (!target && pre && pre.parentNode) {
        target = document.createElement("p");
        ensureEditable(target);
        pre.parentNode.insertBefore(target, pre.nextSibling);
      }
      if (!target) return false;
      placeCaret(target, false);
      return true;
    }

    function focusCell(cell, atEnd) {
      if (!cell) return;
      activeTable = tableAsset.tableFrom(cell);
      activeCell = cell;
      tableAnchorCell = cell;
      setTableSelection(activeTable, [cell], cell);
      placeCaret(cell, atEnd !== false);
      showTableToolbar();
    }

    function handleTableTab(event, cell) {
      var table = tableAsset.tableFrom(cell);
      var target = tableAsset.adjacentCell(table, cell, event.shiftKey ? -1 : 1);
      if (!target && !event.shiftKey) {
        recordSnapshot();
        target = tableAsset.insertRow(table, cell, true);
        if (target) markChanged(true);
      }
      if (!target) return false;
      event.preventDefault();
      event.stopPropagation();
      focusCell(target, false);
      return true;
    }

    function tryStructuralKey(event) {
      var range = activeCollapsedRange();
      if (!range || event.ctrlKey || event.altKey || event.metaKey) return false;
      var tableCell = tableAsset.cellFrom(range.startContainer);
      if (tableCell && event.key === "Tab") return handleTableTab(event, tableCell);
      var itemForTab = closestElement(range.startContainer, "li");
      if (itemForTab && body.contains(itemForTab) && event.key === "Tab") {
        event.preventDefault();
        recordSnapshot();
        document.execCommand(event.shiftKey ? "outdent" : "indent", false, null);
        markChanged(true);
        return true;
      }
      if (event.shiftKey) return false;
      if (event.key === "ArrowDown") {
        var terminalPre = closestElement(range.startContainer, "pre");
        if (terminalPre && body.contains(terminalPre) && caretAtEdge(terminalPre, range, true)) {
          event.preventDefault();
          event.stopPropagation();
          return exitCodeBlock(terminalPre);
        }
      } else if (event.key === "Enter") {
        var block = topLevelTextBlock(range);
        var rule = block && caretAtEdge(block, range, true) &&
          blockRuleForText(normalizeInputText(block.textContent), true);
        if (rule) {
          event.preventDefault();
          return applyBlockInputRule(block, range, rule);
        }
        var item = closestElement(range.startContainer, "li");
        if (item && body.contains(item) && isEmptyElement(item) && caretAtEdge(item, range, false)) {
          event.preventDefault();
          return exitEmptyListItem(item);
        }
        var pre = closestElement(range.startContainer, "pre");
        if (pre && body.contains(pre) && caretAtEdge(pre, range, true)) {
          var code = pre.querySelector("code") || pre;
          var source = String(code.textContent || "");
          if (/\n$/.test(source)) {
            event.preventDefault();
            recordSnapshot();
            code.textContent = source.slice(0, -1);
            ensureEditable(code);
            var paragraph = document.createElement("p");
            ensureEditable(paragraph);
            pre.parentNode.insertBefore(paragraph, pre.nextSibling);
            placeCaret(paragraph, false);
            markChanged(true);
            return true;
          }
        }
      } else if (event.key === "Backspace") {
        var emptyItem = closestElement(range.startContainer, "li");
        if (emptyItem && body.contains(emptyItem) && isEmptyElement(emptyItem) && caretAtEdge(emptyItem, range, false)) {
          event.preventDefault();
          return exitEmptyListItem(emptyItem);
        }
        var container = closestElement(range.startContainer, "h1,h2,h3,h4,h5,h6,blockquote,pre");
        if (container && body.contains(container) && isEmptyElement(container) && caretAtEdge(container, range, false)) {
          event.preventDefault();
          recordSnapshot();
          var plain = document.createElement("p");
          ensureEditable(plain);
          container.parentNode.replaceChild(plain, container);
          placeCaret(plain, false);
          markChanged(true);
          return true;
        }
      }
      return false;
    }

    function blockTagAtSelection() {
      var range = activeRange();
      if (!range) return "";
      var node = range.startContainer;
      if (node.nodeType === Node.TEXT_NODE) node = node.parentNode;
      while (node && node !== body) {
        var tag = node.nodeName ? node.nodeName.toLowerCase() : "";
        if (/^(p|div|h[1-6]|blockquote|pre|li)$/.test(tag)) return tag;
        node = node.parentNode;
      }
      return "";
    }

    function selectedAnchor() {
      var range = activeRange();
      if (!range) return null;
      var anchor = closestElement(range.commonAncestorContainer, "a") || closestElement(range.startContainer, "a");
      return anchor && body.contains(anchor) ? anchor : null;
    }

    function queryState(command) {
      try { return !!document.queryCommandState(command); } catch (_) { return false; }
    }

    function exec(command, value) {
      if (composing) return;
      savedBookmark = captureBookmark() || savedBookmark;
      recordSnapshot();
      restoreBookmark(savedBookmark);
      var range = activeRange();
      var semanticBold = range ? closestElement(range.commonAncestorContainer, "strong,b") : null;
      if (command === "bold" && range && !range.collapsed && !semanticBold &&
          range.startContainer === range.endContainer) {
        var strong = document.createElement("strong");
        strong.appendChild(range.extractContents());
        range.insertNode(strong);
        range.selectNodeContents(strong);
        var selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
      } else {
        document.execCommand(command, false, value == null ? null : value);
      }
      markChanged(true);
      updateContextUi();
    }

    function wrapInlineCode() {
      if (composing) return;
      savedBookmark = captureBookmark() || savedBookmark;
      recordSnapshot();
      restoreBookmark(savedBookmark);
      var range = activeRange();
      if (!range) return;
      var existing = closestElement(range.commonAncestorContainer, "code");
      if (existing && !closestElement(existing, "pre")) {
        var parent = existing.parentNode;
        while (existing.firstChild) parent.insertBefore(existing.firstChild, existing);
        parent.removeChild(existing);
        parent.normalize();
      } else {
        var code = document.createElement("code");
        if (range.collapsed) code.textContent = "code";
        else code.appendChild(range.extractContents());
        range.insertNode(code);
        range.selectNodeContents(code);
        var selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
      }
      markChanged(true);
      updateContextUi();
    }

    function slashContext() {
      if (composing) return null;
      var range = activeCollapsedRange();
      if (!range || range.startContainer.nodeType !== Node.TEXT_NODE ||
          closestElement(range.startContainer, "pre,code,a,table")) return null;
      var text = String(range.startContainer.nodeValue || "").slice(0, range.startOffset);
      var match = /(?:^|\s)\/([a-z0-9 _-]*)$/i.exec(text);
      if (!match) return null;
      var query = match[1] || "";
      var start = range.startOffset - query.length - 1;
      var marker = range.cloneRange();
      marker.setStart(range.startContainer, start);
      var rect = marker.getBoundingClientRect();
      if (!rect || (!rect.width && !rect.height)) rect = range.getBoundingClientRect();
      return { node: range.startContainer, start: start, end: range.startOffset, query: query, rect: rect };
    }

    function deleteSlashQuery() {
      var context = slashContext();
      if (!context) return null;
      var range = document.createRange();
      range.setStart(context.node, context.start);
      range.setEnd(context.node, context.end);
      range.deleteContents();
      range.collapse(true);
      var selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      savedBookmark = captureBookmark();
      return range;
    }

    function commandRule(id) {
      if (id === "paragraph") return { type: "paragraph" };
      if (/^h[1-6]$/.test(id)) return { type: "heading", level: Number(id.slice(1)) };
      if (id === "quote") return { type: "quote" };
      if (id === "bullet") return { type: "list", ordered: false, start: 1 };
      if (id === "numbered") return { type: "list", ordered: true, start: 1 };
      if (id === "codeblock") return { type: "code", language: "" };
      if (id === "divider") return { type: "rule" };
      return null;
    }

    function applySlashBlock(id) {
      var context = slashContext();
      if (!context) return;
      recordSnapshot();
      var range = deleteSlashQuery();
      var block = topLevelBlock(range);
      var rule = commandRule(id);
      if (!block || !rule || !replaceBlock(block, rule)) return;
      markChanged(true);
    }

    function insertTable(rowCount, columnCount) {
      var context = slashContext();
      if (!context) return;
      recordSnapshot();
      var range = deleteSlashQuery();
      var block = topLevelBlock(range);
      var table = tableAsset.create(rowCount, columnCount);
      var wrapper = document.createElement("div");
      wrapper.className = "table-scroll";
      wrapper.appendChild(table);
      var paragraph = document.createElement("p");
      ensureEditable(paragraph);
      if (block && !normalizeInputText(block.textContent).trim()) {
        block.parentNode.insertBefore(wrapper, block);
        block.parentNode.insertBefore(paragraph, block.nextSibling);
        block.remove();
      } else if (block && block.parentNode) {
        block.parentNode.insertBefore(wrapper, block.nextSibling);
        block.parentNode.insertBefore(paragraph, wrapper.nextSibling);
      } else {
        body.appendChild(wrapper);
        body.appendChild(paragraph);
      }
      prepareTables();
      markChanged(true);
      focusCell(table.querySelector("th,td"), false);
    }

    function openLink() {
      if (composing) return;
      var range = activeRange();
      var anchor = selectedAnchor();
      if (!range || (range.collapsed && !anchor)) return;
      savedBookmark = captureBookmark() || savedBookmark;
      var rect = anchor ? anchor.getBoundingClientRect() : range.getBoundingClientRect();
      ui.openLink({ href: anchor ? anchor.getAttribute("href") || "" : "", canRemove: !!anchor, rect: rect });
    }

    function applyLink(value) {
      var href = safeLink(value);
      if (!href) return "Enter a safe external HTTP(S) link.";
      restoreBookmark(savedBookmark);
      var range = activeRange();
      if (!range) return "The text selection is no longer available.";
      recordSnapshot();
      var anchor = selectedAnchor();
      if (anchor) {
        anchor.setAttribute("href", href);
        range.selectNodeContents(anchor);
        var anchorSelection = window.getSelection();
        anchorSelection.removeAllRanges();
        anchorSelection.addRange(range);
      } else {
        document.execCommand("createLink", false, href);
      }
      markChanged(true);
      return "";
    }

    function removeLink() {
      restoreBookmark(savedBookmark);
      var anchor = selectedAnchor();
      if (!anchor) return;
      recordSnapshot();
      var parent = anchor.parentNode;
      while (anchor.firstChild) parent.insertBefore(anchor.firstChild, anchor);
      parent.removeChild(anchor);
      parent.normalize();
      markChanged(true);
    }

    function showTableToolbar() {
      if (!activeTable || !activeCell || !body.contains(activeTable)) return;
      var size = tableAsset.dimensions(activeTable);
      ui.showTableToolbar(activeTable.getBoundingClientRect(), {
        canDeleteRow: tableAsset.rowIndexOf(activeTable, activeCell) > 0 && size.rows > 1,
        canDeleteColumn: size.columns > 1,
        alignment: tableAsset.alignment(activeCell)
      });
    }

    function deleteActiveTable() {
      if (!activeTable || !body.contains(activeTable)) return;
      var target = activeTable.parentElement && activeTable.parentElement.classList.contains("table-scroll")
        ? activeTable.parentElement : activeTable;
      var paragraph = document.createElement("p");
      ensureEditable(paragraph);
      target.parentNode.insertBefore(paragraph, target.nextSibling);
      target.remove();
      clearTableSelection();
      activeTable = null;
      activeCell = null;
      tableAnchorCell = null;
      placeCaret(paragraph, false);
    }

    function tableAction(action) {
      if (!activeTable || !activeCell || !body.contains(activeTable)) return;
      recordSnapshot();
      var next = activeCell;
      if (action === "rowAbove") next = tableAsset.insertRow(activeTable, activeCell, false) || activeCell;
      else if (action === "rowBelow") next = tableAsset.insertRow(activeTable, activeCell, true) || activeCell;
      else if (action === "columnLeft") next = tableAsset.insertColumn(activeTable, activeCell, false) || activeCell;
      else if (action === "columnRight") next = tableAsset.insertColumn(activeTable, activeCell, true) || activeCell;
      else if (action === "deleteRow") next = tableAsset.deleteRow(activeTable, activeCell) || activeCell;
      else if (action === "deleteColumn") next = tableAsset.deleteColumn(activeTable, activeCell) || activeCell;
      else if (action === "alignLeft") tableAsset.setAlignment(activeTable, activeCell, "left");
      else if (action === "alignCenter") tableAsset.setAlignment(activeTable, activeCell, "center");
      else if (action === "alignRight") tableAsset.setAlignment(activeTable, activeCell, "right");
      else if (action === "deleteTable") {
        deleteActiveTable();
        markChanged(true);
        return;
      }
      prepareTables();
      markChanged(true);
      if (next && body.contains(next)) focusCell(next, false);
    }

    function commandActive(id) {
      if (id === "bold") return queryState("bold");
      if (id === "italic") return queryState("italic");
      if (id === "strike") return queryState("strikeThrough");
      if (id === "inlinecode") {
        var range = activeRange();
        return !!(range && closestElement(range.commonAncestorContainer, "code") &&
          !closestElement(range.commonAncestorContainer, "pre"));
      }
      if (id === "link") return !!selectedAnchor();
      var tag = blockTagAtSelection();
      if (id === "paragraph") return tag === "p" || tag === "div";
      if (/^h[1-6]$/.test(id)) return tag === id;
      if (id === "quote") return tag === "blockquote";
      return false;
    }

    var commandDefinitions = [
      ["paragraph", "Basic", "Text", ["paragraph", "plain"], ["text"], ["slash", "selection"]],
      ["h1", "Headings", "Heading 1", ["h1", "title"], ["heading"], ["slash", "selection"]],
      ["h2", "Headings", "Heading 2", ["h2", "subtitle"], ["heading"], ["slash", "selection"]],
      ["h3", "Headings", "Heading 3", ["h3", "subheading"], ["heading"], ["slash", "selection"]],
      ["h4", "Headings", "Heading 4", ["h4"], ["heading"], ["slash"]],
      ["h5", "Headings", "Heading 5", ["h5"], ["heading"], ["slash"]],
      ["h6", "Headings", "Heading 6", ["h6"], ["heading"], ["slash"]],
      ["bullet", "Lists", "Bulleted list", ["bullet", "unordered", "ul"], ["list"], ["slash"]],
      ["numbered", "Lists", "Numbered list", ["number", "ordered", "ol"], ["list"], ["slash"]],
      ["quote", "Blocks", "Quote", ["blockquote"], ["quote"], ["slash", "selection"]],
      ["codeblock", "Blocks", "Code block", ["code", "fence", "pre"], ["code"], ["slash"]],
      ["divider", "Blocks", "Divider", ["line", "rule", "hr"], ["separator"], ["slash"]],
      ["table", "Content", "Table", ["grid", "rows", "columns"], ["table"], ["slash"]],
      ["bold", "Formatting", "Bold", ["strong"], ["format"], ["selection"]],
      ["italic", "Formatting", "Italic", ["emphasis"], ["format"], ["selection"]],
      ["strike", "Formatting", "Strike", ["strikethrough"], ["format"], ["selection"]],
      ["inlinecode", "Formatting", "Inline code", ["code"], ["format"], ["selection"]],
      ["link", "Formatting", "Link", ["url", "hyperlink"], ["link"], ["selection"]]
    ].map(function (definition) {
      var id = definition[0];
      return {
        id: id,
        group: definition[1],
        label: definition[2],
        shortLabel: id === "bold" ? "B" : id === "italic" ? "I" : id === "strike" ? "S" :
          id === "inlinecode" ? "`" : id === "link" ? "🔗" : id === "paragraph" ? "T" : id.toUpperCase(),
        aliases: definition[3],
        keywords: definition[4],
        contexts: definition[5],
        picker: id === "table" ? "table" : "",
        isActive: function () { return commandActive(id); }
      };
    });

    function executeCommand(id, context) {
      if (id === "link") { openLink(); return; }
      if (context === "slash") { applySlashBlock(id); return; }
      if (id === "bold") exec("bold");
      else if (id === "italic") exec("italic");
      else if (id === "strike") exec("strikeThrough");
      else if (id === "inlinecode") wrapInlineCode();
      else {
        var rule = commandRule(id);
        if (rule && rule.type === "paragraph") exec("formatBlock", "P");
        else if (rule && rule.type === "heading") exec("formatBlock", "H" + rule.level);
        else if (rule && rule.type === "quote") exec("formatBlock", "BLOCKQUOTE");
      }
    }

    var ui = uiAsset.create({
      host: options.bodyHost,
      commands: commandDefinitions,
      executeCommand: executeCommand,
      insertTable: insertTable,
      tableAction: tableAction,
      applyLink: applyLink,
      removeLink: removeLink,
      restoreSelection: function () { restoreBookmark(savedBookmark); },
      slashContext: slashContext
    });

    function rangeRect(range) {
      if (!range) return null;
      var rect = range.getBoundingClientRect();
      if (rect && (rect.width || rect.height)) return rect;
      var rects = range.getClientRects();
      return rects.length ? rects[0] : rect;
    }

    function updateContextUi() {
      if (destroyed || composing || !body.isConnected) return;
      if (ui.slashOpen()) {
        var slash = slashContext();
        if (slash) ui.showSlash(slash);
        else ui.closeSlash();
        return;
      }
      var range = activeRange();
      if (!range || (document.activeElement !== body && !body.contains(document.activeElement))) return;
      var cell = tableAsset.cellFrom(range.startContainer);
      if (cell && body.contains(cell)) {
        activeTable = tableAsset.tableFrom(cell);
        activeCell = cell;
        if (!tableAnchorCell) tableAnchorCell = cell;
        if (!selectedTableCells.length || selectedTableCells.indexOf(cell) === -1) {
          setTableSelection(activeTable, [cell], cell);
        }
        ui.closeSelectionToolbar();
        showTableToolbar();
        return;
      }
      ui.closeTableToolbar();
      if (!range.collapsed) {
        savedBookmark = captureBookmark() || savedBookmark;
        ui.showSelectionToolbar(rangeRect(range));
      } else {
        ui.closeSelectionToolbar();
      }
    }

    function updateSlash() {
      var context = slashContext();
      if (context) ui.showSlash(context);
      else ui.closeSlash();
    }

    function setActions(config) {
      actionConfig = config || null;
      if (!actionConfig) {
        ui.setActions(null);
        return;
      }
      ui.setActions({
        save: actionConfig.save,
        cancel: actionConfig.cancel,
        restore: actionConfig.restore,
        showCapsule: actionConfig.showCapsule !== false
      });
      refreshActions();
    }

    function insertSanitizedPaste(event) {
      if (!event.clipboardData || composing) return false;
      var html = event.clipboardData.getData("text/html");
      if (!html) return false;
      event.preventDefault();
      var text = event.clipboardData.getData("text/plain");
      var safe = typeof options.sanitizeHtml === "function" ? options.sanitizeHtml(html) : "";
      var holder = document.createElement("div");
      holder.innerHTML = safe;
      var insertHtml = safe && (typeof options.canSerialize !== "function" || options.canSerialize(holder));
      recordSnapshot();
      document.execCommand(insertHtml ? "insertHTML" : "insertText", false, insertHtml ? safe : text);
      prepareTables();
      markChanged(true);
      return true;
    }

    prepareTables();
    undoStack.push(makeSnapshot());

    listen(body, "input", function (event) {
      if (applyingSnapshot || composing || (event && event.isComposing)) return;
      if (tryBlockInputRule(event) || tryInlineInputRule(event)) return;
      setDirty(true);
      prepareTables();
      savedBookmark = captureBookmark() || savedBookmark;
      updateSlash();
      var immediate = event && (event.inputType === "insertFromPaste" ||
        event.inputType === "insertFromDrop" || event.inputType === "deleteByCut");
      if (immediate) recordSnapshot();
      else scheduleSnapshot();
    });
    listen(body, "paste", insertSanitizedPaste);
    listen(body, "compositionstart", function () {
      recordSnapshot();
      clearSnapshotTimer();
      composing = true;
      ui.closeSlash();
      ui.closeSelectionToolbar();
      notifyState();
    });
    listen(body, "compositionend", function () {
      composing = false;
      setDirty(true);
      savedBookmark = captureBookmark() || savedBookmark;
      recordSnapshot();
      updateContextUi();
    });
    listen(body, "keydown", function (event) {
      if (event.isComposing || composing) return;
      if (ui.handleSlashKeydown(event)) return;
      if (event.key === "Escape" && ui.handleEscape()) {
        event.preventDefault();
        event.stopPropagation();
        return;
      }
      if (tryStructuralKey(event) || !event.ctrlKey || event.altKey || event.metaKey) return;
      var key = String(event.key || "").toLowerCase();
      if (key === "b") { event.preventDefault(); exec("bold"); }
      else if (key === "i") { event.preventDefault(); exec("italic"); }
      else if (key === "x" && event.shiftKey) { event.preventDefault(); exec("strikeThrough"); }
      else if (key === "k") { event.preventDefault(); openLink(); }
      else if (key === "z" && event.shiftKey) { event.preventDefault(); redo(); }
      else if (key === "z") { event.preventDefault(); undo(); }
      else if (key === "y") { event.preventDefault(); redo(); }
    });
    listen(body, "mouseup", rememberSelection);
    listen(body, "keyup", rememberSelection);
    listen(body, "click", function (event) {
      var last = body.lastElementChild;
      if (event.target !== body || !last || last.nodeName.toLowerCase() !== "pre" ||
          event.clientY < last.getBoundingClientRect().bottom) return;
      event.preventDefault();
      exitCodeBlock(last);
    });
    listen(body, "focusin", function (event) {
      var cell = tableAsset.cellFrom(event.target);
      if (cell && body.contains(cell)) {
        activeTable = tableAsset.tableFrom(cell);
        activeCell = cell;
        tableAnchorCell = cell;
        setTableSelection(activeTable, [cell], cell);
        showTableToolbar();
      }
    });
    listen(body, "pointerdown", function (event) {
      var cell = tableAsset.cellFrom(event.target);
      if (!cell || !body.contains(cell) || event.button !== 0) return;
      activeTable = tableAsset.tableFrom(cell);
      activeCell = cell;
      tableAnchorCell = cell;
      tableDragging = true;
      setTableSelection(activeTable, [cell], cell);
    });
    listen(body, "pointerover", function (event) {
      if (!tableDragging || !activeTable || !tableAnchorCell) return;
      var cell = tableAsset.cellFrom(event.target);
      if (!cell || tableAsset.tableFrom(cell) !== activeTable) return;
      setTableSelection(activeTable, tableAsset.rangeCells(activeTable, tableAnchorCell, cell), cell);
    });
    listen(document, "pointerup", function () { tableDragging = false; }, true);
    listen(document, "selectionchange", function () {
      if (document.activeElement === body || body.contains(document.activeElement)) rememberSelection();
    });

    return {
      body: body,
      focus: function () {
        body.focus();
        if (!restoreBookmark(savedBookmark)) {
          var range = document.createRange();
          range.selectNodeContents(body);
          range.collapse(false);
          var selection = window.getSelection();
          selection.removeAllRanges();
          selection.addRange(range);
          rememberSelection();
        }
      },
      readContent: currentMarkdown,
      canSave: function () { return !composing && canSerialize(); },
      canSerialize: canSerialize,
      isDirty: function () { return dirty; },
      isComposing: function () { return composing; },
      undo: undo,
      redo: redo,
      setActions: setActions,
      refreshActions: refreshActions,
      cleanup: function () {
        if (destroyed) return;
        destroyed = true;
        clearSnapshotTimer();
        ui.destroy();
        while (listeners.length) listeners.pop()();
        clearTableSelection();
        undoStack = [];
        redoStack = [];
      }
    };
  }

  window.ZenCropPreviewRichEditor = { createEditor: createEditor };
}());
