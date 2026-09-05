(function () {
  "use strict";

  function createUi(options) {
    if (!options || !options.host || typeof options.executeCommand !== "function" ||
        typeof options.insertTable !== "function") {
      throw new Error("Rich editor UI dependencies are unavailable.");
    }

    var host = options.host;
    var commands = options.commands || [];
    var listeners = [];
    var slash = null;
    var slashQuery = "";
    var slashItems = [];
    var slashIndex = 0;
    var picker = null;
    var pickerRows = 3;
    var pickerColumns = 3;
    var pickerDragging = false;
    var selectionToolbar = null;
    var tableToolbar = null;
    var linkPopover = null;
    var linkInput = null;
    var actions = null;
    var capsule = null;
    var actionState = { dirty: false, composing: false, canSave: true, pending: false, canRestore: false };

    function listen(target, type, handler, capture) {
      target.addEventListener(type, handler, !!capture);
      if (target === window || target === document || target === host) {
        listeners.push(function () { target.removeEventListener(type, handler, !!capture); });
      }
    }

    function button(label, title, handler, className) {
      var node = document.createElement("button");
      node.type = "button";
      node.className = className || "ocr-preview-editor-button is-compact";
      node.textContent = label;
      node.title = title || label;
      node.setAttribute("aria-label", title || label);
      listen(node, "mousedown", function (event) { event.preventDefault(); });
      listen(node, "click", function (event) {
        event.preventDefault();
        handler();
      });
      return node;
    }

    function clampFloating(node, rect, below) {
      if (!node || !rect) return;
      node.style.left = "0px";
      node.style.top = "0px";
      node.classList.remove("opens-up");
      var margin = 8;
      var gap = 6;
      var bounds = node.getBoundingClientRect();
      var roomBelow = window.innerHeight - rect.bottom - margin;
      var roomAbove = rect.top - margin;
      var opensUp = below === false || (below !== true && roomBelow < bounds.height && roomAbove > roomBelow);
      var top = opensUp ? rect.top - bounds.height - gap : rect.bottom + gap;
      var left = rect.left + (rect.width - bounds.width) / 2;
      left = Math.max(margin, Math.min(left, Math.max(margin, window.innerWidth - bounds.width - margin)));
      top = Math.max(margin, Math.min(top, Math.max(margin, window.innerHeight - bounds.height - margin)));
      node.classList.toggle("opens-up", opensUp);
      node.style.left = Math.round(left) + "px";
      node.style.top = Math.round(top) + "px";
    }

    function normalizeWords(value) {
      return String(value || "").trim().toLowerCase().split(/\s+/).filter(Boolean);
    }

    function commandScore(command, words) {
      if (!words.length) return 3;
      var label = String(command.label || "").toLowerCase();
      var aliases = (command.aliases || []).map(function (value) { return String(value).toLowerCase(); });
      var keywords = (command.keywords || []).map(function (value) { return String(value).toLowerCase(); });
      var values = [label].concat(aliases, keywords);
      var score = 3;
      for (var i = 0; i < words.length; i++) {
        var word = words[i];
        var best = 99;
        values.forEach(function (value) {
          if (value === word || value.indexOf(word) === 0) best = Math.min(best, value === label ? 0 : 1);
          else if (value.indexOf(word) !== -1) best = Math.min(best, 2);
        });
        if (best === 99) return -1;
        score = Math.min(score, best);
      }
      return score;
    }

    function closePicker() {
      if (picker && picker.parentNode) picker.parentNode.removeChild(picker);
      picker = null;
      pickerDragging = false;
    }

    function closeSlash() {
      closePicker();
      if (slash && slash.parentNode) slash.parentNode.removeChild(slash);
      slash = null;
      slashQuery = "";
      slashItems = [];
      slashIndex = 0;
    }

    function selectSlash(index) {
      if (!slashItems.length) return;
      slashIndex = (index + slashItems.length) % slashItems.length;
      slashItems.forEach(function (item, itemIndex) {
        item.node.classList.toggle("is-selected", itemIndex === slashIndex);
        item.node.setAttribute("aria-selected", itemIndex === slashIndex ? "true" : "false");
      });
      slashItems[slashIndex].node.scrollIntoView({ block: "nearest" });
    }

    function pickerCell(rows, columns) {
      var cell = document.createElement("button");
      cell.type = "button";
      cell.className = "ocr-preview-table-picker-cell";
      cell.setAttribute("aria-label", rows + " rows by " + columns + " columns");
      cell.dataset.rows = String(rows);
      cell.dataset.columns = String(columns);
      listen(cell, "pointerdown", function (event) {
        event.preventDefault();
        pickerDragging = true;
        pickerRows = rows;
        pickerColumns = columns;
        refreshPicker();
      });
      listen(cell, "pointerenter", function () {
        if (!pickerDragging) return;
        pickerRows = rows;
        pickerColumns = columns;
        refreshPicker();
      });
      listen(cell, "mouseenter", function () {
        if (pickerDragging) return;
        pickerRows = rows;
        pickerColumns = columns;
        refreshPicker();
      });
      return cell;
    }

    function refreshPicker() {
      if (!picker) return;
      picker.querySelector(".ocr-preview-table-picker-label").textContent =
        pickerRows + " × " + pickerColumns;
      Array.prototype.forEach.call(picker.querySelectorAll(".ocr-preview-table-picker-cell"), function (cell) {
        var rows = Number(cell.dataset.rows);
        var columns = Number(cell.dataset.columns);
        cell.classList.toggle("is-selected", rows <= pickerRows && columns <= pickerColumns);
        cell.tabIndex = rows === pickerRows && columns === pickerColumns ? 0 : -1;
      });
    }

    function finishPicker() {
      var rows = pickerRows;
      var columns = pickerColumns;
      closeSlash();
      options.insertTable(rows, columns);
    }

    function openTablePicker() {
      if (!slash) return;
      closePicker();
      pickerRows = 3;
      pickerColumns = 3;
      picker = document.createElement("div");
      picker.className = "ocr-preview-table-picker";
      picker.setAttribute("role", "dialog");
      picker.setAttribute("aria-label", "Choose table size");
      var header = document.createElement("div");
      header.className = "ocr-preview-table-picker-header";
      var back = button("‹", "Back to commands", function () {
        closePicker();
        if (slashItems.length) selectSlash(slashIndex);
      });
      var label = document.createElement("span");
      label.className = "ocr-preview-table-picker-label";
      header.appendChild(back);
      header.appendChild(label);
      picker.appendChild(header);
      var grid = document.createElement("div");
      grid.className = "ocr-preview-table-picker-grid";
      grid.setAttribute("role", "grid");
      for (var row = 1; row <= 20; row++) {
        for (var column = 1; column <= 20; column++) grid.appendChild(pickerCell(row, column));
      }
      picker.appendChild(grid);
      slash.appendChild(picker);
      refreshPicker();
    }

    function activateSlashItem() {
      if (!slashItems.length) return true;
      var command = slashItems[slashIndex].command;
      if (command.picker === "table") openTablePicker();
      else {
        closeSlash();
        options.executeCommand(command.id, "slash");
      }
      return true;
    }

    function showSlash(context) {
      var query = String(context.query || "");
      if (slash && slashQuery === query) {
        clampFloating(slash, context.rect, true);
        return;
      }
      closeSlash();
      closeSelectionToolbar();
      closeTableToolbar();
      closeLink(false);
      slash = document.createElement("div");
      slashQuery = query;
      slash.className = "ocr-preview-slash-menu";
      slash.setAttribute("role", "listbox");
      slash.setAttribute("aria-label", "Insert Markdown block");
      var words = normalizeWords(query);
      var matches = commands.filter(function (command) {
        return (command.contexts || []).indexOf("slash") !== -1 &&
          (!command.isEnabled || command.isEnabled()) && commandScore(command, words) >= 0;
      }).map(function (command, index) {
        return { command: command, index: index, score: commandScore(command, words) };
      }).sort(function (left, right) {
        return left.score - right.score || left.index - right.index;
      });
      var groups = [];
      matches.forEach(function (match) {
        var group = match.command.group || "Other";
        if (groups.indexOf(group) === -1) groups.push(group);
      });
      groups.forEach(function (group) {
        var heading = document.createElement("div");
        heading.className = "ocr-preview-slash-group";
        heading.textContent = group;
        slash.appendChild(heading);
        matches.filter(function (match) { return (match.command.group || "Other") === group; })
          .forEach(function (match) {
            var item = document.createElement("button");
            item.type = "button";
            item.className = "ocr-preview-slash-item";
            item.setAttribute("role", "option");
            item.setAttribute("aria-selected", "false");
            item.textContent = match.command.label;
            listen(item, "mousedown", function (event) { event.preventDefault(); });
            listen(item, "mouseenter", function () {
              var found = slashItems.filter(function (entry) { return entry.node === item; })[0];
              if (found) selectSlash(slashItems.indexOf(found));
            });
            listen(item, "click", function (event) {
              event.preventDefault();
              var found = slashItems.filter(function (entry) { return entry.node === item; })[0];
              if (found) slashIndex = slashItems.indexOf(found);
              activateSlashItem();
            });
            slash.appendChild(item);
            slashItems.push({ command: match.command, node: item });
          });
      });
      if (!slashItems.length) {
        var empty = document.createElement("div");
        empty.className = "ocr-preview-slash-empty";
        empty.textContent = "No matching command";
        slash.appendChild(empty);
      }
      host.appendChild(slash);
      selectSlash(0);
      clampFloating(slash, context.rect, true);
    }

    function handleSlashKeydown(event) {
      if (picker) {
        if (event.key === "Escape") {
          event.preventDefault();
          event.stopPropagation();
          closePicker();
          return true;
        }
        if (event.key === "ArrowUp" || event.key === "ArrowDown" ||
            event.key === "ArrowLeft" || event.key === "ArrowRight") {
          event.preventDefault();
          event.stopPropagation();
          if (event.key === "ArrowUp") pickerRows = Math.max(1, pickerRows - 1);
          if (event.key === "ArrowDown") pickerRows = Math.min(20, pickerRows + 1);
          if (event.key === "ArrowLeft") pickerColumns = Math.max(1, pickerColumns - 1);
          if (event.key === "ArrowRight") pickerColumns = Math.min(20, pickerColumns + 1);
          refreshPicker();
          return true;
        }
        if (event.key === "Enter" || event.key === "Tab") {
          event.preventDefault();
          event.stopPropagation();
          finishPicker();
          return true;
        }
        return false;
      }
      if (!slash) return false;
      if (event.key === "ArrowUp" || event.key === "ArrowDown") {
        event.preventDefault();
        event.stopPropagation();
        selectSlash(slashIndex + (event.key === "ArrowDown" ? 1 : -1));
        return true;
      }
      if (event.key === "Enter" || event.key === "Tab") {
        event.preventDefault();
        event.stopPropagation();
        return activateSlashItem();
      }
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        closeSlash();
        return true;
      }
      return false;
    }

    function commandToolbar(className, ariaLabel, commandList, context, rect) {
      var toolbar = document.createElement("div");
      toolbar.className = className;
      toolbar.setAttribute("role", "toolbar");
      toolbar.setAttribute("aria-label", ariaLabel);
      commandList.forEach(function (command) {
        var item = button(command.shortLabel || command.label, command.title || command.label, function () {
          options.executeCommand(command.id, context);
        });
        var active = command.isActive && command.isActive();
        item.classList.toggle("is-active", !!active);
        item.setAttribute("aria-pressed", active ? "true" : "false");
        toolbar.appendChild(item);
      });
      host.appendChild(toolbar);
      clampFloating(toolbar, rect, false);
      return toolbar;
    }

    function closeSelectionToolbar() {
      if (selectionToolbar && selectionToolbar.parentNode) selectionToolbar.parentNode.removeChild(selectionToolbar);
      selectionToolbar = null;
    }

    function showSelectionToolbar(rect) {
      closeSelectionToolbar();
      closeSlash();
      closeTableToolbar();
      var selected = commands.filter(function (command) {
        return (command.contexts || []).indexOf("selection") !== -1 &&
          (!command.isEnabled || command.isEnabled());
      });
      selectionToolbar = commandToolbar(
        "ocr-preview-context-toolbar ocr-preview-selection-toolbar",
        "Text formatting", selected, "selection", rect);
    }

    function closeTableToolbar() {
      if (tableToolbar && tableToolbar.parentNode) tableToolbar.parentNode.removeChild(tableToolbar);
      tableToolbar = null;
    }

    function showTableToolbar(rect, state) {
      closeTableToolbar();
      closeSlash();
      closeSelectionToolbar();
      var toolbar = document.createElement("div");
      toolbar.className = "ocr-preview-context-toolbar ocr-preview-table-toolbar";
      toolbar.setAttribute("role", "toolbar");
      toolbar.setAttribute("aria-label", "Table editing");
      [
        ["↑+", "Insert row above", "rowAbove"],
        ["↓+", "Insert row below", "rowBelow"],
        ["←+", "Insert column left", "columnLeft"],
        ["→+", "Insert column right", "columnRight"],
        ["−R", "Delete row", "deleteRow"],
        ["−C", "Delete column", "deleteColumn"],
        ["L", "Align column left", "alignLeft"],
        ["C", "Align column center", "alignCenter"],
        ["R", "Align column right", "alignRight"],
        ["×", "Delete table", "deleteTable"]
      ].forEach(function (item) {
        var control = button(item[0], item[1], function () { options.tableAction(item[2]); });
        if (item[2] === "deleteRow") control.disabled = !state.canDeleteRow;
        if (item[2] === "deleteColumn") control.disabled = !state.canDeleteColumn;
        if (item[2] === "alignLeft" || item[2] === "alignCenter" || item[2] === "alignRight") {
          var align = item[2].slice(5).toLowerCase();
          control.classList.toggle("is-active", state.alignment === align);
          control.setAttribute("aria-pressed", state.alignment === align ? "true" : "false");
        }
        toolbar.appendChild(control);
      });
      host.appendChild(toolbar);
      tableToolbar = toolbar;
      clampFloating(toolbar, rect, false);
    }

    function closeLink(restoreSelection) {
      if (!linkPopover) return false;
      if (linkPopover.parentNode) linkPopover.parentNode.removeChild(linkPopover);
      linkPopover = null;
      linkInput = null;
      if (restoreSelection && typeof options.restoreSelection === "function") options.restoreSelection();
      return true;
    }

    function openLink(context) {
      closeSlash();
      closeSelectionToolbar();
      closeTableToolbar();
      closeLink(false);
      linkPopover = document.createElement("div");
      linkPopover.className = "ocr-preview-link-popover";
      linkPopover.setAttribute("role", "dialog");
      linkPopover.setAttribute("aria-label", "Edit link");
      linkInput = document.createElement("input");
      linkInput.type = "url";
      linkInput.inputMode = "url";
      linkInput.autocomplete = "off";
      linkInput.className = "ocr-preview-editor-input";
      linkInput.placeholder = "https://";
      linkInput.value = context.href || "";
      linkInput.setAttribute("aria-label", "Link destination");
      linkPopover.appendChild(linkInput);
      var row = document.createElement("div");
      row.className = "ocr-preview-editor-link-actions";
      var linkRemove = button("Remove", "Remove link", function () {
        options.removeLink();
        closeLink(false);
      });
      linkRemove.hidden = !context.canRemove;
      row.appendChild(linkRemove);
      var cancel = button("Cancel", "Close link editor", function () { closeLink(true); });
      row.appendChild(cancel);
      var apply = button(context.canRemove ? "Update" : "Apply", "Apply link", function () {
        var error = options.applyLink(linkInput.value);
        if (error) {
          linkInput.setCustomValidity(error);
          linkInput.reportValidity();
          linkInput.focus();
          return;
        }
        closeLink(false);
      });
      apply.classList.add("is-primary");
      row.appendChild(apply);
      linkPopover.appendChild(row);
      listen(linkInput, "input", function () { linkInput.setCustomValidity(""); });
      listen(linkInput, "keydown", function (event) {
        if (event.key === "Enter") {
          event.preventDefault();
          event.stopPropagation();
          apply.click();
        } else if (event.key === "Escape") {
          event.preventDefault();
          event.stopPropagation();
          closeLink(true);
        }
      });
      host.appendChild(linkPopover);
      clampFloating(linkPopover, context.rect, true);
      window.setTimeout(function () {
        if (!linkInput) return;
        linkInput.focus();
        linkInput.select();
      }, 0);
    }

    function refreshActions(next) {
      if (next) {
        Object.keys(next).forEach(function (key) { actionState[key] = next[key]; });
      }
      if (!capsule || !actions) return;
      actions.save.disabled = actionState.pending || actionState.composing ||
        !actionState.dirty || !actionState.canSave;
      actions.restore.hidden = !actionState.canRestore;
      actions.restore.disabled = actionState.pending;
      actions.cancel.disabled = actionState.pending;
    }

    function setActions(config) {
      if (capsule && capsule.parentNode) capsule.parentNode.removeChild(capsule);
      capsule = null;
      actions = null;
      if (!config || config.showCapsule === false) return;
      capsule = document.createElement("div");
      capsule.className = "ocr-preview-transaction-capsule";
      var status = document.createElement("div");
      status.className = "ocr-preview-editor-status";
      status.setAttribute("role", "status");
      status.setAttribute("aria-live", "polite");
      capsule.appendChild(status);
      var restore = button("Restore OCR", "Restore original OCR", config.restore || function () {});
      restore.classList.add("is-restore");
      capsule.appendChild(restore);
      var cancel = button("Cancel", "Cancel editing (Esc)", config.cancel);
      capsule.appendChild(cancel);
      var save = button("Save", "Save (Ctrl+S)", config.save);
      save.classList.add("is-primary");
      capsule.appendChild(save);
      actions = { save: save, cancel: cancel, restore: restore };
      host.appendChild(capsule);
      refreshActions();
    }

    function handleEscape() {
      if (picker) { closePicker(); return true; }
      if (closeLink(true)) return true;
      if (slash) { closeSlash(); return true; }
      if (selectionToolbar) { closeSelectionToolbar(); return true; }
      if (tableToolbar) { closeTableToolbar(); return true; }
      return false;
    }

    listen(window, "resize", function () {
      if (slash && options.slashContext) {
        var context = options.slashContext();
        if (context) clampFloating(slash, context.rect, true);
      }
    });
    listen(document, "pointerup", function () {
      if (!pickerDragging) return;
      pickerDragging = false;
      finishPicker();
    }, true);
    listen(document, "mousedown", function (event) {
      if (linkPopover && !linkPopover.contains(event.target) &&
          (!selectionToolbar || !selectionToolbar.contains(event.target))) closeLink(false);
    }, true);

    return {
      showSlash: showSlash,
      closeSlash: closeSlash,
      slashOpen: function () { return !!slash; },
      handleSlashKeydown: handleSlashKeydown,
      showSelectionToolbar: showSelectionToolbar,
      closeSelectionToolbar: closeSelectionToolbar,
      showTableToolbar: showTableToolbar,
      closeTableToolbar: closeTableToolbar,
      openLink: openLink,
      closeLink: closeLink,
      setActions: setActions,
      refreshActions: refreshActions,
      handleEscape: handleEscape,
      destroy: function () {
        closeSlash();
        closeSelectionToolbar();
        closeTableToolbar();
        closeLink(false);
        if (capsule && capsule.parentNode) capsule.parentNode.removeChild(capsule);
        while (listeners.length) listeners.pop()();
      }
    };
  }

  window.ZenCropPreviewRichEditorUi = { create: createUi };
}());
