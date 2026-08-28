(function () {
  "use strict";

  function createFormulaEditor(options) {
    if (!options || !options.katex || typeof options.addActions !== "function" ||
        typeof options.createStatus !== "function" || typeof options.setStatus !== "function") {
      throw new Error("Preview formula editor dependencies are unavailable.");
    }

    function parse(content, block, element) {
      var raw = String(content || "");
      var trimmed = raw.trim();
      var result = { source: trimmed, prefix: /^\s*/.exec(raw)[0], suffix: /\s*$/.exec(raw)[0], opener: "", closer: "",
        display: /display/.test(block.labelKey || "") || !!(element && element.querySelector && element.querySelector(".math-display")) };
      [{ open: "$$", close: "$$", display: true }, { open: "\\[", close: "\\]", display: true },
        { open: "\\(", close: "\\)", display: false }, { open: "$", close: "$", display: false }].some(function (wrapper) {
        if (trimmed.length < wrapper.open.length + wrapper.close.length || trimmed.slice(0, wrapper.open.length) !== wrapper.open ||
            trimmed.slice(-wrapper.close.length) !== wrapper.close) return false;
        result.source = trimmed.slice(wrapper.open.length, -wrapper.close.length).trim();
        result.opener = wrapper.open;
        result.closer = wrapper.close;
        result.display = wrapper.display;
        return true;
      });
      return result;
    }

    return function build(toolbar, bodyHost, block, element) {
      var original = block.visibleSourceContent || block.content || element.textContent || "";
      var parsed = parse(original, block, element);
      var valid = false;
      var dirty = false;
      var timer = 0;
      var saveControl = null;
      var source = document.createElement("textarea");
      source.className = "ocr-preview-editor-textarea ocr-preview-formula-source";
      source.setAttribute("spellcheck", "false");
      source.setAttribute("aria-label", "LaTeX source");
      source.value = parsed.source;
      var preview = document.createElement("div");
      preview.className = "ocr-preview-formula-preview";
      var status = options.createStatus();

      function content() {
        if (!dirty) return original;
        var latex = source.value.trim();
        return !parsed.opener && !parsed.closer ? parsed.prefix + latex + parsed.suffix :
          parsed.prefix + parsed.opener + " " + latex + " " + parsed.closer + parsed.suffix;
      }
      function render() {
        timer = 0;
        preview.innerHTML = "";
        var latex = source.value.trim();
        if (!latex) {
          valid = false;
          options.setStatus(status, "LaTeX source is empty.", true);
        } else {
          try {
            options.katex.render(latex, preview, { displayMode: parsed.display, throwOnError: true, trust: false, maxSize: 10, maxExpand: 1000 });
            valid = true;
            options.setStatus(status, "Preview ready.", false);
          } catch (error) {
            valid = false;
            options.setStatus(status, error && error.message ? error.message : "Invalid LaTeX.", true);
          }
        }
        if (saveControl) saveControl.refresh();
      }
      function validate() {
        if (timer) window.clearTimeout(timer);
        render();
        return valid;
      }
      source.addEventListener("input", function () {
        if (timer) window.clearTimeout(timer);
        dirty = true;
        valid = false;
        if (saveControl) saveControl.refresh();
        timer = window.setTimeout(render, 220);
      });
      bodyHost.classList.add("ocr-preview-formula-editor");
      bodyHost.appendChild(source);
      bodyHost.appendChild(preview);
      bodyHost.appendChild(status);
      saveControl = options.addActions(toolbar, block, content, function () { return valid; }, validate);
      render();
      return { actionsAdded: true, saveControl: saveControl, cleanup: function () { if (timer) window.clearTimeout(timer); },
        focus: function () { source.focus(); source.select(); }, readContent: content, canSave: function () { return valid; }, validateBeforeSave: validate };
    };
  }

  window.ZenCropPreviewFormulaEditor = { createFormulaEditor: createFormulaEditor };
}());
