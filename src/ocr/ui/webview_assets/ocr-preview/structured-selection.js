(function () {
  "use strict";

  var MAX_INPUT_CHARS = 1024 * 1024;
  var MAX_OUTPUT_CHARS = 1024 * 1024;
  var MAX_MARKDOWN_CHARS = 100000;
  // Turndown can add one Markdown escape per UTF-16 unit. Keep the raw chunk
  // below half of the native 4,000-unit plan limit.
  var MAX_LEAF_CHARS = 1800;
  var MAX_LEAVES = 5000;
  var MAX_NODES = 20000;
  var MAX_DEPTH = 128;
  var MAX_MILLISECONDS = 2000;
  var BLOCK_TAGS = {
    address: true, blockquote: true, caption: true, dd: true, dt: true,
    figcaption: true, h1: true, h2: true, h3: true, h4: true, h5: true,
    h6: true, li: true, p: true, td: true, th: true
  };
  var SKIP_TEXT_TAGS = {
    code: true, math: true, pre: true, script: true, style: true,
    svg: true, textarea: true
  };

  function fail(code) {
    var error = new Error(code);
    error.code = code;
    throw error;
  }

  function validateEnvelope(payload) {
    var token = String(payload && payload.token || "");
    var generation = Number(payload && payload.generation);
    if (!/^[a-f0-9]{32}$/i.test(token) || !Number.isSafeInteger(generation) || generation <= 0) {
      fail("invalid_envelope");
    }
    return { token: token.toLowerCase(), generation: generation };
  }

  function decodeUtf8Base64(value) {
    var encoded = String(value || "");
    if (!encoded || encoded.length > 1536 * 1024 || !/^[a-z0-9+/=]+$/i.test(encoded)) {
      fail("invalid_payload");
    }
    var binary;
    try { binary = atob(encoded); } catch (_) { fail("invalid_payload"); }
    if (binary.length > MAX_INPUT_CHARS * 4) fail("payload_too_large");
    var bytes = new Uint8Array(binary.length);
    for (var i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
    try {
      return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    } catch (_) {
      fail("invalid_utf8");
    }
  }

  function enforceTreeLimits(root) {
    var stack = [{ node: root, depth: 0 }];
    var count = 0;
    while (stack.length) {
      var item = stack.pop();
      count += 1;
      if (count > MAX_NODES) fail("node_limit");
      if (item.depth > MAX_DEPTH) fail("depth_limit");
      for (var child = item.node.lastChild; child; child = child.previousSibling) {
        stack.push({ node: child, depth: item.depth + 1 });
      }
    }
  }

  function findSelectionComments(documentRoot, token) {
    var startText = "ZENCROP_SELECTION_START_" + token;
    var endText = "ZENCROP_SELECTION_END_" + token;
    var start = null;
    var end = null;
    var walker = documentRoot.createTreeWalker(
      documentRoot, NodeFilter.SHOW_COMMENT, null);
    while (walker.nextNode()) {
      var value = String(walker.currentNode.nodeValue || "").trim();
      if (value === startText) {
        if (start) fail("duplicate_start_marker");
        start = walker.currentNode;
      } else if (value === endText) {
        if (end) fail("duplicate_end_marker");
        end = walker.currentNode;
      }
    }
    if (!start || !end) fail("selection_marker_missing");
    var order = start.compareDocumentPosition(end);
    if (!(order & Node.DOCUMENT_POSITION_FOLLOWING)) fail("selection_marker_order");
    return { start: start, end: end };
  }

  function formulaAncestor(node) {
    var element = node && (node.nodeType === Node.ELEMENT_NODE
      ? node : node.parentElement);
    return element && element.closest
      ? element.closest(".katex, .math-node, mjx-container, math") : null;
  }

  function fragmentFromMarkedHtml(html, token) {
    if (!html || html.length > MAX_INPUT_CHARS) fail("payload_too_large");
    var parsed = new DOMParser().parseFromString(html, "text/html");
    enforceTreeLimits(parsed.documentElement);
    var markers = findSelectionComments(parsed, token);
    var range = parsed.createRange();
    var startFormula = formulaAncestor(markers.start);
    var endFormula = formulaAncestor(markers.end);
    if (startFormula) range.setStartBefore(startFormula);
    else range.setStartAfter(markers.start);
    if (endFormula) range.setEndAfter(endFormula);
    else range.setEndBefore(markers.end);
    var fragment = range.cloneContents();
    var common = range.commonAncestorContainer.nodeType === Node.ELEMENT_NODE
      ? range.commonAncestorContainer : range.commonAncestorContainer.parentElement;
    var structuralRoot = common && common.closest
      ? common.closest("table,ul,ol,dl") : null;
    if (common && structuralRoot) {
      var wrapped = fragment;
      for (var context = common; context; context = context.parentElement) {
        var shell = context.cloneNode(false);
        shell.appendChild(wrapped);
        wrapped = documentRootFragment(parsed, shell);
        if (context === structuralRoot) break;
      }
      fragment = wrapped;
    }
    enforceTreeLimits(fragment);
    return fragment;
  }

  function documentRootFragment(owner, node) {
    var fragment = owner.createDocumentFragment();
    fragment.appendChild(node);
    return fragment;
  }

  function addLiteral(literals, token, value) {
    var placeholder = "ZC" + token + "L" + String(literals.length + 1).padStart(5, "0") + "Z";
    literals.push({ placeholder: placeholder, value: String(value || "") });
    return placeholder;
  }

  function isEscaped(text, index) {
    var count = 0;
    for (var cursor = index - 1; cursor >= 0 && text.charAt(cursor) === "\\"; cursor -= 1) count += 1;
    return count % 2 === 1;
  }

  function lineEnd(text, index) {
    var end = text.indexOf("\n", index);
    return end === -1 ? text.length : end + 1;
  }

  function protectMarkdownMath(markdown, token, literals) {
    var output = "";
    var index = 0;
    while (index < markdown.length) {
      var lineStart = index === 0 || markdown.charAt(index - 1) === "\n";
      if (lineStart) {
        var fenceMatch = /^( {0,3})(`{3,}|~{3,})/.exec(markdown.slice(index, lineEnd(markdown, index)));
        if (fenceMatch) {
          var marker = fenceMatch[2].charAt(0);
          var length = fenceMatch[2].length;
          var fenceEnd = lineEnd(markdown, index);
          while (fenceEnd < markdown.length) {
            var line = markdown.slice(fenceEnd, lineEnd(markdown, fenceEnd)).replace(/\r?\n$/, "");
            if (new RegExp("^( {0,3})\\" + marker + "{" + length + ",}\\s*$").test(line)) {
              fenceEnd = lineEnd(markdown, fenceEnd);
              break;
            }
            fenceEnd = lineEnd(markdown, fenceEnd);
          }
          output += markdown.slice(index, fenceEnd);
          index = fenceEnd;
          continue;
        }
      }
      if (markdown.charAt(index) === "`") {
        var run = 1;
        while (markdown.charAt(index + run) === "`") run += 1;
        var delimiter = new Array(run + 1).join("`");
        var codeEnd = markdown.indexOf(delimiter, index + run);
        codeEnd = codeEnd === -1 ? index + run : codeEnd + run;
        output += markdown.slice(index, codeEnd);
        index = codeEnd;
        continue;
      }
      var open = "";
      var close = "";
      var display = false;
      if (!isEscaped(markdown, index) && markdown.substr(index, 2) === "$$") {
        open = close = "$$"; display = true;
      } else if (markdown.substr(index, 2) === "\\[") {
        open = "\\["; close = "\\]"; display = true;
      } else if (markdown.substr(index, 2) === "\\(") {
        open = "\\("; close = "\\)";
      } else if (markdown.charAt(index) === "$" && !isEscaped(markdown, index)) {
        open = close = "$";
      }
      if (open) {
        var end = markdown.indexOf(close, index + open.length);
        while (end !== -1 && isEscaped(markdown, end)) end = markdown.indexOf(close, end + close.length);
        if (end !== -1 && (display || open !== "$" || markdown.slice(index + 1, end).indexOf("\n") === -1)) {
          var source = markdown.slice(index + open.length, end);
          if (source.trim() && (display || open !== "$" || /[\\_^{}=<>+\-*\/a-z]/i.test(source))) {
            output += addLiteral(literals, token, open + source + close);
            index = end + close.length;
            continue;
          }
        }
      }
      output += markdown.charAt(index);
      index += 1;
    }
    return output;
  }

  function fragmentFromMarkdown(markdown, token, literals) {
    if (!markdown || markdown.length > MAX_MARKDOWN_CHARS) fail("markdown_too_large");
    if (typeof window.markdownit !== "function") fail("markdown_renderer_unavailable");
    var protectedMarkdown = protectMarkdownMath(markdown.replace(/\r\n?/g, "\n"), token, literals);
    var md = window.markdownit({ html: true, linkify: true, breaks: true, typographer: false });
    md.linkify.set({ fuzzyEmail: false });
    var template = document.createElement("template");
    template.innerHTML = md.render(protectedMarkdown);
    enforceTreeLimits(template.content);
    return template.content.cloneNode(true);
  }

  function formulaSource(node) {
    var explicit = node.getAttribute && (node.getAttribute("data-tex") || node.getAttribute("data-latex"));
    if (explicit) return explicit;
    var annotation = node.querySelector && node.querySelector('annotation[encoding="application/x-tex"]');
    return annotation ? String(annotation.textContent || "") : "";
  }

  function normalizeFormulas(fragment, token, literals) {
    var candidates = Array.prototype.slice.call(fragment.querySelectorAll(
      ".katex, .math-node, mjx-container, math"));
    candidates.forEach(function (node) {
      if (!node.parentNode || node.closest("[data-zencrop-formula-normalized]")) return;
      var source = formulaSource(node).trim();
      if (!source) return;
      node.setAttribute("data-zencrop-formula-normalized", "1");
      var display = (node.getAttribute("data-display") || "").toLowerCase() === "block" ||
        node.classList.contains("math-display") || node.classList.contains("katex-display") ||
        !!node.closest(".katex-display, .katex-block") ||
        !!node.querySelector('math[display="block"]') ||
        String(node.getAttribute("display") || "").toLowerCase() === "block";
      node.parentNode.replaceChild(document.createTextNode(
        addLiteral(literals, token, display ? "$$" + source + "$$" : "$" + source + "$")), node);
    });
  }

  function normalizeWrappedPreBlocks(fragment) {
    Array.prototype.forEach.call(fragment.querySelectorAll("pre"), function (pre) {
      if (pre.firstElementChild && pre.firstElementChild.nodeName === "CODE") return;
      var codeNodes = pre.querySelectorAll("code");
      if (codeNodes.length !== 1) return;
      var source = codeNodes[0];
      var className = String(source.getAttribute("class") || "");
      var text = String(source.textContent || "");
      if (!/\blanguage-\S+\b/.test(className) && text.indexOf("\n") === -1) return;
      var code = document.createElement("code");
      if (className) code.setAttribute("class", className);
      code.textContent = text;
      pre.replaceChildren(code);
    });
  }

  function wrapLiteralPlaceholders(fragment, literals) {
    if (!literals.length) return;
    var byPlaceholder = {};
    literals.forEach(function (item, index) {
      byPlaceholder[item.placeholder] = String(index);
    });
    var pattern = new RegExp(literals.map(function (item) {
      return item.placeholder;
    }).join("|"), "g");
    var walker = document.createTreeWalker(fragment, NodeFilter.SHOW_TEXT, null);
    var nodes = [];
    while (walker.nextNode()) nodes.push(walker.currentNode);
    nodes.forEach(function (node) {
      var value = String(node.nodeValue || "");
      pattern.lastIndex = 0;
      if (!pattern.test(value)) return;
      pattern.lastIndex = 0;
      var replacement = document.createDocumentFragment();
      var cursor = 0;
      var match;
      while ((match = pattern.exec(value)) !== null) {
        if (match.index > cursor) replacement.appendChild(
          document.createTextNode(value.slice(cursor, match.index)));
        var span = document.createElement("span");
        span.setAttribute("data-zencrop-literal", byPlaceholder[match[0]]);
        span.textContent = match[0];
        replacement.appendChild(span);
        cursor = match.index + match[0].length;
      }
      if (cursor < value.length) replacement.appendChild(
        document.createTextNode(value.slice(cursor)));
      node.parentNode.replaceChild(replacement, node);
    });
  }

  function sanitizeFragment(fragment) {
    if (!window.DOMPurify) fail("sanitizer_unavailable");
    var holder = document.createElement("div");
    holder.appendChild(fragment.cloneNode(true));
    var clean = window.DOMPurify.sanitize(holder.innerHTML, {
      RETURN_DOM_FRAGMENT: true,
      ALLOWED_TAGS: [
        "p", "br", "hr", "h1", "h2", "h3", "h4", "h5", "h6",
        "strong", "b", "em", "i", "del", "s", "strike", "u", "ins",
        "code", "pre", "blockquote", "q", "cite", "ul", "ol", "li",
        "dl", "dt", "dd", "table", "caption", "colgroup", "col", "thead",
        "tbody", "tfoot", "tr", "th", "td", "img", "a", "span", "div",
        "section", "article", "header", "footer", "main", "aside", "nav",
        "figure", "figcaption", "details", "summary", "mark", "small", "sub",
        "sup", "kbd", "samp", "var", "abbr", "dfn", "time", "address",
        "center", "ruby", "rt", "rp", "wbr", "math", "mrow", "mi", "mn",
        "mo", "ms", "mtext", "mspace", "msup", "msub", "msubsup", "mfrac",
        "msqrt", "mroot", "mfenced", "mtable", "mtr", "mtd", "munderover",
        "munder", "mover", "semantics", "annotation"
      ],
      ALLOWED_ATTR: [
        "href", "title", "src", "alt", "colspan", "rowspan", "align", "valign",
        "start", "reversed", "type", "value", "scope", "headers", "class",
        "language", "encoding", "display", "data-tex", "data-display",
        "data-zencrop-literal"
      ],
      FORBID_TAGS: [
        "script", "style", "iframe", "object", "embed", "form", "input",
        "button", "textarea", "select", "svg", "audio", "video", "canvas",
        "map", "area", "link", "meta"
      ]
    });
    enforceTreeLimits(clean);
    return clean;
  }

  function normalizeUrl(value, sourceUrl) {
    var text = String(value || "").trim();
    if (!text) return "";
    try {
      var url = sourceUrl ? new URL(text, sourceUrl) : new URL(text);
      if (url.username || url.password) return "";
      if (url.protocol !== "http:" && url.protocol !== "https:" && url.protocol !== "mailto:") return "";
      return url.href;
    } catch (_) {
      return "";
    }
  }

  function normalizeLinks(fragment, sourceUrl) {
    Array.prototype.forEach.call(fragment.querySelectorAll("a[href]"), function (anchor) {
      var safe = normalizeUrl(anchor.getAttribute("href"), sourceUrl);
      if (safe) anchor.setAttribute("href", safe); else anchor.removeAttribute("href");
    });
    Array.prototype.forEach.call(fragment.querySelectorAll("img"), function (image) {
      var safe = normalizeUrl(image.getAttribute("src"), sourceUrl);
      if (safe) image.setAttribute("src", safe); else image.removeAttribute("src");
    });
  }

  function nearestBlock(node, root, blocks) {
    var element = node.parentElement;
    while (element && element !== root) {
      var tag = element.nodeName.toLowerCase();
      if (BLOCK_TAGS[tag]) {
        if (!blocks.has(element)) blocks.set(element, "b" + String(blocks.size + 1));
        return blocks.get(element);
      }
      element = element.parentElement;
    }
    if (!blocks.has(root)) blocks.set(root, "b" + String(blocks.size + 1));
    return blocks.get(root);
  }

  function nextLeafChunkEnd(text, start) {
    var end = Math.min(text.length, start + MAX_LEAF_CHARS);
    if (end >= text.length) return text.length;
    var minimum = Math.max(start + 1, end - 256);
    for (var cursor = end; cursor > minimum; cursor -= 1) {
      if (/\s|[.,!?;:\u3002\uff0c\uff01\uff1f\uff1b\uff1a]/.test(
          text.charAt(cursor - 1))) {
        end = cursor;
        break;
      }
    }
    var next = text.charCodeAt(end);
    var previous = text.charCodeAt(end - 1);
    if (previous >= 0xD800 && previous <= 0xDBFF &&
        next >= 0xDC00 && next <= 0xDFFF) end -= 1;
    while (end > start) {
      next = text.charCodeAt(end);
      if ((next >= 0x0300 && next <= 0x036F) ||
          (next >= 0xFE00 && next <= 0xFE0F) || next === 0x200D) {
        end -= 1;
      } else {
        break;
      }
    }
    return end > start ? end : Math.min(text.length, start + MAX_LEAF_CHARS);
  }

  function markTranslatableText(root, token) {
    var leaves = [];
    var blocks = new Map();
    var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
    var nodes = [];
    while (walker.nextNode()) nodes.push(walker.currentNode);
    nodes.forEach(function (node) {
      var text = String(node.nodeValue || "");
      if (!/\S/.test(text)) return;
      var parent = node.parentElement;
      for (var current = parent; current && current !== root; current = current.parentElement) {
        var tag = current.nodeName.toLowerCase();
        if (SKIP_TEXT_TAGS[tag] || current.hasAttribute("data-zencrop-literal") ||
            current.classList.contains("diagram-block")) return;
      }
      var replacement = document.createDocumentFragment();
      for (var start = 0; start < text.length;) {
        var end = nextLeafChunkEnd(text, start);
        var chunk = text.slice(start, end);
        var leading = /^\s*/.exec(chunk)[0];
        var trailing = /\s*$/.exec(chunk.slice(leading.length))[0];
        var coreEnd = chunk.length - trailing.length;
        var core = chunk.slice(leading.length, coreEnd);
        if (leading) replacement.appendChild(document.createTextNode(leading));
        if (core) {
          if (leaves.length >= MAX_LEAVES) fail("leaf_limit");
          var id = "t" + String(leaves.length + 1).padStart(5, "0");
          var marker = "ZC" + token + "T" +
            String(leaves.length + 1).padStart(5, "0");
          leaves.push({
            id: id,
            blockId: nearestBlock(node, root, blocks),
            text: core,
            marker: marker,
            projection: ""
          });
          var span = document.createElement("span");
          span.setAttribute("data-zencrop-segment", id);
          span.textContent = core;
          replacement.appendChild(span);
        }
        if (trailing) replacement.appendChild(document.createTextNode(trailing));
        start = end;
      }
      node.parentNode.replaceChild(replacement, node);
    });
    return leaves;
  }

  function isComplexTable(node) {
    return !!node.querySelector("table") || !!node.querySelector("[rowspan]:not([rowspan=\"1\"]),[colspan]:not([colspan=\"1\"])");
  }

  function escapeMarkdown(value) {
    return String(value || "")
      .replace(/\\/g, "\\\\")
      .replace(/\*/g, "\\*")
      .replace(/^-/, "\\-")
      .replace(/^\+ /, "\\+ ")
      .replace(/^(=+)/, "\\$1")
      .replace(/^(#{1,6}) /, "\\$1 ")
      .replace(/`/g, "\\`")
      .replace(/^~~~/, "\\~~~")
      .replace(/\[/g, "\\[")
      .replace(/\]/g, "\\]")
      .replace(/^>/, "\\>")
      .replace(/_/g, "\\_")
      .replace(/^(\d+)\. /, "$1\\. ");
  }

  function projectLeafSource(leaf) {
    if (leaf.projection === "markdown") return escapeMarkdown(leaf.text);
    if (leaf.projection === "markdownTableCell") {
      return escapeMarkdown(leaf.text)
        .replace(/\|/g, "\\|")
        .replace(/\r?\n/g, "<br>");
    }
    if (leaf.projection === "htmlText") {
      return String(leaf.text || "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;");
    }
    fail("segment_projection_missing");
  }

  function createTurndown(leaves) {
    if (typeof window.TurndownService !== "function" || !window.turndownPluginGfm) {
      fail("turndown_unavailable");
    }
    var service = new window.TurndownService({
      headingStyle: "atx",
      bulletListMarker: "-",
      codeBlockStyle: "fenced",
      fence: "```",
      emDelimiter: "*",
      strongDelimiter: "**",
      linkStyle: "inlined",
      preformattedCode: true
    });
    service.use(window.turndownPluginGfm.gfm);
    var leavesById = {};
    leaves.forEach(function (leaf) { leavesById[leaf.id] = leaf; });
    service.addRule("structuredSegment", {
      filter: function (node) {
        return node.nodeName === "SPAN" && node.hasAttribute("data-zencrop-segment");
      },
      replacement: function (content, node) {
        var leaf = leavesById[String(node.getAttribute("data-zencrop-segment") || "")];
        if (!leaf) fail("unknown_segment_node");
        leaf.text = String(node.textContent || "");
        leaf.projection = "markdown";
        return leaf.marker + "O" + content + leaf.marker + "C";
      }
    });
    service.addRule("singleParagraphListItem", {
      filter: function (node) {
        return node.nodeName === "P" && node.parentElement &&
          node.parentElement.nodeName === "LI" &&
          node.parentElement.childElementCount === 1;
      },
      replacement: function (content) { return content; }
    });
    service.addRule("complexTable", {
      filter: function (node) { return node.nodeName === "TABLE" && isComplexTable(node); },
      replacement: function (_, node) {
        var clone = node.cloneNode(true);
        Array.prototype.forEach.call(
          clone.querySelectorAll("span[data-zencrop-segment]"), function (span) {
            var leaf = leavesById[String(span.getAttribute("data-zencrop-segment") || "")];
            if (!leaf) fail("unknown_complex_table_segment");
            leaf.text = String(span.textContent || "");
            leaf.projection = "htmlText";
            span.parentNode.replaceChild(document.createTextNode(
              leaf.marker + "O" + leaf.text + leaf.marker + "C"), span);
          });
        return "\n\n" + clone.outerHTML + "\n\n";
      }
    });
    service.addRule("structuredTableCell", {
      filter: function (node) {
        if (node.nodeName !== "TH" && node.nodeName !== "TD") return false;
        var table = node.closest("table");
        return table && !isComplexTable(table);
      },
      replacement: function (content, node) {
        Array.prototype.forEach.call(
          node.querySelectorAll("span[data-zencrop-segment]"), function (span) {
            var leaf = leavesById[String(span.getAttribute("data-zencrop-segment") || "")];
            if (leaf) leaf.projection = "markdownTableCell";
          });
        content = content.replace(/\|/g, "\\|").replace(/\r?\n/g, "<br>");
        var index = Array.prototype.indexOf.call(node.parentNode.childNodes, node);
        return (index === 0 ? "| " : " ") + content + " |";
      }
    });
    service.addRule("mathml", {
      filter: "math",
      replacement: function (_, node) { return node.outerHTML; }
    });
    service.addRule("safeImageAsLink", {
      filter: "img",
      replacement: function (_, node) {
        var alt = String(node.getAttribute("alt") || "Image").replace(/[\[\]]/g, "");
        var src = String(node.getAttribute("src") || "");
        return src ? "[" + alt + "](" + src.replace(/[()]/g, encodeURIComponent) + ")" : alt;
      }
    });
    return service;
  }

  function restoreLiterals(markdown, literals) {
    literals.forEach(function (item) {
      var first = markdown.indexOf(item.placeholder);
      if (first === -1 || markdown.indexOf(item.placeholder, first + item.placeholder.length) !== -1) {
        fail("literal_projection_invalid");
      }
      markdown = markdown.slice(0, first) + item.value + markdown.slice(first + item.placeholder.length);
    });
    return markdown;
  }

  function buildParts(markdown, token, leaves) {
    var byIndex = {};
    leaves.forEach(function (leaf, index) {
      byIndex[String(index + 1).padStart(5, "0")] = leaf;
    });
    var pattern = new RegExp("ZC" + token + "T([0-9]{5})([OC])", "g");
    var parts = [];
    var counts = {};
    var cursor = 0;
    var open = null;
    var match;
    while ((match = pattern.exec(markdown)) !== null) {
      var leaf = byIndex[match[1]];
      if (!leaf) fail("unknown_segment_placeholder");
      if (match[2] === "O") {
        if (open) fail("nested_segment_placeholder");
        if (match.index > cursor) parts.push({ literal: markdown.slice(cursor, match.index) });
        open = { leaf: leaf, contentStart: pattern.lastIndex };
      } else {
        if (!open || open.leaf !== leaf) fail("segment_placeholder_order");
        var sourceProjection = markdown.slice(open.contentStart, match.index);
        if (!/\S/.test(leaf.text) || sourceProjection !== projectLeafSource(leaf)) {
          fail("segment_text_invalid");
        }
        counts[leaf.id] = (counts[leaf.id] || 0) + 1;
        parts.push({ segmentId: leaf.id });
        cursor = pattern.lastIndex;
        open = null;
      }
    }
    if (open) fail("segment_placeholder_unclosed");
    if (cursor < markdown.length) parts.push({ literal: markdown.slice(cursor) });
    leaves.forEach(function (leaf) {
      if (counts[leaf.id] !== 1) fail("segment_projection_invalid");
    });
    if (!parts.length && markdown) parts.push({ literal: markdown });
    return parts;
  }

  function project(parts, leaves) {
    var values = {};
    leaves.forEach(function (leaf) { values[leaf.id] = projectLeafSource(leaf); });
    return parts.map(function (part) {
      return part.segmentId ? values[part.segmentId] : String(part.literal || "");
    }).join("");
  }

  function convertFragment(fragment, envelope, sourceUrl, literals, started) {
    normalizeWrappedPreBlocks(fragment);
    normalizeFormulas(fragment, envelope.token, literals);
    wrapLiteralPlaceholders(fragment, literals);
    normalizeLinks(fragment, sourceUrl);
    var clean = sanitizeFragment(fragment);
    normalizeLinks(clean, sourceUrl);
    var holder = document.createElement("div");
    holder.appendChild(clean);
    var leaves = markTranslatableText(holder, envelope.token);
    var markdown = createTurndown(leaves).turndown(holder);
    markdown = restoreLiterals(markdown, literals)
      .replace(/\r\n?/g, "\n").replace(/\n+$/, "");
    if (!markdown || markdown.length > MAX_MARKDOWN_CHARS) fail("markdown_size_invalid");
    var parts = buildParts(markdown, envelope.token, leaves);
    var sourceMarkdown = project(parts, leaves);
    var plan = {
      version: 1,
      token: envelope.token,
      generation: envelope.generation,
      sourceMarkdown: sourceMarkdown,
      parts: parts,
      leaves: leaves.map(function (leaf) {
        return {
          id: leaf.id,
          blockId: leaf.blockId,
          text: leaf.text,
          projection: leaf.projection
        };
      })
    };
    var serialized = JSON.stringify(plan);
    if (serialized.length > MAX_OUTPUT_CHARS) fail("plan_too_large");
    if (performance.now() - started > MAX_MILLISECONDS) fail("conversion_timeout");
    return serialized;
  }

  function prepare(payload) {
    var started = performance.now();
    var envelope = validateEnvelope(payload);
    var format = String(payload.format || "");
    var decoded = decodeUtf8Base64(payload.payloadBase64);
    var literals = [];
    var fragment;
    if (format === "html") {
      fragment = fragmentFromMarkedHtml(decoded, envelope.token);
    } else if (format === "markdown") {
      fragment = fragmentFromMarkdown(decoded, envelope.token, literals);
    } else {
      fail("unsupported_format");
    }
    return convertFragment(fragment, envelope, String(payload.sourceUrl || ""), literals, started);
  }

  function preparePreviewSelection(payload, previewRoot, sourceMarkdown, editing) {
    var started = performance.now();
    var envelope = validateEnvelope(payload);
    if (!previewRoot || editing) fail("preview_selection_unavailable");
    var expectedSelectionGeneration = Number(payload.selectionGeneration);
    if (!Number.isSafeInteger(expectedSelectionGeneration) ||
        expectedSelectionGeneration !== Number(payload.currentSelectionGeneration)) {
      fail("stale_preview_selection");
    }
    var selection = window.getSelection();
    if (!selection || selection.rangeCount !== 1 || selection.isCollapsed) {
      fail("preview_selection_empty");
    }
    var range = selection.getRangeAt(0);
    if (!previewRoot.contains(range.commonAncestorContainer)) fail("preview_selection_outside");
    var selectedRange = range.cloneRange();
    var startFormula = formulaAncestor(range.startContainer);
    var endFormula = formulaAncestor(range.endContainer);
    if (startFormula && previewRoot.contains(startFormula)) {
      selectedRange.setStartBefore(startFormula);
    }
    if (endFormula && previewRoot.contains(endFormula)) {
      selectedRange.setEndAfter(endFormula);
    }
    var literals = [];
    var completeDocument = selectedRange.startContainer === previewRoot &&
      selectedRange.startOffset === 0 && selectedRange.endContainer === previewRoot &&
      selectedRange.endOffset === previewRoot.childNodes.length;
    var fragment = completeDocument && sourceMarkdown
      ? fragmentFromMarkdown(String(sourceMarkdown), envelope.token, literals)
      : selectedRange.cloneContents();
    enforceTreeLimits(fragment);
    return convertFragment(fragment, envelope, "", literals, started);
  }

  window.ZenCropStructuredSelection = {
    prepare: prepare,
    preparePreviewSelection: preparePreviewSelection,
    limits: {
      inputChars: MAX_INPUT_CHARS,
      outputChars: MAX_OUTPUT_CHARS,
      markdownChars: MAX_MARKDOWN_CHARS,
      leafChars: MAX_LEAF_CHARS,
      leaves: MAX_LEAVES,
      nodes: MAX_NODES,
      depth: MAX_DEPTH,
      milliseconds: MAX_MILLISECONDS
    }
  };
}());
