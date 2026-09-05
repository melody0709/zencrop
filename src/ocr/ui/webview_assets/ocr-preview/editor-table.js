(function () {
  "use strict";

  function tableFrom(node) {
    if (node && node.nodeType === Node.TEXT_NODE) node = node.parentNode;
    return node && node.closest ? node.closest("table") : null;
  }

  function cellFrom(node) {
    if (node && node.nodeType === Node.TEXT_NODE) node = node.parentNode;
    return node && node.closest ? node.closest("th,td") : null;
  }

  function rows(table) {
    return table ? Array.prototype.slice.call(table.rows || []) : [];
  }

  function cells(row) {
    return row ? Array.prototype.slice.call(row.cells || []) : [];
  }

  function dimensions(table) {
    var allRows = rows(table);
    return {
      rows: allRows.length,
      columns: allRows.reduce(function (maximum, row) {
        return Math.max(maximum, row.cells.length);
      }, 0)
    };
  }

  function create(rowsCount, columnsCount) {
    rowsCount = Math.max(1, Math.min(20, Number(rowsCount) || 3));
    columnsCount = Math.max(1, Math.min(20, Number(columnsCount) || 3));
    var table = document.createElement("table");
    var thead = document.createElement("thead");
    var tbody = document.createElement("tbody");
    table.appendChild(thead);
    table.appendChild(tbody);
    for (var rowIndex = 0; rowIndex < rowsCount; rowIndex++) {
      var row = document.createElement("tr");
      for (var columnIndex = 0; columnIndex < columnsCount; columnIndex++) {
        var cell = document.createElement(rowIndex === 0 ? "th" : "td");
        cell.appendChild(document.createElement("br"));
        row.appendChild(cell);
      }
      (rowIndex === 0 ? thead : tbody).appendChild(row);
    }
    return table;
  }

  function ensureBody(table) {
    var body = table.tBodies && table.tBodies[0];
    if (!body) {
      body = document.createElement("tbody");
      table.appendChild(body);
    }
    return body;
  }

  function makeCell(header) {
    var cell = document.createElement(header ? "th" : "td");
    cell.appendChild(document.createElement("br"));
    return cell;
  }

  function rowIndexOf(table, cell) {
    var allRows = rows(table);
    return cell && cell.parentElement ? allRows.indexOf(cell.parentElement) : -1;
  }

  function columnIndexOf(cell) {
    return cell ? cell.cellIndex : -1;
  }

  function insertRow(table, cell, after) {
    var size = dimensions(table);
    if (!size.columns) return null;
    var activeRow = rowIndexOf(table, cell);
    var insertIndex = activeRow < 1 ? 1 : activeRow + (after ? 1 : 0);
    insertIndex = Math.max(1, Math.min(insertIndex, size.rows));
    var row = document.createElement("tr");
    for (var i = 0; i < size.columns; i++) row.appendChild(makeCell(false));
    var body = ensureBody(table);
    var reference = rows(table)[insertIndex];
    body.insertBefore(row, reference && reference.parentElement === body ? reference : null);
    return row.cells[Math.max(0, Math.min(columnIndexOf(cell), size.columns - 1))] || row.cells[0];
  }

  function deleteRow(table, cell) {
    var allRows = rows(table);
    var index = rowIndexOf(table, cell);
    if (index < 1 || allRows.length <= 1) return null;
    allRows[index].remove();
    allRows = rows(table);
    var next = allRows[Math.min(index, allRows.length - 1)] || allRows[0];
    return next && next.cells[Math.max(0, Math.min(columnIndexOf(cell), next.cells.length - 1))];
  }

  function insertColumn(table, cell, after) {
    var column = Math.max(0, columnIndexOf(cell) + (after ? 1 : 0));
    var selected = null;
    var activeRow = rowIndexOf(table, cell);
    rows(table).forEach(function (row, rowIndex) {
      var newCell = makeCell(rowIndex === 0);
      row.insertBefore(newCell, row.cells[column] || null);
      if (rowIndex === activeRow) selected = newCell;
    });
    return selected;
  }

  function deleteColumn(table, cell) {
    var size = dimensions(table);
    if (size.columns <= 1) return null;
    var column = Math.max(0, columnIndexOf(cell));
    var activeRow = rowIndexOf(table, cell);
    rows(table).forEach(function (row) {
      if (row.cells[column]) row.deleteCell(column);
    });
    var targetRow = rows(table)[Math.max(0, Math.min(activeRow, rows(table).length - 1))];
    return targetRow && targetRow.cells[Math.min(column, targetRow.cells.length - 1)];
  }

  function setAlignment(table, cell, alignment) {
    var column = columnIndexOf(cell);
    if (column < 0 || !/^(left|center|right)$/.test(alignment)) return;
    rows(table).forEach(function (row) {
      if (row.cells[column]) row.cells[column].style.textAlign = alignment;
    });
  }

  function alignment(cell) {
    var value = cell && (cell.style.textAlign || cell.getAttribute("align") || "");
    return /^(left|center|right)$/.test(value) ? value : "";
  }

  function canSerialize(table) {
    if (!table || table.nodeName.toLowerCase() !== "table") return false;
    var size = dimensions(table);
    if (!size.rows || !size.columns) return false;
    return rows(table).every(function (row, rowIndex) {
      if (row.cells.length !== size.columns) return false;
      return cells(row).every(function (cell) {
        var tag = cell.nodeName.toLowerCase();
        if ((rowIndex === 0 && tag !== "th") || (rowIndex > 0 && tag !== "td")) return false;
        if (Number(cell.rowSpan || 1) !== 1 || Number(cell.colSpan || 1) !== 1) return false;
        return Array.prototype.slice.call(cell.attributes || []).every(function (attribute) {
          var name = attribute.name.toLowerCase();
          if (name === "contenteditable" || name === "tabindex") return true;
          if (name === "class") return !attribute.value ||
            /^(?:is-active|is-range-selected)(?:\s+(?:is-active|is-range-selected))*$/.test(attribute.value);
          if (name === "align") return /^(left|center|right)$/.test(attribute.value || "");
          return name === "style" && /^\s*text-align\s*:\s*(left|center|right)\s*;?\s*$/i.test(attribute.value || "");
        });
      });
    });
  }

  function serialize(table, serializeCell) {
    if (!canSerialize(table) || typeof serializeCell !== "function") return "";
    var allRows = rows(table);
    var size = dimensions(table);
    function content(cell) {
      if (!String(cell.textContent || "").trim() && !cell.querySelector("img,table")) return "";
      return String(serializeCell(cell) || "").replace(/\s+$/g, "")
        .replace(/\|/g, "\\|").replace(/\n+/g, "<br>").trim();
    }
    var output = allRows.map(function (row) {
      var values = [];
      for (var column = 0; column < size.columns; column++) values.push(content(row.cells[column]));
      return "| " + values.join(" | ") + " |";
    });
    var alignments = cells(allRows[0]).map(alignment).map(function (value) {
      if (value === "left") return ":---";
      if (value === "center") return ":---:";
      if (value === "right") return "---:";
      return "---";
    });
    output.splice(1, 0, "| " + alignments.join(" | ") + " |");
    return output.join("\n");
  }

  function rangeCells(table, first, last) {
    var firstRow = rowIndexOf(table, first);
    var lastRow = rowIndexOf(table, last);
    var firstColumn = columnIndexOf(first);
    var lastColumn = columnIndexOf(last);
    if (firstRow < 0 || lastRow < 0 || firstColumn < 0 || lastColumn < 0) return [];
    var top = Math.min(firstRow, lastRow);
    var bottom = Math.max(firstRow, lastRow);
    var left = Math.min(firstColumn, lastColumn);
    var right = Math.max(firstColumn, lastColumn);
    var selected = [];
    rows(table).forEach(function (row, rowIndex) {
      if (rowIndex < top || rowIndex > bottom) return;
      cells(row).forEach(function (cell, columnIndex) {
        if (columnIndex >= left && columnIndex <= right) selected.push(cell);
      });
    });
    return selected;
  }

  function adjacentCell(table, cell, step) {
    var all = [];
    rows(table).forEach(function (row) { cells(row).forEach(function (item) { all.push(item); }); });
    var index = all.indexOf(cell);
    if (index === -1) return null;
    return all[index + step] || null;
  }

  window.ZenCropPreviewEditorTable = {
    tableFrom: tableFrom,
    cellFrom: cellFrom,
    dimensions: dimensions,
    create: create,
    insertRow: insertRow,
    deleteRow: deleteRow,
    insertColumn: insertColumn,
    deleteColumn: deleteColumn,
    setAlignment: setAlignment,
    alignment: alignment,
    canSerialize: canSerialize,
    serialize: serialize,
    rangeCells: rangeCells,
    adjacentCell: adjacentCell,
    rowIndexOf: rowIndexOf
  };
}());
