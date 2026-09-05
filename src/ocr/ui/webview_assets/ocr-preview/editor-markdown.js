(function () {
  "use strict";

  var tableAsset = window.ZenCropPreviewEditorTable;

  function canSerialize(editorBody) {
    if (!editorBody || !editorBody.querySelectorAll || !tableAsset ||
        typeof tableAsset.canSerialize !== "function") return false;
    if (!Array.prototype.slice.call(editorBody.querySelectorAll("table")).every(tableAsset.canSerialize)) return false;
    var allowed = /^(a|b|blockquote|br|code|del|div|em|h[1-6]|hr|i|ins|kbd|li|mark|ol|p|pre|s|samp|small|strike|strong|sub|sup|table|tbody|td|th|thead|tr|u|ul|var)$/;
    return Array.prototype.slice.call(editorBody.querySelectorAll("*")).every(function (element) {
      var tag = element.nodeName.toLowerCase();
      if (!allowed.test(tag)) return false;
      var attributes = Array.prototype.slice.call(element.attributes || []).map(function (attribute) {
        return attribute.name.toLowerCase();
      });
      if (tag === "a") return attributes.every(function (name) { return name === "href"; });
      if (tag === "ol") return attributes.every(function (name) { return name === "start"; });
      if (tag === "div" && element.classList.contains("table-scroll")) {
        return attributes.length === 1 && attributes[0] === "class";
      }
      if (tag === "th" || tag === "td") {
        return attributes.every(function (name) {
          if (name === "contenteditable" || name === "tabindex") return true;
          if (name === "class") return !element.getAttribute(name) ||
            /^(?:is-active|is-range-selected)(?:\s+(?:is-active|is-range-selected))*$/.test(element.getAttribute(name));
          if (name === "align") return /^(left|center|right)$/.test(element.getAttribute(name) || "");
          return name === "style" &&
            /^\s*text-align\s*:\s*(left|center|right)\s*;?\s*$/i.test(element.getAttribute(name) || "");
        });
      }
      if (tag === "code" && element.parentElement &&
          element.parentElement.nodeName.toLowerCase() === "pre") {
        return attributes.every(function (name) { return name === "class"; }) &&
          (!element.className || /^language-[a-z0-9_+.-]+$/i.test(element.className));
      }
      return attributes.length === 0;
    });
  }

  function serialize(editorBody) {
    function escapeText(text) {
      return String(text || "").replace(/\u00a0/g, " ")
        .replace(/([\\`*_[\]{}()#+\-.!>])/g, "\\$1");
    }

    function inlineCode(text) {
      text = String(text || "");
      var longest = 0;
      text.replace(/`+/g, function (run) { longest = Math.max(longest, run.length); return run; });
      var fence = new Array(longest + 2).join("`");
      var padded = /^`|`$|^\s|\s$/.test(text);
      return fence + (padded ? " " : "") + text + (padded ? " " : "") + fence;
    }

    function destination(value) {
      return "<" + String(value || "").replace(/\\/g, "%5C").replace(/</g, "%3C")
        .replace(/>/g, "%3E").replace(/\r/g, "%0D").replace(/\n/g, "%0A") + ">";
    }

    function inline(node) {
      if (!node) return "";
      if (node.nodeType === Node.TEXT_NODE) return escapeText(node.nodeValue || "");
      if (node.nodeType !== Node.ELEMENT_NODE) return "";
      var tag = node.nodeName.toLowerCase();
      if (tag === "br") return "  \n";
      if (tag === "strong" || tag === "b") return "**" + children(node) + "**";
      if (tag === "em" || tag === "i") return "*" + children(node) + "*";
      if (tag === "s" || tag === "strike" || tag === "del") return "~~" + children(node) + "~~";
      if (tag === "code") return inlineCode(node.textContent || "");
      if (tag === "a") {
        var label = children(node) || (node.textContent || "");
        var href = node.getAttribute("href") || "";
        return href ? "[" + label + "](" + destination(href) + ")" : label;
      }
      if (tag === "table") return "\n" + tableAsset.serialize(node, children);
      if (tag === "u" || tag === "ins" || tag === "mark" || tag === "sub" || tag === "sup" ||
          tag === "kbd" || tag === "samp" || tag === "var" || tag === "small") {
        return "<" + tag + ">" + children(node) + "</" + tag + ">";
      }
      if (tag === "ul" || tag === "ol") return "\n" + list(node);
      return children(node);
    }

    function children(node) {
      var out = "";
      Array.prototype.slice.call(node.childNodes || []).forEach(function (child) { out += inline(child); });
      return out;
    }

    function listItem(item) {
      var content = "";
      var nestedLists = [];
      Array.prototype.slice.call(item.childNodes || []).forEach(function (child) {
        var tag = child.nodeType === Node.ELEMENT_NODE ? child.nodeName.toLowerCase() : "";
        if (tag === "ul" || tag === "ol") { nestedLists.push(child); return; }
        if (/^(p|div|pre|blockquote|hr)$/.test(tag)) {
          var text = block(child).trim();
          if (text) content += (content.trim() ? "\n\n" : "") + text;
          return;
        }
        content += inline(child);
      });
      return { content: content.trim(), nestedLists: nestedLists };
    }

    function list(node) {
      var ordered = node.nodeName && node.nodeName.toLowerCase() === "ol";
      var start = ordered && node.hasAttribute("start") ? Number(node.getAttribute("start")) : 1;
      if (!Number.isFinite(start)) start = 1;
      var items = Array.prototype.slice.call(node.children || []).filter(function (child) {
        return child.nodeName && child.nodeName.toLowerCase() === "li";
      });
      return items.map(function (item, index) {
        var parts = listItem(item);
        var marker = ordered ? String(start + index) + ". " : "- ";
        var continuation = new Array(marker.length + 1).join(" ");
        var lines = (parts.content || "").split("\n");
        var result = marker + (lines.shift() || "");
        lines.forEach(function (line) { result += "\n" + continuation + line; });
        parts.nestedLists.forEach(function (nested) {
          result += "\n" + list(nested).split("\n").map(function (line) { return "    " + line; }).join("\n");
        });
        return result;
      }).join("\n");
    }

    function block(node) {
      if (node.nodeType === Node.TEXT_NODE) return escapeText(node.nodeValue || "");
      if (node.nodeType !== Node.ELEMENT_NODE) return "";
      var tag = node.nodeName.toLowerCase();
      if (/^h[1-6]$/.test(tag)) return new Array(Number(tag.slice(1)) + 1).join("#") + " " + children(node).trim();
      if (tag === "hr") return "---";
      if (tag === "ul" || tag === "ol") return list(node);
      if (tag === "table") return tableAsset.serialize(node, children);
      if (tag === "div" && node.classList.contains("table-scroll")) {
        var wrappedTable = node.querySelector(":scope > table");
        return wrappedTable ? tableAsset.serialize(wrappedTable, children) : "";
      }
      if (tag === "pre") {
        var code = node.querySelector ? node.querySelector("code") : null;
        var source = ((code ? code.textContent : node.textContent) || "").replace(/\s+$/g, "");
        var longestFence = 2;
        source.replace(/`+/g, function (run) { longestFence = Math.max(longestFence, run.length); return run; });
        var fence = new Array(longestFence + 2).join("`");
        var language = "";
        if (code) {
          var match = /(?:^|\s)language-([^\s]+)/.exec(code.className || "");
          if (match) language = match[1].replace(/[^a-z0-9_+.-]/gi, "");
        }
        return fence + language + "\n" + source + "\n" + fence;
      }
      if (tag === "blockquote") {
        var quoteParts = [];
        var inlinePart = "";
        Array.prototype.slice.call(node.childNodes || []).forEach(function (child) {
          var childTag = child.nodeType === Node.ELEMENT_NODE ? child.nodeName.toLowerCase() : "";
          if (/^(blockquote|div|h[1-6]|hr|ol|p|pre|ul)$/.test(childTag)) {
            if (inlinePart.trim()) quoteParts.push(inlinePart.trim());
            inlinePart = "";
            var childBlock = block(child).trim();
            if (childBlock) quoteParts.push(childBlock);
          } else {
            inlinePart += inline(child);
          }
        });
        if (inlinePart.trim()) quoteParts.push(inlinePart.trim());
        return quoteParts.join("\n\n").split(/\n/).map(function (line) {
          return line ? "> " + line : ">";
        }).join("\n");
      }
      return children(node).trim();
    }

    var blocks = Array.prototype.slice.call(editorBody.childNodes || []).map(block).filter(function (text) {
      return String(text || "").trim();
    });
    if (!blocks.length) return editorBody.innerText || "";
    return blocks.join("\n\n").replace(/\n{3,}/g, "\n\n").trim();
  }

  window.ZenCropPreviewEditorMarkdown = {
    serialize: serialize,
    canSerialize: canSerialize,
    destination: function (value) {
      return "<" + String(value || "").replace(/\\/g, "%5C").replace(/</g, "%3C")
        .replace(/>/g, "%3E").replace(/\r/g, "%0D").replace(/\n/g, "%0A") + ">";
    }
  };
}());
