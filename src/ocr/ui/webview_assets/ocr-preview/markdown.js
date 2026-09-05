(function () {
  "use strict";

  function createRenderer(options) {
    if (!options || !options.security || typeof options.markdownIt !== "function") {
      throw new Error("Preview Markdown renderer dependencies are unavailable.");
    }

    var security = options.security;
    var katex = options.katex;
    var mermaid = options.mermaid;
    var Chart = options.Chart;
    var isCurrentGeneration = options.isCurrentGeneration;
    if (typeof security.sanitizeHtml !== "function" ||
        typeof security.sanitizeSvg !== "function" ||
        typeof security.applyHtmlCompatibility !== "function" ||
        typeof security.wrapTables !== "function" ||
        typeof isCurrentGeneration !== "function") {
      throw new Error("Preview Markdown renderer contract is incomplete.");
    }

    var md = options.markdownIt({
      html: true,
      linkify: true,
      // OCR Source uses single hard line breaks for visually separated text
      // lines. Keep those breaks in Preview as well; otherwise markdown-it
      // collapses them into spaces and Preview no longer matches Source.
      breaks: true,
      typographer: false
    });
    md.linkify.set({ fuzzyEmail: false });

    var defaultFenceRenderer = md.renderer.rules.fence;
    var activeCharts = [];
    var mermaidInitialized = false;

    function escapeHtml(value) {
      return String(value || "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/\"/g, "&quot;");
    }

    function createDiagramContext() {
      var salt = Math.floor(Math.random() * 0x7fffffff).toString(36);
      var context = {
        prefix: "ZENCROPDIAGRAM" + salt,
        tokens: []
      };
      context.add = function (kind, source, info) {
        var placeholder = context.prefix + "X" + context.tokens.length + "END";
        context.tokens.push({
          placeholder: placeholder,
          kind: kind,
          source: source || "",
          info: info || ""
        });
        return placeholder;
      };
      return context;
    }

    function diagramKindFromInfo(info) {
      var lang = String(info || "").trim().split(/\s+/)[0].toLowerCase();
      if (lang === "mermaid") return "mermaid";
      if (lang === "chart" || lang === "chartjs" || lang === "chart.js") return "chart";
      return "";
    }

    md.renderer.rules.fence = function (tokens, idx, renderOptions, env, self) {
      var token = tokens[idx];
      var kind = diagramKindFromInfo(token.info);
      if (kind && env && env.diagramContext) {
        return "\n" + env.diagramContext.add(kind, token.content, token.info) + "\n";
      }
      if (defaultFenceRenderer) {
        return defaultFenceRenderer(tokens, idx, renderOptions, env, self);
      }
      return "<pre><code>" + escapeHtml(token.content) + "</code></pre>\n";
    };

    function lineEnd(text, start) {
      var index = text.indexOf("\n", start);
      return index === -1 ? text.length : index + 1;
    }

    function isLineStart(text, index) {
      return index === 0 || text.charAt(index - 1) === "\n";
    }

    function matchFence(text, index) {
      if (!isLineStart(text, index)) return null;
      var line = text.slice(index, lineEnd(text, index));
      var match = /^( {0,3})(`{3,}|~{3,})/.exec(line);
      if (!match) return null;
      return { marker: match[2].charAt(0), length: match[2].length };
    }

    function skipFence(text, index, fence) {
      var cursor = lineEnd(text, index);
      while (cursor < text.length) {
        var line = text.slice(cursor, lineEnd(text, cursor));
        var pattern = new RegExp("^( {0,3})\\" + fence.marker + "{" + fence.length + ",}(?:\\s*)$");
        if (pattern.test(line.replace(/\r?\n$/, ""))) return lineEnd(text, cursor);
        cursor = lineEnd(text, cursor);
      }
      return text.length;
    }

    function skipCodeSpan(text, index) {
      var markerLength = 1;
      while (index + markerLength < text.length && text.charAt(index + markerLength) === "`") {
        markerLength += 1;
      }
      var marker = new Array(markerLength + 1).join("`");
      var end = text.indexOf(marker, index + markerLength);
      return end === -1 ? index + markerLength : end + markerLength;
    }

    function isIndentedCodeLine(text, index) {
      if (!isLineStart(text, index)) return false;
      return text.substr(index, 4) === "    " || text.charAt(index) === "\t";
    }

    function isEscaped(text, index) {
      var backslashCount = 0;
      var cursor = index - 1;
      while (cursor >= 0 && text.charAt(cursor) === "\\") {
        backslashCount += 1;
        cursor -= 1;
      }
      return backslashCount % 2 === 1;
    }

    function findMathEnd(text, start, delimiter) {
      var cursor = start;
      while (cursor < text.length) {
        var found = text.indexOf(delimiter, cursor);
        if (found === -1) return -1;
        if (!isEscaped(text, found)) return found;
        cursor = found + delimiter.length;
      }
      return -1;
    }

    function findInlineDollarEnd(text, start) {
      for (var cursor = start; cursor < text.length; cursor += 1) {
        var character = text.charAt(cursor);
        if (character === "\r" || character === "\n") return -1;
        if (character === "$" && !isEscaped(text, cursor)) return cursor;
      }
      return -1;
    }

    function normalizeMathSource(source) {
      return source.replace(/\r\n?/g, "\n").trim();
    }

    function protectMath(markdown) {
      var tokens = [];
      var salt = Math.floor(Math.random() * 0x7fffffff).toString(36);
      var output = "";
      var index = 0;

      function addToken(source, display) {
        var placeholder = "ZENCROPMATH" + salt + "X" + tokens.length + "END";
        tokens.push({
          placeholder: placeholder,
          source: normalizeMathSource(source),
          display: display
        });
        return placeholder;
      }

      while (index < markdown.length) {
        var fence = matchFence(markdown, index);
        if (fence) {
          var fenceEnd = skipFence(markdown, index, fence);
          output += markdown.slice(index, fenceEnd);
          index = fenceEnd;
          continue;
        }

        if (isIndentedCodeLine(markdown, index)) {
          var codeLineEnd = lineEnd(markdown, index);
          output += markdown.slice(index, codeLineEnd);
          index = codeLineEnd;
          continue;
        }

        var character = markdown.charAt(index);
        if (character === "`") {
          var codeEnd = skipCodeSpan(markdown, index);
          output += markdown.slice(index, codeEnd);
          index = codeEnd;
          continue;
        }

        if (!isEscaped(markdown, index) && markdown.substr(index, 2) === "$$") {
          var dollarEnd = findMathEnd(markdown, index + 2, "$$");
          if (dollarEnd !== -1) {
            output += addToken(markdown.slice(index + 2, dollarEnd), true);
            index = dollarEnd + 2;
            continue;
          }
        }

        if (markdown.substr(index, 2) === "\\[") {
          var bracketEnd = findMathEnd(markdown, index + 2, "\\]");
          if (bracketEnd !== -1) {
            output += addToken(markdown.slice(index + 2, bracketEnd), true);
            index = bracketEnd + 2;
            continue;
          }
        }

        if (markdown.substr(index, 2) === "\\(") {
          var parenEnd = findMathEnd(markdown, index + 2, "\\)");
          if (parenEnd !== -1) {
            output += addToken(markdown.slice(index + 2, parenEnd), false);
            index = parenEnd + 2;
            continue;
          }
        }

        if (character === "$" && !isEscaped(markdown, index)) {
          var inlineDollarEnd = findInlineDollarEnd(markdown, index + 1);
          if (inlineDollarEnd !== -1) {
            var inlineSource = markdown.slice(index + 1, inlineDollarEnd);
            var normalizedInlineSource = normalizeMathSource(inlineSource);
            if (normalizedInlineSource &&
                /[\\_^{}=<>+\-*\/]|[a-zA-Z]/.test(normalizedInlineSource)) {
              output += addToken(normalizedInlineSource, false);
              index = inlineDollarEnd + 1;
              continue;
            }
            output += markdown.slice(index, inlineDollarEnd + 1);
            index = inlineDollarEnd + 1;
            continue;
          }
        }

        output += character;
        index += 1;
      }

      return { markdown: output, tokens: tokens, prefix: "ZENCROPMATH" + salt };
    }

    function mathOptions(display) {
      return {
        displayMode: display,
        throwOnError: false,
        trust: false,
        maxSize: 10,
        maxExpand: 1000
      };
    }

    function createMathNode(token) {
      var span = document.createElement("span");
      span.className = token.display ? "math-node math-display" : "math-node math-inline";
      span.setAttribute("data-tex", token.source);
      span.setAttribute("data-display", token.display ? "block" : "inline");
      if (katex && typeof katex.render === "function") {
        try {
          katex.render(token.source, span, mathOptions(token.display));
          return span;
        } catch (_) {
          // Fall through to the safe plain-text fallback.
        }
      }
      span.className += " math-fallback";
      span.textContent = token.display ? "$$" + token.source + "$$" : "\\(" + token.source + "\\)";
      return span;
    }

    function replaceMathPlaceholders(root, protectedMath) {
      if (!protectedMath.tokens.length) return;
      var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
        acceptNode: function (node) {
          var parent = node.parentElement;
          while (parent && parent !== root) {
            var name = parent.nodeName.toLowerCase();
            if (name === "pre" || name === "code" || name === "script" || name === "style" || name === "textarea") {
              return NodeFilter.FILTER_REJECT;
            }
            parent = parent.parentElement;
          }
          return node.nodeValue.indexOf(protectedMath.prefix) === -1
            ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_ACCEPT;
        }
      });
      var nodes = [];
      while (walker.nextNode()) nodes.push(walker.currentNode);
      nodes.forEach(function (node) {
        var text = node.nodeValue;
        var fragment = document.createDocumentFragment();
        var cursor = 0;
        while (cursor < text.length) {
          var nearestIndex = -1;
          var nearestToken = null;
          protectedMath.tokens.forEach(function (token) {
            var found = text.indexOf(token.placeholder, cursor);
            if (found !== -1 && (nearestIndex === -1 || found < nearestIndex)) {
              nearestIndex = found;
              nearestToken = token;
            }
          });
          if (nearestIndex === -1) {
            fragment.appendChild(document.createTextNode(text.slice(cursor)));
            break;
          }
          if (nearestIndex > cursor) fragment.appendChild(document.createTextNode(text.slice(cursor, nearestIndex)));
          fragment.appendChild(createMathNode(nearestToken));
          cursor = nearestIndex + nearestToken.placeholder.length;
        }
        node.parentNode.replaceChild(fragment, node);
      });
    }

    function destroyActiveCharts() {
      activeCharts.forEach(function (chart) {
        try {
          chart.destroy();
        } catch (_) {
          // Detached Chart instances can already be disposed.
        }
      });
      activeCharts = [];
    }

    function setDiagramMessage(container, message, isError) {
      container.className = "diagram-block " + (isError ? "diagram-error" : "diagram-empty");
      container.textContent = message;
    }

    function ensureMermaid() {
      if (!mermaid) return false;
      if (!mermaidInitialized) {
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "dark",
          htmlLabels: false,
          flowchart: { htmlLabels: false },
          fontFamily: "Segoe UI, Microsoft YaHei UI, Arial, sans-serif",
          themeVariables: {
            background: "#1e1e1e",
            primaryColor: "#2d2d30",
            primaryTextColor: "#d4d4d4",
            primaryBorderColor: "#4ec9b0",
            lineColor: "#8f8f8f",
            secondaryColor: "#252526",
            tertiaryColor: "#171717"
          }
        });
        mermaidInitialized = true;
      }
      return true;
    }

    function renderMermaidDiagram(container, token, generation) {
      if (!ensureMermaid()) {
        setDiagramMessage(container, "Mermaid renderer is not available.", true);
        return;
      }
      var source = String(token.source || "").trim();
      if (!source) {
        setDiagramMessage(container, "Mermaid diagram is empty.", false);
        return;
      }
      var id = "zencrop-mermaid-" + generation + "-" + Math.random().toString(36).slice(2);
      Promise.resolve(mermaid.render(id, source)).then(function (result) {
        if (!isCurrentGeneration(generation)) return;
        var svg = typeof result === "string" ? result : result && result.svg;
        if (!svg) throw new Error("Mermaid returned an empty SVG.");
        container.className = "diagram-block mermaid-block";
        container.innerHTML = security.sanitizeSvg(svg);
        if (result && typeof result.bindFunctions === "function") result.bindFunctions(container);
      }).catch(function (error) {
        if (!isCurrentGeneration(generation)) return;
        setDiagramMessage(container, error && error.message ? error.message : String(error), true);
      });
    }

    function sanitizeChartValue(value, depth) {
      if (depth > 8) return undefined;
      if (value === null || typeof value === "boolean") return value;
      if (typeof value === "number") return Number.isFinite(value) ? value : undefined;
      if (typeof value === "string") return value.slice(0, 2000);
      if (Array.isArray(value)) {
        return value.slice(0, 1000).map(function (item) {
          return sanitizeChartValue(item, depth + 1);
        }).filter(function (item) { return item !== undefined; });
      }
      if (typeof value === "object") {
        var cleaned = {};
        Object.keys(value).slice(0, 160).forEach(function (key) {
          var lowerKey = key.toLowerCase();
          if (lowerKey.indexOf("__") === 0 || lowerKey.indexOf("proto") !== -1 || lowerKey.indexOf("constructor") !== -1) return;
          var cleanedValue = sanitizeChartValue(value[key], depth + 1);
          if (cleanedValue !== undefined) cleaned[key] = cleanedValue;
        });
        return cleaned;
      }
      return undefined;
    }

    function allowedChartType(type) {
      return /^(bar|line|pie|doughnut|radar|polarArea|bubble|scatter)$/i.test(String(type || ""));
    }

    function enforceChartLimits(config) {
      var data = config.data || {};
      var labels = Array.isArray(data.labels) ? data.labels : [];
      var datasets = Array.isArray(data.datasets) ? data.datasets : [];
      if (labels.length > 1000) throw new Error("Chart has too many labels.");
      if (datasets.length > 20) throw new Error("Chart has too many datasets.");
      datasets.forEach(function (dataset) {
        var points = Array.isArray(dataset && dataset.data) ? dataset.data.length : 0;
        if (points > 1000) throw new Error("Chart dataset has too many points.");
      });
    }

    function applyChartTheme(config) {
      config.options = (config.options && typeof config.options === "object") ? config.options : {};
      config.options.responsive = true;
      config.options.maintainAspectRatio = false;
      if (config.options.animation === undefined) config.options.animation = false;
      if (!config.options.color) config.options.color = "#d4d4d4";
      config.options.plugins = (config.options.plugins && typeof config.options.plugins === "object") ? config.options.plugins : {};
      config.options.plugins.legend = (config.options.plugins.legend && typeof config.options.plugins.legend === "object") ? config.options.plugins.legend : {};
      config.options.plugins.legend.labels = (config.options.plugins.legend.labels && typeof config.options.plugins.legend.labels === "object") ? config.options.plugins.legend.labels : {};
      if (!config.options.plugins.legend.labels.color) config.options.plugins.legend.labels.color = "#d4d4d4";
      config.options.scales = (config.options.scales && typeof config.options.scales === "object") ? config.options.scales : {};
      Object.keys(config.options.scales).forEach(function (axis) {
        var scale = config.options.scales[axis];
        if (!scale || typeof scale !== "object") return;
        scale.ticks = (scale.ticks && typeof scale.ticks === "object") ? scale.ticks : {};
        scale.grid = (scale.grid && typeof scale.grid === "object") ? scale.grid : {};
        if (!scale.ticks.color) scale.ticks.color = "#bdbdbd";
        if (!scale.grid.color) scale.grid.color = "#3c3c3c";
      });
      return config;
    }

    function buildChartConfig(source) {
      if (source.length > 100 * 1024) throw new Error("Chart config is too large.");
      var parsed = JSON.parse(source);
      var config = sanitizeChartValue(parsed, 0);
      if (!config || typeof config !== "object") throw new Error("Chart config must be a JSON object.");
      if (!allowedChartType(config.type)) throw new Error("Unsupported chart type.");
      if (!config.data || typeof config.data !== "object") throw new Error("Chart config requires a data object.");
      enforceChartLimits(config);
      return applyChartTheme(config);
    }

    function renderChartDiagram(container, token, generation) {
      if (!Chart) {
        setDiagramMessage(container, "Chart.js renderer is not available.", true);
        return;
      }
      try {
        var config = buildChartConfig(String(token.source || "").trim());
        if (!isCurrentGeneration(generation)) return;
        container.className = "diagram-block chart-block";
        container.innerHTML = "";
        var canvas = document.createElement("canvas");
        canvas.setAttribute("role", "img");
        container.appendChild(canvas);
        activeCharts.push(new Chart(canvas.getContext("2d"), config));
      } catch (error) {
        setDiagramMessage(container, error && error.message ? error.message : String(error), true);
      }
    }

    function createDiagramNode(token, generation) {
      var node = document.createElement("div");
      node.className = "diagram-block diagram-pending";
      node.textContent = "Rendering diagram...";
      window.requestAnimationFrame(function () {
        if (!isCurrentGeneration(generation)) return;
        if (token.kind === "mermaid") renderMermaidDiagram(node, token, generation);
        else if (token.kind === "chart") renderChartDiagram(node, token, generation);
      });
      return node;
    }

    function replaceDiagramPlaceholders(root, diagramContext, generation) {
      if (!diagramContext || !diagramContext.tokens.length) return;
      var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
        acceptNode: function (node) {
          return node.nodeValue.indexOf(diagramContext.prefix) === -1
            ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_ACCEPT;
        }
      });
      var nodes = [];
      while (walker.nextNode()) nodes.push(walker.currentNode);
      nodes.forEach(function (node) {
        var text = node.nodeValue;
        var fragment = document.createDocumentFragment();
        var cursor = 0;
        while (cursor < text.length) {
          var nearestIndex = -1;
          var nearestToken = null;
          diagramContext.tokens.forEach(function (token) {
            var found = text.indexOf(token.placeholder, cursor);
            if (found !== -1 && (nearestIndex === -1 || found < nearestIndex)) {
              nearestIndex = found;
              nearestToken = token;
            }
          });
          if (nearestIndex === -1) {
            fragment.appendChild(document.createTextNode(text.slice(cursor)));
            break;
          }
          if (nearestIndex > cursor) fragment.appendChild(document.createTextNode(text.slice(cursor, nearestIndex)));
          fragment.appendChild(createDiagramNode(nearestToken, generation));
          cursor = nearestIndex + nearestToken.placeholder.length;
        }
        node.parentNode.replaceChild(fragment, node);
      });
    }

    function renderInto(root, markdown, generation) {
      var protectedMath = protectMath(String(markdown || ""));
      var diagramContext = createDiagramContext();
      var html = md.render(protectedMath.markdown, { diagramContext: diagramContext });
      root.innerHTML = security.sanitizeHtml(html);
      replaceMathPlaceholders(root, protectedMath);
      replaceDiagramPlaceholders(root, diagramContext, generation);
      security.applyHtmlCompatibility(root);
      security.wrapTables(root);
    }

    return {
      destroy: destroyActiveCharts,
      renderInto: renderInto,
      parseSource: function (markdown, context) { return md.parse(markdown, context); },
      createDiagramContext: createDiagramContext,
      diagramKindFromInfo: diagramKindFromInfo
    };
  }

  window.ZenCropPreviewMarkdown = { createRenderer: createRenderer };
}());
