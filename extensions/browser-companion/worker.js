"use strict";
const host = "com.zencrop.browser_companion";
let port = null;
let connectionId = crypto.randomUUID();
let active = null;
let renewal = 0;
let backoff = 0;
let sequence = Promise.resolve();
let lastFocusedWindow = -1;
let helloRevision = 0;
const envelope = message => ({version: 1, ...message, connectionId});
const post = message => port?.postMessage(envelope(message));

async function hello() {
  const revision = ++helloRevision;
  const all = (await chrome.windows.getAll({populate: true})).filter(w => w.type === "normal");
  if (revision !== helloRevision) return;
  const focused = all.find(w => w.focused);
  if (focused) lastFocusedWindow = focused.id;
  else if (lastFocusedWindow < 0 && all.length) {
    try { lastFocusedWindow = (await chrome.windows.getLastFocused()).id; } catch {}
  }
  const windows = await Promise.all(all.map(async w => {
    const result = {id: w.id, left: w.left, top: w.top, width: w.width, height: w.height,
      lastFocused: w.id === lastFocusedWindow};
    const tab = w.tabs?.find(t => t.active);
    if (tab && /^https?:\/\//.test(tab.url || "")) {
      try {
        const metrics = await chrome.tabs.sendMessage(tab.id, {type: "metrics"}, {frameId: 0});
        if (Array.isArray(metrics?.screen)) result.screen = metrics.screen;
      } catch {}
    }
    return result;
  }));
  if (revision === helloRevision) post({type: "hello", windows});
}

async function end() {
  const previous = active;
  active = null;
  clearInterval(renewal);
  if (previous) await Promise.allSettled([...previous.frames].map(([frameId, documentId]) =>
    chrome.tabs.sendMessage(previous.tabId, {type: "end", sessionId: previous.sessionId}, {frameId, documentId})));
}

function unavailable(message, status) {
  post({type: "unavailable", sessionId: message.sessionId, requestId: message.requestId, status});
}

async function invalidate(status) {
  if (!active) return;
  const previous = active;
  const {sessionId, requestId} = previous;
  active = previous.frozen ? {...previous, frames: new Map(), invalid: true} : null;
  clearInterval(renewal);
  post({type: "invalidated", sessionId, requestId, status});
  await Promise.allSettled([...previous.frames].map(([frameId, documentId]) =>
    chrome.tabs.sendMessage(previous.tabId, {type: "end", sessionId}, {frameId, documentId})));
}

function mapRect(rect, m) {
  const points = [[rect[0], rect[1]], [rect[2], rect[1]], [rect[0], rect[3]], [rect[2], rect[3]]]
    .map(([x, y]) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]);
  return [Math.min(...points.map(p => p[0])), Math.min(...points.map(p => p[1])),
    Math.max(...points.map(p => p[0])), Math.max(...points.map(p => p[1]))];
}

async function inspectFrame(tabId, frameId, documentId, point, message, depth = 0) {
  if (depth > 16 || !active || active.sessionId !== message.sessionId) throw new Error("target_unavailable");
  const session = active;
  session.frames.set(frameId, documentId);
  let reply;
  try {
    reply = await chrome.tabs.sendMessage(tabId,
      {type: "inspect", sessionId: message.sessionId, frozen: message.frozen, point,
        viewportSize: depth === 0 ? message.viewportSize : undefined}, {frameId, documentId});
  } catch { reply = {status: "not_authorized"}; }
  if (!reply || reply.status || !Array.isArray(reply.candidates)) {
    session.frames.delete(frameId);
    await chrome.tabs.sendMessage(tabId, {type: "end", sessionId: message.sessionId}, {frameId, documentId}).catch(() => {});
    throw new Error(reply?.status || "not_authorized");
  }
  if (reply.frame) {
    try {
      const {matrix: m, index} = reply.frame;
      const frames = await chrome.webNavigation.getAllFrames({tabId});
      if (frames.length > 512) throw new Error("target_unavailable");
      // Frame identities come from extension APIs; only frame-index metadata is queried among siblings.
      const identities = await Promise.all(frames.filter(f => f.parentFrameId === frameId).map(async f => {
        try { const route = await chrome.tabs.sendMessage(tabId, {type: "route"}, {frameId: f.frameId, documentId: f.documentId});
          return route.index === index ? f : null; } catch { return null; }
      }));
      const matches = identities.filter(Boolean);
      if (matches.length !== 1) throw new Error("frame_not_authorized");
      const {frameId: child, documentId: childDocument} = matches[0];
      const determinant = m[0] * m[3] - m[1] * m[2];
      const x = point[0] - m[4], y = point[1] - m[5];
      const childPoint = [(m[3] * x - m[2] * y) / determinant, (-m[1] * x + m[0] * y) / determinant];
      const nested = await inspectFrame(tabId, child, childDocument, childPoint, message, depth + 1);
      const clip = reply.candidates[0];
      const mapped = nested.candidates.map(r => mapRect(r, m)).map(r =>
        [Math.max(r[0], clip[0]), Math.max(r[1], clip[1]), Math.min(r[2], clip[2]), Math.min(r[3], clip[3])]);
      return {candidates: [...mapped, ...reply.candidates].slice(0, 64),
        page: depth === 0 && reply.page === true && mapped.length + reply.candidates.length <= 64};
    } catch (error) {
      if (!["frame_not_authorized", "not_authorized", "unsupported_frame_transform", "empty_frame",
        "outside_viewport", "empty_hit"].includes(error.message)) throw error;
      // The containing frame remains selectable when its document cannot be inspected.
    }
  }
  return {candidates: reply.candidates, page: depth === 0 && reply.page === true};
}

async function query(message) {
  if (message.version !== 1 || typeof message.sessionId !== "string" || typeof message.requestId !== "string") return;
  if (message.type === "end") {
    if (active?.sessionId === message.sessionId) await end();
    return;
  }
  if (!["begin", "inspect", "navigate"].includes(message.type) || message.connectionId !== connectionId) return;
  try {
    const window = await chrome.windows.get(message.windowId, {populate: true});
    const tab = window.tabs.find(t => t.active);
    if (!tab || !/^https?:\/\//.test(tab.url || "")) throw new Error("restricted_page");
    const frame = await chrome.webNavigation.getFrame({tabId: tab.id, frameId: 0});
    if (!frame?.documentId) throw new Error("target_unavailable");
    if (active && active.sessionId === message.sessionId &&
        (active.invalid || active.tabId !== tab.id || active.documentId !== frame.documentId)) {
      throw new Error("geometry_invalidated");
    }
    if (active?.sessionId !== message.sessionId) {
      await end();
      active = {sessionId: message.sessionId, requestId: message.requestId, windowId: window.id,
        tabId: tab.id, documentId: frame.documentId, frames: new Map(), frozen: message.frozen, invalid: false};
      const session = active;
      renewal = setInterval(() => {
        if (active !== session || session.invalid) return;
        for (const [frameId, documentId] of session.frames) chrome.tabs.sendMessage(session.tabId,
          {type: "begin", sessionId: session.sessionId, frozen: session.frozen, lease: true}, {frameId, documentId})
          .then(reply => {
            if (active === session && session.frames.get(frameId) === documentId && !reply?.renewed) invalidate("geometry_invalidated");
          }, () => { if (active === session && session.frames.get(frameId) === documentId) invalidate("geometry_invalidated"); });
      }, 10000);
    }
    active.requestId = message.requestId;
    const metrics = await chrome.tabs.sendMessage(tab.id, {type: "metrics"}, {frameId: 0, documentId: frame.documentId});
    const scale = metrics.dpr * metrics.visualScale;
    // innerWidth/innerHeight round to integer CSS pixels, amplified by device/page zoom.
    const tolerance = Math.max(4, metrics.dpr);
    const matches = (message.viewports || [message.viewport]).filter(v => Array.isArray(v) && v.length === 4 && v.every(Number.isFinite) &&
      Math.abs(metrics.width * metrics.dpr - (v[2] - v[0])) <= tolerance &&
      Math.abs(metrics.height * metrics.dpr - (v[3] - v[1])) <= tolerance);
    if (matches.length > 1) throw new Error("viewport_ambiguous");
    if (!matches.length) throw new Error("viewport_unavailable");
    const v = matches[0];
    if (message.point[0] < v[0] || message.point[0] >= v[2] || message.point[1] < v[1] || message.point[1] >= v[3]) {
      throw new Error("outside_viewport");
    }
    if (!Number.isFinite(scale) || scale < 0.25 || scale > 16 || !Array.isArray(v) || v.length !== 4 ||
        Math.abs(metrics.width * metrics.dpr - (v[2] - v[0])) > tolerance ||
        Math.abs(metrics.height * metrics.dpr - (v[3] - v[1])) > tolerance) throw new Error("viewport_unavailable");
    const point = [(message.point[0] - v[0]) / scale + metrics.offsetX,
      (message.point[1] - v[1]) / scale + metrics.offsetY];
    const hit = await inspectFrame(tab.id, 0, frame.documentId, point, {...message,
      viewportSize: [(v[2] - v[0]) / scale + metrics.offsetX, (v[3] - v[1]) / scale + metrics.offsetY]});
    const candidates = hit.candidates.map(r => [r[0] - metrics.offsetX, r[1] - metrics.offsetY, r[2] - metrics.offsetX, r[3] - metrics.offsetY]);
    if (!active || active.sessionId !== message.sessionId || active.invalid) throw new Error("geometry_invalidated");
    post({type: "result", sessionId: message.sessionId, requestId: message.requestId,
      windowId: window.id, tabId: tab.id, documentId: frame.documentId, frameId: 0, geometryVersion: 1, scale, viewport: v, candidates, page: hit.page});
  } catch (error) {
    const known = ["restricted_page", "target_unavailable", "geometry_invalidated", "viewport_unavailable", "viewport_ambiguous", "frame_not_authorized", "not_authorized", "empty_hit", "outside_viewport", "invalid_frame_transform", "unsupported_frame_transform", "empty_frame"];
    unavailable(message, known.includes(error.message) ? error.message : "not_authorized");
  }
}

chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (sender.id !== chrome.runtime.id || !sender.tab || !sender.documentId) return;
  if (message.type === "invalidated") {
    if (active?.tabId === sender.tab.id && active.sessionId === message.sessionId &&
        active.frames.get(sender.frameId) === sender.documentId) invalidate("geometry_invalidated");
  }
});

function connect() {
  if (port) return;
  connectionId = crypto.randomUUID();
  try {
    port = chrome.runtime.connectNative(host);
    port.onMessage.addListener(message => { sequence = sequence.then(() => query(message)).catch(() => {}); });
    port.onDisconnect.addListener(() => {
      void chrome.runtime.lastError;
      port = null;
      end();
      chrome.alarms.create("reconnect", {delayInMinutes: Math.min(5, 0.5 * 2 ** backoff++)});
    });
    hello().catch(() => {});
  } catch { port = null; }
}

chrome.runtime.onInstalled.addListener(connect);
chrome.runtime.onStartup.addListener(connect);
chrome.action.onClicked.addListener(() => { backoff = 0; connect(); hello().catch(() => {}); });
chrome.alarms.onAlarm.addListener(alarm => { if (alarm.name === "reconnect") connect(); });
chrome.windows.onFocusChanged.addListener(id => {
  if (id >= 0) lastFocusedWindow = id;
  hello().catch(() => {});
});
chrome.windows.onCreated.addListener(() => hello().catch(() => {}));
chrome.windows.onRemoved.addListener(id => { if (active?.windowId === id) invalidate("target_unavailable"); hello().catch(() => {}); });
chrome.windows.onBoundsChanged.addListener(window => { if (active?.windowId === window.id) invalidate("geometry_invalidated"); hello().catch(() => {}); });
chrome.tabs.onActivated.addListener(info => { if (active?.windowId === info.windowId) invalidate("geometry_invalidated"); });
chrome.webNavigation.onCommitted.addListener(info => {
  if (active?.tabId === info.tabId && (info.frameId === 0 || active.frames.has(info.frameId))) invalidate("geometry_invalidated");
});
connect();
