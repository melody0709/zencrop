(() => {
  "use strict";
  const leaseMs = 30000;
  let active = null;
  let observer = null;
  let expiry = 0;
  const shadowRoots = new Set();
  const finiteRect = r => [r.left, r.top, r.right, r.bottom].every(Number.isFinite);
  const contains = (r, x, y) => r[0] <= x && r[1] <= y && r[2] > x && r[3] > y;
  const parentOf = e => e.assignedSlot || e.parentElement || e.getRootNode()?.host || null;
  const viewportSize = () => active?.viewportSize || [innerWidth, innerHeight];
  const clippedRect = e => {
    const r = e.getBoundingClientRect();
    const [width, height] = viewportSize();
    return finiteRect(r) ? [Math.max(0, r.left), Math.max(0, r.top), Math.min(width, r.right), Math.min(height, r.bottom)] : null;
  };

  function end() {
    clearTimeout(expiry);
    observer?.disconnect();
    observer = null;
    for (const root of shadowRoots) root.removeEventListener("scroll", invalidate, true);
    shadowRoots.clear();
    removeEventListener("scroll", invalidate, true);
    removeEventListener("resize", invalidate);
    removeEventListener("pagehide", invalidate);
    visualViewport?.removeEventListener("resize", invalidate);
    visualViewport?.removeEventListener("scroll", invalidate);
    active = null;
  }

  function invalidate() {
    if (!active) return;
    const sessionId = active.sessionId;
    const frozen = active.frozen;
    end();
    chrome.runtime.sendMessage({type: "invalidated", sessionId, frozen}).catch(() => {});
  }

  function checkGeometry() {
    const hit = active?.hit;
    if (!hit) return;
    // Dynamic widgets elsewhere must not end selection when the hit and its candidate geometry are unchanged.
    if (hitElement(...hit.point) !== hit.element || hit.entries.some(({element, rect}) => {
      const current = element.isConnected ? clippedRect(element) : null;
      return !current || current.some((v, i) => Math.abs(v - rect[i]) > 0.01);
    })) { invalidate(); return; }
    if (hit.frame) {
      try {
        if (frameMatrix(hit.element).some((v, i) => Math.abs(v - hit.frame[i]) > 0.01)) invalidate();
      } catch { invalidate(); }
    }
  }

  function begin(message) {
    if (active?.sessionId !== message.sessionId) {
      end();
      active = {sessionId: message.sessionId, frozen: message.frozen};
      observer = new MutationObserver(checkGeometry);
      observer.observe(document, {childList: true, subtree: true, attributes: true, characterData: true});
      addEventListener("scroll", invalidate, {capture: true, passive: true});
      addEventListener("resize", invalidate);
      addEventListener("pagehide", invalidate);
      visualViewport?.addEventListener("resize", invalidate);
      visualViewport?.addEventListener("scroll", invalidate);
    }
    clearTimeout(expiry);
    expiry = setTimeout(invalidate, leaseMs);
  }

  function candidates(element, x, y) {
    const path = [];
    const [width, height] = viewportSize();
    const entries = [];
    let page = false;
    for (let e = element, depth = 0; e && depth < 256 && path.length < 64; e = parentOf(e), ++depth) {
      const r = e.getBoundingClientRect();
      if (!finiteRect(r)) continue;
      const rect = [Math.max(0, r.left), Math.max(0, r.top), Math.min(width, r.right), Math.min(height, r.bottom)];
      if (!contains(rect, x, y) || (rect[2] - rect[0]) * (rect[3] - rect[1]) <= 25) continue;
      const previous = path.at(-1);
      if (previous && (rect[0] > previous[0] || rect[1] > previous[1] || rect[2] < previous[2] || rect[3] < previous[3])) continue;
      if (!previous || rect.some((v, i) => Math.abs(v - previous[i]) > 0.5)) {
        path.push(rect);
        entries.push({element: e, rect});
      }
      if (e === document.documentElement && r.left <= 0 && r.top <= 0 &&
          // clientWidth/clientHeight round to integers while DOMRect keeps fractional CSS pixels.
          r.right + 0.5 >= e.clientWidth && r.bottom + 0.5 >= e.clientHeight) page = true;
    }
    return {candidates: path, page, entries};
  }

  function frameMatrix(frame) {
    const rect = frame.getBoundingClientRect();
    let matrix = new DOMMatrix();
    for (let e = frame; e; e = parentOf(e)) {
      const transform = getComputedStyle(e).transform;
      const local = new DOMMatrix(transform === "none" ? undefined : transform);
      if (!local.is2D) throw new Error("unsupported_frame_transform");
      matrix = local.multiply(matrix);
    }
    const w = frame.offsetWidth, h = frame.offsetHeight;
    if (!w || !h) throw new Error("empty_frame");
    const xs = [0, matrix.a * w, matrix.c * h, matrix.a * w + matrix.c * h];
    const ys = [0, matrix.b * w, matrix.d * h, matrix.b * w + matrix.d * h];
    const sx = rect.width / (Math.max(...xs) - Math.min(...xs));
    const sy = rect.height / (Math.max(...ys) - Math.min(...ys));
    const a = matrix.a * sx, b = matrix.b * sy, c = matrix.c * sx, d = matrix.d * sy;
    const tx = rect.left - Math.min(...xs) * sx + a * frame.clientLeft + c * frame.clientTop;
    const ty = rect.top - Math.min(...ys) * sy + b * frame.clientLeft + d * frame.clientTop;
    const result = [a, b, c, d, tx, ty];
    if (!result.every(Number.isFinite) || Math.abs(a * d - b * c) < 0.0001) throw new Error("invalid_frame_transform");
    return result;
  }

  function hitElement(x, y) {
    let element = document.elementFromPoint(x, y);
    for (let depth = 0; depth < 64 && element?.shadowRoot; ++depth) {
      const child = element.shadowRoot.elementFromPoint(x, y);
      if (!shadowRoots.has(element.shadowRoot)) {
        observer?.observe(element.shadowRoot, {childList: true, subtree: true, attributes: true, characterData: true});
        shadowRoots.add(element.shadowRoot);
        element.shadowRoot.addEventListener("scroll", invalidate, {capture: true, passive: true});
      }
      if (!child || child === element) break;
      element = child;
    }
    return element;
  }

  async function inspect(message) {
    begin(message);
    // The verified native viewport retains fractions lost by integer innerWidth at high zoom.
    active.viewportSize = Array.isArray(message.viewportSize) && message.viewportSize.length === 2 &&
      message.viewportSize.every((v, i) => Number.isFinite(v) && v > 0 &&
        Math.abs(v - [innerWidth, innerHeight][i]) <= 16) ? message.viewportSize : null;
    const [width, height] = viewportSize();
    const [x, y] = message.point;
    if (!Number.isFinite(x) || !Number.isFinite(y) || x < 0 || y < 0 || x >= width || y >= height) {
      throw new Error("outside_viewport");
    }
    const element = hitElement(x, y);
    if (!element) throw new Error("empty_hit");
    const {candidates: path, page, entries} = candidates(element, x, y);
    if (!path.length) throw new Error("empty_hit");
    active.hit = {element, point: [x, y], entries};
    if (element instanceof HTMLIFrameElement || element instanceof HTMLFrameElement) {
      try {
        const matrix = frameMatrix(element);
        let index = -1;
        for (let i = 0; i < window.length; ++i) if (window[i] === element.contentWindow) { index = i; break; }
        if (index < 0) throw new Error("frame_not_authorized");
        active.hit.frame = matrix;
        return {candidates: path, page, frame: {index, matrix}, metrics: metrics()};
      } catch (error) {
        if (!["unsupported_frame_transform", "empty_frame", "frame_not_authorized"].includes(error.message)) throw error;
      }
    }
    // Canvas/PDF plug-ins expose their containing DOM box only.
    return {candidates: path, page, metrics: metrics()};
  }

  function metrics() {
    return {width: innerWidth, height: innerHeight, dpr: devicePixelRatio,
      screen: typeof screen === "undefined" ? undefined :
        [screen.availLeft, screen.availTop, screen.availLeft + screen.availWidth, screen.availTop + screen.availHeight],
      visualScale: visualViewport?.scale || 1, offsetX: visualViewport?.offsetLeft || 0,
      offsetY: visualViewport?.offsetTop || 0};
  }

  chrome.runtime.onMessage.addListener((message, sender, reply) => {
    if (sender.id !== chrome.runtime.id || !["begin", "inspect", "end", "metrics", "route"].includes(message?.type)) return;
    if (message.type === "end") {
      if (active?.sessionId === message.sessionId) end();
      reply({ended: true}); return;
    }
    if (message.type === "route") {
      let index = -1;
      if (parent !== window) for (let i = 0; i < parent.length; ++i) if (parent[i] === window) { index = i; break; }
      reply({index}); return;
    }
    if (message.type === "metrics") { reply(metrics()); return; }
    if (typeof message.sessionId !== "string" || message.sessionId.length > 64) return;
    if (message.type === "begin" && message.lease) {
      if (active?.sessionId === message.sessionId) { clearTimeout(expiry); expiry = setTimeout(invalidate, leaseMs); }
      reply({renewed: !!active}); return;
    }
    if (!Array.isArray(message.point)) return;
    inspect(message).then(reply, error => reply({status: error.message}));
    return true;
  });
})();
