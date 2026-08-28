(function () {
  "use strict";

  function createBlockMapper(options) {
    if (!options || !options.root || !options.markdown ||
        typeof options.markdown.parseSource !== "function" ||
        typeof options.markdown.createDiagramContext !== "function" ||
        typeof options.markdown.diagramKindFromInfo !== "function") {
      throw new Error("Preview block mapper dependencies are unavailable.");
    }

    var root = options.root;
    var markdown = options.markdown;

    function normalizeLabel(value) {
      return String(value || "").trim().toLowerCase().replace(/[\s-]+/g, "_");
    }

    function normalizeText(value) {
      return String(value || "")
        .replace(/!\[([^\]]*)\]\(([^)]*)\)/g, " $1 $2 ")
        .replace(/\[([^\]]+)\]\(([^)]*)\)/g, " $1 ")
        .replace(/<[^>]+>/g, " ")
        .replace(/&nbsp;/gi, " ")
        .replace(/[`*_~>#]/g, " ")
        .replace(/[|:+\-=[\]{}()\\/$^]+/g, " ")
        .replace(/\s+/g, " ")
        .trim()
        .toLowerCase();
    }

    function isTable(block) {
      var label = block.labelKey || "";
      var content = String(block.content || "");
      return label.indexOf("table") !== -1 || /<table[\s>]/i.test(content) || /\n\s*\|.+\|\s*\n/.test(content);
    }

    function isImage(block) {
      var label = block.labelKey || "";
      var content = String(block.content || "");
      return /^(?:image|img|figure|chart|seal|picture|photo|illustration)$/.test(label) ||
        /!\[[^\]]*\]\([^)]+\)/.test(content) || /<img[\s>]/i.test(content);
    }

    function isStandaloneFormula(content) {
      var trimmed = String(content || "").trim();
      if (!trimmed) return false;
      return /^(?:\$\$[\s\S]*\$\$|\\\[[\s\S]*\\\]|\\begin\{(?:equation\*?|align\*?|aligned|gather\*?|multline\*?|array|cases)\}[\s\S]*\\end\{(?:equation\*?|align\*?|aligned|gather\*?|multline\*?|array|cases)\})$/.test(trimmed);
    }

    function isFormula(block) {
      var label = block.labelKey || "";
      if (label === "inline_formula") return false;
      return /^(?:formula|display_formula|equation|display_equation|latex)$/.test(label) ||
        isStandaloneFormula(block.content);
    }

    function isHeading(block) {
      return /^(?:title|doc_title|paragraph_title|heading|header|section_title|section_header|subheading)$/.test(
        block.labelKey || "");
    }

    function kindOf(block) {
      if (isTable(block)) return "table";
      if (isImage(block)) return "image";
      if (isFormula(block)) return "formula";
      if (isHeading(block)) return "heading";
      return "text";
    }

    function isLayoutOnly(block) {
      var raw = String(block.content || "");
      if (!raw.trim()) return !isImage(block);
      var label = block.labelKey || "";
      if (/^(?:formula_number|equation_number|eq_number|formula_no|equation_no)$/.test(label)) return true;
      if (/^(number|page_number|page_no|pagination|page_index)$/.test(label)) return true;
      if (/^(?:header|footer|page_header|page_footer|running_header|running_footer)$/.test(label)) return true;
      if (/^(noise|ignored|ignore|background|layout_only|watermark)$/.test(label)) return true;
      return !block.searchText && !isImage(block) && !isTable(block) && !isFormula(block);
    }

    function normalizeBlock(raw, index) {
      raw = raw || {};
      var id = String(raw.id || "");
      if (!id) id = "preview-block-" + (index + 1);
      var label = String(raw.label || "text");
      var content = String(raw.content || "");
      return {
        id: id,
        sequenceIndex: index,
        pageIndex: typeof raw.pageIndex === "number" ? raw.pageIndex : 0,
        order: typeof raw.order === "number" ? raw.order : index + 1,
        label: label,
        labelKey: normalizeLabel(label),
        displayLabel: String(raw.displayLabel || raw.label || "Text"),
        content: content,
        groupId: String(raw.groupId || ""),
        contentOwnerId: String(raw.contentOwnerId || id),
        searchText: normalizeText(content),
        visibleSourceContent: "",
        visibleSourceStart: -1,
        visibleSourceEnd: -1,
        edited: !!raw.edited,
        canRestoreOriginal: !!raw.canRestoreOriginal,
        editable: raw.editable !== false
      };
    }

    function normalizeBlocks(blocks) {
      if (!Array.isArray(blocks)) return [];
      return blocks.map(normalizeBlock).filter(function (block) { return !!block.id; });
    }

    function imageText(node) {
      var images = [];
      if (node.nodeName && node.nodeName.toLowerCase() === "img") images.push(node);
      else if (node.querySelectorAll) images = Array.prototype.slice.call(node.querySelectorAll("img"));
      return images.map(function (image) {
        return [
          image.getAttribute("alt") || "",
          image.getAttribute("title") || "",
          image.getAttribute("src") || ""
        ].join(" ");
      }).join(" ");
    }

    function extractImageSourceSegments(markdownText) {
      var text = String(markdownText || "");
      var segments = [];
      var markdownImage = /!\[[^\]]*\]\(\s*(?:<[^>\r\n]+>|[^\s)\r\n]+)(?:\s+["'][^"'\r\n]*["'])?\s*\)/g;
      var htmlImage = /<img\b[^>]*>/gi;
      var match;
      while ((match = markdownImage.exec(text))) {
        segments.push({ index: match.index, end: match.index + match[0].length, content: match[0],
          kind: "image", searchText: normalizeText(match[0]), used: false });
      }
      while ((match = htmlImage.exec(text))) {
        segments.push({ index: match.index, end: match.index + match[0].length, content: match[0],
          kind: "image", searchText: normalizeText(match[0]), used: false });
      }
      segments.sort(function (left, right) { return left.index - right.index; });
      segments.sourceText = text;
      return segments;
    }

    function extractSourceSegments(markdownText) {
      var normalized = String(markdownText || "").replace(/\r\n?/g, "\n");
      var lines = normalized.split("\n");
      var offsets = [0];
      for (var index = 0; index < lines.length; index++) offsets.push(offsets[index] + lines[index].length + 1);
      var tokens;
      try {
        tokens = markdown.parseSource(normalized, { diagramContext: markdown.createDiagramContext() });
      } catch (_) {
        return extractImageSourceSegments(normalized);
      }
      var blockTypes = {
        heading_open: true, paragraph_open: true, blockquote_open: true, bullet_list_open: true,
        ordered_list_open: true, table_open: true, fence: true, code_block: true, html_block: true
      };
      var seen = {};
      var segments = [];
      tokens.forEach(function (token) {
        if (!blockTypes[token.type] || !token.map || token.level !== 0) return;
        var startLine = token.map[0];
        var endLine = token.map[1];
        var key = startLine + ":" + endLine;
        if (seen[key] || startLine < 0 || endLine <= startLine) return;
        seen[key] = true;
        var start = offsets[Math.min(startLine, offsets.length - 1)] || 0;
        var end = offsets[Math.min(endLine, offsets.length - 1)] || normalized.length;
        var content = normalized.slice(start, end).replace(/\n+$/g, "");
        var kind = "text";
        if (token.type === "heading_open") kind = "heading";
        else if (token.type === "table_open" || /<table[\s>]/i.test(content)) kind = "table";
        else if (token.type === "fence" && markdown.diagramKindFromInfo(token.info)) kind = "diagram";
        else if (/!\[[^\]]*\]\([^)]+\)|<img[\s>]/i.test(content)) kind = "image";
        else if (isStandaloneFormula(content)) kind = "formula";
        else if (token.type === "fence" || token.type === "code_block") kind = "code";
        segments.push({ index: start, end: end, content: content, kind: kind,
          searchText: normalizeText(content), used: false });
      });
      segments.sort(function (left, right) { return left.index - right.index; });
      segments.sourceText = normalized;
      return segments;
    }

    function collectCandidates() {
      var selector = [
        "h1", "h2", "h3", "h4", "h5", "h6", "p", "blockquote", "pre", "ul", "ol", "dl",
        "figure", "details", ".table-scroll", ".math-display", ".diagram-block", "img"
      ].join(",");
      var nodes = Array.prototype.slice.call(root.querySelectorAll(selector));
      var htmlBlockTag = /^(?:div|section|article|header|footer|main|aside|nav|center|address)$/i;
      Array.prototype.slice.call(root.children || []).forEach(function (node) {
        if (!node || !htmlBlockTag.test(node.nodeName || "")) return;
        if (nodes.indexOf(node) !== -1) return;
        if (node.classList && node.classList.contains("ocr-preview-block-limit-notice")) return;
        if (node.querySelector && node.querySelector(selector)) return;
        if (!normalizeText(node.textContent || "")) return;
        nodes.push(node);
      });
      nodes.sort(function (left, right) {
        if (left === right) return 0;
        return left.compareDocumentPosition(right) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1;
      });
      var candidates = [];
      nodes.forEach(function (node) {
        if (!node || node.closest(".ocr-preview-editor")) return;
        if (node.nodeName && node.nodeName.toLowerCase() === "img" && node.closest("p,figure")) return;
        if (node.classList && node.classList.contains("math-display") && node.closest("p")) return;
        var ancestor = node.parentElement && node.parentElement.closest ? node.parentElement.closest(selector) : null;
        if (ancestor && root.contains(ancestor)) return;
        var text = (node.textContent || "") + " " + imageText(node);
        var searchText = normalizeText(text);
        var hasTable = !!(node.classList && node.classList.contains("table-scroll")) || !!(node.querySelector && node.querySelector("table"));
        var hasImage = (node.nodeName && node.nodeName.toLowerCase() === "img") || !!(node.querySelector && node.querySelector("img"));
        var hasFormula = !!(node.classList && node.classList.contains("math-node")) || !!(node.querySelector && node.querySelector(".math-node"));
        var hasDisplayFormula = !!(node.classList && node.classList.contains("math-display")) || !!(node.querySelector && node.querySelector(".math-display"));
        var hasDiagram = !!(node.classList && node.classList.contains("diagram-block"));
        var isHeadingNode = /^h[1-6]$/i.test(node.nodeName || "");
        var hasCode = /^pre$/i.test(node.nodeName || "");
        if (!searchText && !hasTable && !hasImage && !hasFormula && !hasDiagram && !hasCode) return;
        var kind = hasDiagram ? "diagram" : (hasTable ? "table" : (hasImage ? "image" :
          (hasDisplayFormula ? "formula" : (isHeadingNode ? "heading" : (hasCode ? "code" : "text")))));
        candidates.push({ node: node, searchText: searchText, kind: kind, used: false });
      });
      return candidates;
    }

    function tokenOverlapScore(blockText, candidateText) {
      var blockTokens = blockText.split(/\s+/).filter(function (token) { return token.length > 1; });
      var candidateTokens = candidateText.split(/\s+/).filter(function (token) { return token.length > 1; });
      if (!blockTokens.length || !candidateTokens.length) return 0;
      var seen = {};
      candidateTokens.forEach(function (token) { seen[token] = true; });
      var hits = 0;
      blockTokens.forEach(function (token) { if (seen[token]) hits += 1; });
      var ratio = hits / blockTokens.length;
      return ratio >= 0.6 ? Math.round(520 * ratio) : 0;
    }

    function textMatchScore(blockText, candidateText) {
      if (!blockText || !candidateText) return 0;
      if (blockText === candidateText) return 1000;
      if (candidateText.indexOf(blockText) !== -1) return 880 - Math.min(180, candidateText.length - blockText.length);
      if (blockText.indexOf(candidateText) !== -1 && candidateText.length >= Math.min(16, blockText.length)) {
        return 720 - Math.min(160, blockText.length - candidateText.length);
      }
      return tokenOverlapScore(blockText, candidateText);
    }

    function kindsCompatible(left, right) {
      return left === right || ((left === "text" || left === "heading") && (right === "text" || right === "heading"));
    }

    function candidateScore(candidate, segment, candidateIndex, segmentIndex) {
      if (!candidate || !segment) return 0;
      var compatible = kindsCompatible(candidate.kind, segment.kind);
      var structural = /^(?:table|image|formula|diagram|code)$/.test(candidate.kind) ||
        /^(?:table|image|formula|diagram|code)$/.test(segment.kind);
      if (!compatible && structural) return 0;
      var score = textMatchScore(candidate.searchText, segment.searchText);
      if (candidate.kind === segment.kind) score += candidate.kind === "text" ? 320 : 520;
      else if (compatible) score += 180;
      return score - Math.min(180, Math.abs(candidateIndex - segmentIndex) * 12);
    }

    function alignCandidates(candidates, segments) {
      var sourceCursor = 0;
      candidates.forEach(function (candidate, candidateIndex) {
        var remaining = candidates.length - candidateIndex;
        var surplus = Math.max(0, segments.length - sourceCursor - remaining);
        var maximum = Math.min(segments.length - 1, sourceCursor + Math.max(8, surplus + 2));
        var bestIndex = -1;
        var bestScore = 0;
        for (var segmentIndex = sourceCursor; segmentIndex <= maximum; segmentIndex++) {
          var score = candidateScore(candidate, segments[segmentIndex], candidateIndex, segmentIndex);
          if (segmentIndex === sourceCursor) score += 80;
          if (score > bestScore) {
            bestScore = score;
            bestIndex = segmentIndex;
          }
        }
        if (bestIndex < 0 || bestScore < 250) return;
        candidate.sourceSegment = segments[bestIndex];
        segments[bestIndex].candidate = candidate;
        sourceCursor = bestIndex + 1;
      });
    }

    function rangeContent(segments, startIndex, endIndex, sourceMarkdown) {
      if (startIndex < 0 || endIndex <= startIndex || startIndex >= segments.length) return "";
      var first = segments[startIndex];
      var last = segments[Math.min(endIndex, segments.length) - 1];
      var source = String(segments.sourceText || sourceMarkdown || "").replace(/\r\n?/g, "\n");
      if (typeof first.index === "number" && typeof last.end === "number" && last.end > first.index) {
        return source.slice(first.index, last.end).replace(/\n+$/g, "");
      }
      return segments.slice(startIndex, endIndex).map(function (segment) { return segment.content || ""; }).join("\n\n");
    }

    function rangeScore(block, segments, startIndex, endIndex, sourceMarkdown) {
      var range = segments.slice(startIndex, endIndex);
      if (!range.length || !range.some(function (segment) { return !!segment.candidate; })) return 0;
      var kind = kindOf(block);
      var kinds = {};
      range.forEach(function (segment) { kinds[segment.kind] = true; });
      if (kind !== "text" && kind !== "heading" && range.length !== 1) return 0;
      if (kind === "image" && !kinds.image) return 0;
      if (kind === "table" && !kinds.table) return 0;
      if (kind === "formula" && !kinds.formula) return 0;
      if (kind === "heading" && !kinds.heading) return 0;
      if (kind === "text" && (kinds.image || kinds.table || kinds.formula || kinds.diagram || kinds.code)) return 0;
      var textScore = textMatchScore(block.searchText, normalizeText(rangeContent(segments, startIndex, endIndex, sourceMarkdown)));
      if ((kind === "text" || kind === "heading") && !textScore) return 0;
      var score = textScore;
      if (kind === "text") {
        if (kinds.text || kinds.heading) score += 160;
      } else if (kinds[kind]) {
        score += 480;
      }
      if (!textScore && kind === "image" && kinds.image) score = Math.max(score, 700);
      if (!textScore && kind === "table" && kinds.table) score = Math.max(score, 620);
      if (!textScore && kind === "formula" && kinds.formula) score = Math.max(score, 620);
      return score - Math.max(0, range.length - 1) * 8;
    }

    function findBestRange(block, segments, sourceCursor, sourceMarkdown) {
      var best = null;
      var bestScore = 0;
      var maximumStart = Math.min(segments.length - 1, sourceCursor + 12);
      for (var startIndex = sourceCursor; startIndex <= maximumStart; startIndex++) {
        var maximumEnd = Math.min(segments.length, startIndex + 16);
        for (var endIndex = startIndex + 1; endIndex <= maximumEnd; endIndex++) {
          var score = rangeScore(block, segments, startIndex, endIndex, sourceMarkdown) - (startIndex - sourceCursor) * 24;
          if (score > bestScore) {
            bestScore = score;
            best = { start: startIndex, end: endIndex };
          }
        }
      }
      return best && bestScore >= 300 ? best : null;
    }

    function isFormulaNumber(block) {
      return /^(?:formula_number|equation_number|eq_number|formula_no|equation_no)$/.test(block && block.labelKey || "");
    }

    function map(blocks, sourceMarkdown) {
      var candidates = collectCandidates();
      if (!candidates.length) return { assignments: [], aliases: [] };
      var segments = extractSourceSegments(sourceMarkdown);
      if (!segments.length) return { assignments: [], aliases: [] };
      alignCandidates(candidates, segments);
      var orderedBlocks = blocks.slice();
      var byId = {};
      orderedBlocks.forEach(function (block) { byId[block.id] = block; });
      var assignments = [];
      var assignedNodes = {};
      var sourceCursor = 0;
      orderedBlocks.filter(function (block) { return !isLayoutOnly(block); }).forEach(function (block) {
        var range = findBestRange(block, segments, sourceCursor, sourceMarkdown);
        if (!range) return;
        var nodes = [];
        for (var segmentIndex = range.start; segmentIndex < range.end; segmentIndex++) {
          var candidate = segments[segmentIndex].candidate;
          if (!candidate || candidate.used) continue;
          candidate.used = true;
          nodes.push(candidate.node);
        }
        var sourceContent = rangeContent(segments, range.start, range.end, sourceMarkdown);
        var sourceStart = segments[range.start].index || 0;
        assignments.push({ block: block, nodes: nodes, sourceContent: sourceContent,
          sourceStart: sourceStart, sourceEnd: sourceStart + sourceContent.length });
        if (nodes.length) {
          assignedNodes[block.id] = nodes;
          sourceCursor = range.end;
        }
      });
      var aliases = [];
      orderedBlocks.forEach(function (block) {
        if (!block.contentOwnerId || block.contentOwnerId === block.id) return;
        var owner = byId[block.contentOwnerId];
        var ownerAssignment = assignments.filter(function (assignment) { return assignment.block === owner; })[0];
        if (!owner || owner.pageIndex !== block.pageIndex || !ownerAssignment) return;
        aliases.push({ block: block, nodes: assignedNodes[owner.id] || [],
          sourceContent: ownerAssignment.sourceContent, sourceStart: ownerAssignment.sourceStart,
          sourceEnd: ownerAssignment.sourceEnd });
      });
      orderedBlocks.forEach(function (block, blockIndex) {
        if (!isFormulaNumber(block)) return;
        var previous = blockIndex > 0 ? orderedBlocks[blockIndex - 1] : null;
        if (!previous || previous.pageIndex !== block.pageIndex || !isFormula(previous)) return;
        aliases.push({ block: block, nodes: assignedNodes[previous.id] || [] });
      });
      return { assignments: assignments, aliases: aliases };
    }

    return {
      normalizeBlocks: normalizeBlocks,
      normalizeText: normalizeText,
      kindOf: kindOf,
      map: map
    };
  }

  window.ZenCropPreviewBlocks = { createBlockMapper: createBlockMapper };
}());
