(function () {
  "use strict";

  var DOMPurify = window.DOMPurify;
  if (!DOMPurify) {
    throw new Error("Preview security requires DOMPurify.");
  }

  function resolveUrl(value) {
    try {
      return new URL(value, window.location.href);
    } catch (_) {
      return null;
    }
  }

  function isLoopbackOrInternalHost(hostname) {
    var host = String(hostname || "").toLowerCase().replace(/\.$/, "");
    if (!host) return true;
    if (host === "localhost" || host === "0.0.0.0" || host === "::1" || host === "[::1]") return true;
    if (/^(?:0|10|127)\./.test(host)) return true;
    if (/^169\.254\./.test(host)) return true;
    if (/^192\.168\./.test(host)) return true;
    var match = /^172\.(\d+)\./.exec(host);
    if (match) {
      var second = Number(match[1]);
      if (second >= 16 && second <= 31) return true;
    }
    return host.indexOf(":") !== -1;
  }

  function isPreviewAssetsHost(hostname) {
    var host = String(hostname || "").toLowerCase().replace(/\.$/, "");
    var current = String(window.location.hostname || "").toLowerCase().replace(/\.$/, "");
    return host === current && /^zencrop-assets-[a-f0-9]{32}\.invalid$/.test(host);
  }

  function isSafeImageSrc(value) {
    var url = resolveUrl(value);
    if (!url) return false;
    if (url.protocol === "https:" && isPreviewAssetsHost(url.hostname)) return true;
    if (url.protocol === "https:" && url.hostname === "zencrop-ocr-images.invalid") return true;
    if (url.protocol === "https:" && url.hostname === "zencrop-preview-output.invalid") return true;
    if ((url.protocol === "http:" || url.protocol === "https:") && !isLoopbackOrInternalHost(url.hostname)) return true;
    if (url.protocol === "data:") {
      return /^data:image\/(?:png|jpe?g|gif|webp|avif);base64,[a-z0-9+/=\s]+$/i.test(value) && value.length < 2 * 1024 * 1024;
    }
    return false;
  }

  function isManagedPreviewImageUrl(value) {
    var url = resolveUrl(value);
    if (!url || url.protocol !== "https:") return false;
    return isPreviewAssetsHost(url.hostname) ||
      url.hostname === "zencrop-ocr-images.invalid" ||
      url.hostname === "zencrop-preview-output.invalid";
  }

  function isSafeLinkHref(value) {
    var url = resolveUrl(value);
    if (!url) return false;
    if (url.protocol !== "http:" && url.protocol !== "https:") return false;
    if (isPreviewAssetsHost(url.hostname)) return false;
    if (url.hostname === "zencrop-ocr-images.invalid") return false;
    return !isLoopbackOrInternalHost(url.hostname);
  }

  function isSafeInteger(value, maxValue) {
    var text = String(value || "").trim();
    if (!/^[0-9]{1,4}$/.test(text)) return false;
    return Number(text) <= maxValue;
  }

  function isSafeTokenList(value) {
    return /^[a-z0-9 _:-]{0,256}$/i.test(String(value || ""));
  }

  function isSafeDimension(value) {
    var text = String(value || "").trim().toLowerCase();
    if (!text) return false;
    if (text === "auto" || text === "0") return true;
    if (/^[1-9][0-9]{0,3}$/.test(text)) return Number(text) <= 4096;
    var match = /^([0-9]{1,4}(?:\.[0-9]{1,2})?)(px|em|rem|%)$/.exec(text);
    if (!match) return false;
    var number = Number(match[1]);
    var unit = match[2];
    if (unit === "%") return number >= 0 && number <= 100;
    if (unit === "px") return number >= 0 && number <= 4096;
    return number >= 0 && number <= 200;
  }

  function isSafeLengthList(value, maxParts) {
    var parts = String(value || "").trim().split(/\s+/);
    if (!parts.length || parts.length > maxParts) return false;
    return parts.every(isSafeDimension);
  }

  function isSafeCssColor(value) {
    var text = String(value || "").trim();
    if (/^#[0-9a-f]{3,8}$/i.test(text)) return true;
    if (/^[a-z]{3,24}$/i.test(text)) return !/^(expression|url|import|behavior)$/i.test(text);
    if (/^rgba?\(\s*(?:[0-9]{1,3}%?\s*,\s*){2}[0-9]{1,3}%?(?:\s*,\s*(?:0|1|0?\.[0-9]+))?\s*\)$/i.test(text)) return true;
    if (/^hsla?\(\s*[0-9]{1,3}(?:deg)?\s*,\s*[0-9]{1,3}%\s*,\s*[0-9]{1,3}%(?:\s*,\s*(?:0|1|0?\.[0-9]+))?\s*\)$/i.test(text)) return true;
    return false;
  }

  function isSafeBorder(value) {
    var text = String(value || "").trim();
    if (!text || /[<>\\]/.test(text)) return false;
    if (/url\s*\(|expression\s*\(/i.test(text)) return false;
    return /^(?:none|0|[0-9]{1,2}px\s+(?:solid|dashed|dotted|double)\s+(?:#[0-9a-f]{3,8}|[a-z]{3,24}|rgba?\([^)]+\)|hsla?\([^)]+\)))$/i.test(text);
  }

  function sanitizeStyle(value) {
    var declarations = String(value || "").split(";");
    var cleaned = [];
    declarations.forEach(function (declaration) {
      var colon = declaration.indexOf(":");
      if (colon === -1) return;
      var property = declaration.slice(0, colon).trim().toLowerCase();
      var cssValue = declaration.slice(colon + 1).trim();
      var lowerValue = cssValue.toLowerCase();
      if (!property || !cssValue) return;
      if (/[<>\\]/.test(cssValue)) return;
      if (/url\s*\(|expression\s*\(|@import|behavior\s*:|-moz-binding|javascript\s*:|vbscript\s*:|data\s*:/i.test(cssValue)) return;

      var keep = false;
      switch (property) {
        case "text-align":
          keep = /^(left|right|center|justify|start|end)$/i.test(cssValue);
          break;
        case "vertical-align":
          keep = /^(baseline|sub|super|top|text-top|middle|bottom|text-bottom)$/i.test(cssValue) || isSafeDimension(cssValue);
          break;
        case "word-wrap":
        case "overflow-wrap":
          keep = /^(normal|break-word|anywhere)$/i.test(cssValue);
          break;
        case "word-break":
          keep = /^(normal|break-all|keep-all|break-word)$/i.test(cssValue);
          break;
        case "white-space":
          keep = /^(normal|nowrap|pre|pre-wrap|pre-line|break-spaces)$/i.test(cssValue);
          break;
        case "width":
        case "min-width":
        case "max-width":
        case "height":
        case "min-height":
        case "max-height":
        case "font-size":
          keep = isSafeDimension(cssValue) || /^(xx-small|x-small|small|medium|large|x-large|xx-large|smaller|larger)$/i.test(cssValue);
          break;
        case "margin":
        case "padding":
          keep = isSafeLengthList(cssValue, 4);
          break;
        case "margin-top":
        case "margin-right":
        case "margin-bottom":
        case "margin-left":
        case "padding-top":
        case "padding-right":
        case "padding-bottom":
        case "padding-left":
        case "border-spacing":
          keep = isSafeLengthList(cssValue, property === "border-spacing" ? 2 : 1);
          break;
        case "color":
        case "background-color":
          keep = isSafeCssColor(cssValue);
          break;
        case "font-weight":
          keep = /^(normal|bold|bolder|lighter|[1-9]00)$/i.test(cssValue);
          break;
        case "font-style":
          keep = /^(normal|italic|oblique)$/i.test(cssValue);
          break;
        case "text-decoration":
          keep = /^(none|underline|overline|line-through)(\s+(underline|overline|line-through))*$/i.test(cssValue);
          break;
        case "border":
        case "border-top":
        case "border-right":
        case "border-bottom":
        case "border-left":
          keep = isSafeBorder(cssValue);
          break;
        case "border-collapse":
          keep = /^(collapse|separate)$/i.test(cssValue);
          break;
        case "display":
          keep = /^(block|inline|inline-block|table|table-row|table-cell)$/i.test(cssValue);
          break;
        case "caption-side":
          keep = /^(top|bottom)$/i.test(cssValue);
          break;
        case "list-style-type":
          keep = /^(disc|circle|square|decimal|decimal-leading-zero|lower-alpha|upper-alpha|lower-roman|upper-roman|none)$/i.test(cssValue);
          break;
        case "object-fit":
          keep = /^(contain|cover|fill|none|scale-down)$/i.test(cssValue);
          break;
        default:
          keep = false;
      }

      if (keep) cleaned.push(property + ": " + lowerValue);
    });
    return cleaned.join("; ");
  }

  DOMPurify.addHook("uponSanitizeAttribute", function (node, data) {
    var name = data.attrName ? data.attrName.toLowerCase() : "";
    if (name === "target") {
      data.keepAttr = false;
      return;
    }
    if (node && node.nodeName === "IMG" && name === "src" && !isSafeImageSrc(data.attrValue)) {
      data.keepAttr = false;
      return;
    }
    if (node && node.nodeName === "A" && name === "href" && !isSafeLinkHref(data.attrValue)) {
      data.keepAttr = false;
      return;
    }
    if (name === "style") {
      var cleanedStyle = sanitizeStyle(data.attrValue);
      if (cleanedStyle) {
        data.attrValue = cleanedStyle;
      } else {
        data.keepAttr = false;
      }
      return;
    }
    if ((name === "rowspan" || name === "colspan") && !/^[1-9][0-9]?$|^100$/.test(data.attrValue || "")) {
      data.keepAttr = false;
      return;
    }
    if ((name === "width" || name === "height") && !isSafeDimension(data.attrValue)) {
      data.keepAttr = false;
      return;
    }
    if ((name === "border" || name === "cellpadding" || name === "cellspacing") && !isSafeInteger(data.attrValue, 100)) {
      data.keepAttr = false;
      return;
    }
    if ((name === "start" || name === "value") && !isSafeInteger(data.attrValue, 9999)) {
      data.keepAttr = false;
      return;
    }
    if ((name === "align" || name === "valign") && !/^(left|right|center|justify|top|middle|bottom|baseline)$/i.test(data.attrValue || "")) {
      data.keepAttr = false;
      return;
    }
    if ((name === "class" || name === "id" || name === "name" || name === "role") && !isSafeTokenList(data.attrValue)) {
      data.keepAttr = false;
      return;
    }
    if (name === "aria-hidden" && !/^(true|false)$/i.test(data.attrValue || "")) {
      data.keepAttr = false;
    }
  });

  function sanitizeHtml(html) {
    return DOMPurify.sanitize(html, {
      ALLOWED_TAGS: [
        "p", "br", "hr",
        "h1", "h2", "h3", "h4", "h5", "h6",
        "strong", "b", "em", "i", "del", "s", "strike", "u", "ins",
        "code", "pre", "blockquote", "q", "cite",
        "ul", "ol", "li", "dl", "dt", "dd",
        "table", "caption", "colgroup", "col", "thead", "tbody", "tfoot", "tr", "th", "td",
        "img", "a", "span", "div", "section", "article", "header", "footer", "main", "aside", "nav",
        "figure", "figcaption", "details", "summary",
        "mark", "small", "sub", "sup", "kbd", "samp", "var", "abbr", "dfn", "time", "address",
        "center", "ruby", "rt", "rp", "wbr"
      ],
      ALLOWED_ATTR: [
        "href", "title", "src", "alt",
        "colspan", "rowspan", "align", "valign",
        "width", "height", "border", "cellpadding", "cellspacing",
        "start", "reversed", "type", "value", "open",
        "scope", "headers", "cite", "datetime",
        "class", "id", "name", "role", "aria-label", "aria-hidden",
        "style"
      ],
      FORBID_TAGS: [
        "script", "style", "iframe", "object", "embed",
        "form", "input", "button", "textarea", "select",
        "svg", "math", "audio", "video", "canvas", "map", "area", "link", "meta"
      ]
    });
  }

  function sanitizeSvg(svg) {
    return DOMPurify.sanitize(svg, {
      USE_PROFILES: { svg: true, svgFilters: true }
    });
  }

  function wrapTables(root) {
    var tables = Array.prototype.slice.call(root.querySelectorAll("table"));
    tables.forEach(function (table) {
      if (table.parentElement && table.parentElement.classList.contains("table-scroll")) return;
      var wrapper = document.createElement("div");
      wrapper.className = "table-scroll";
      table.parentNode.insertBefore(wrapper, table);
      wrapper.appendChild(table);
    });
  }

  function applyHtmlCompatibility(root) {
    var images = Array.prototype.slice.call(root.querySelectorAll("img"));
    images.forEach(function (image) {
      var width = image.getAttribute("width");
      var height = image.getAttribute("height");
      if (width && isSafeDimension(width)) {
        image.style.width = /^\d+$/.test(width) ? width + "px" : width;
      }
      if (height && isSafeDimension(height)) {
        image.style.height = /^\d+$/.test(height) ? height + "px" : height;
      }
    });
  }

  function observeRenderedImages(root, generation, recordId, renderToken, isCurrentGeneration, post) {
    Array.prototype.forEach.call(root.querySelectorAll("img"), function (image) {
      var reported = false;
      var reportFailure = function () {
        if (reported || !isCurrentGeneration(generation) || !image.isConnected) return;
        var src = image.currentSrc || image.src || image.getAttribute("src") || "";
        if (!isManagedPreviewImageUrl(src)) return;
        reported = true;
        post({
          type: "previewImageError",
          recordId: recordId,
          renderToken: renderToken,
          src: src
        });
      };
      image.addEventListener("error", reportFailure, { once: true });
      if (image.complete && image.naturalWidth === 0) {
        window.setTimeout(reportFailure, 0);
      }
    });
  }

  window.ZenCropPreviewSecurity = {
    resolveUrl: resolveUrl,
    isSafeImageSrc: isSafeImageSrc,
    isSafeLinkHref: isSafeLinkHref,
    sanitizeHtml: sanitizeHtml,
    sanitizeSvg: sanitizeSvg,
    applyHtmlCompatibility: applyHtmlCompatibility,
    wrapTables: wrapTables,
    observeRenderedImages: observeRenderedImages
  };
}());
