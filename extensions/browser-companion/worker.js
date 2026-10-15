"use strict";
// ZenCrop Browser Companion - Manifest V3 service worker.
//
// WHY THIS EXISTS
//   Chromium keeps a process-wide accessibility mode once any platform consumer asks
//   for it, and releasing our COM objects does not close it. This worker answers
//   "which rectangle is under the cursor" straight from the DOM, which never touches
//   the accessibility tree.
//
// DESIGN RULES (keep them when editing)
//   1. Passive until asked. No content script is registered here; the target page is
//      only touched by the declarative content script and by on-demand tab messages.
//   2. Never guess. A missing or ambiguous document is reported as a miss, which the
//      desktop side treats as "unknown" and fails closed.
//   3. The resident native-messaging host is gone from the daily path. WebSocket 2.0
//      carries the traffic; native messaging survives only as an explicit, bounded
//      pairing bootstrap (see pair()). Nothing on the startup, alarm or reconnect
//      path may launch a host process.
//   4. One connection context at a time. Every attempt owns an object holding its
//      socket, its id, its serial chain, its session and all of its timers. Every
//      asynchronous step captures the context it started in and re-checks it after
//      each await, so a superseded connection can never post into, end, or restart a
//      lease for its successor. There is no shared transport state to get this wrong.
//   5. Unpaired means silent. Before auth_ok no window list is sent and no page is
//      inspected - a page read is not "free" just because the last post was dropped.
const HOST_NAME = "com.zencrop.browser_companion";
const WS_URL = "ws://127.0.0.1:19227/zencrop/browser/v2";
const WS_SUBPROTOCOL = "zencrop-companion-v2";
const PAIRING_KEY = "zencropPairingV2";
const PAIRING_PAGE = "connection.html";
const MAX_MESSAGE_BYTES = 65536;
const HEARTBEAT_MS = 20000;
const PONG_TIMEOUT_MS = 60000;
const AUTH_TIMEOUT_MS = 5000;
const PAIRING_TIMEOUT_MS = 7000;
const RECONNECT_ALARM = "zencrop-reconnect";
const RECONNECT_MAX_MINUTES = 5;
const RECONNECT_FAST_MS = 250;

// The connection that owns the transport right now, or null. Everything that touches
// the socket, the session or a timer goes through it.
let current = null;
let lastFocusedWindow = -1;
let helloRevision = 0;
let connectPending = null;
let pairingPending = null;
let fastRetryTimer = 0;
let reconnectMinutes = 0;
let connectionStatus = {state: "unpaired", reason: ""};

function newConnection(socket) {
  return {
    socket,
    id: crypto.randomUUID(),
    state: "auth",
    sequence: Promise.resolve(),
    session: null,
    renewal: 0,
    keepalive: 0,
    lastPong: Date.now(),
    credential: null,
    key: null,
    transcript: "",
    failure: ""
  };
}

// The single question every asynchronous callback asks before it acts: is this still
// the live connection? A superseded context answers no forever, which is what keeps an
// old task out of a new connection's session, lease and transport.
function live(context) {
  return context !== null && current === context && context.state === "ready" &&
    context.socket.readyState === WebSocket.OPEN;
}

function showStatus(state, reason = "") {
  connectionStatus = {state, reason};
  const text = state === "connected" ? "ON" : state === "unpaired" ? "PAIR" : state === "error" ? "ERR" : "WAIT";
  void chrome.action.setBadgeText({text});
  void chrome.action.setTitle({title: "ZenCrop: " + state + (reason ? " (" + reason + ")" : "") + ". Open the connection page to pair."});
}

function validHex(value, bytes) {
  return typeof value === "string" && value.length === bytes * 2 && /^[0-9a-f]+$/.test(value);
}

function randomHex(bytes) {
  return Array.from(crypto.getRandomValues(new Uint8Array(bytes)), b => b.toString(16).padStart(2, "0")).join("");
}

function hexBytes(value) {
  return Uint8Array.from(value.match(/../g), b => parseInt(b, 16));
}

async function hmacKey(secret) {
  return crypto.subtle.importKey("raw", hexBytes(secret), {name: "HMAC", hash: "SHA-256"}, false, ["sign", "verify"]);
}

async function signProof(key, message) {
  const bytes = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(message));
  return Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2, "0")).join("");
}

// The secret lives in trusted extension storage only: content scripts run on every
// page the user allows, so the access level is set before every read and write and a
// failure stops connecting instead of silently continuing unprotected.
async function loadPairing() {
  await chrome.storage.local.setAccessLevel({accessLevel: "TRUSTED_CONTEXTS"});
  return (await chrome.storage.local.get(PAIRING_KEY))[PAIRING_KEY];
}

function validPairing(value) {
  return value?.wsVersion === 2 && value?.port === 19227 && validHex(value.instance, 16) &&
    validHex(value.profile, 16) && validHex(value.secret, 32);
}

// Sending is only ever allowed through the live connection, and the envelope is
// stamped from that connection's own id: a message can never carry another
// connection's identity.
function post(context, message) {
  if (!live(context))
    return false;
  try {
    const json = JSON.stringify({version: 1, ...message, connectionId: context.id});
    if (json.length > MAX_MESSAGE_BYTES || context.socket.bufferedAmount > MAX_MESSAGE_BYTES)
      return false;
    context.socket.send(json);
    return true;
  } catch {
    return false;
  }
}

async function hello(context) {
  if (!live(context))
    return;
  const revision = ++helloRevision;
  const all = (await chrome.windows.getAll({populate: true})).filter(w => w.type === "normal");
  if (revision !== helloRevision || !live(context))
    return;
  const focused = all.find(w => w.focused);
  if (focused)
    lastFocusedWindow = focused.id;
  else if (lastFocusedWindow < 0 && all.length) {
    try {
      lastFocusedWindow = (await chrome.windows.getLastFocused()).id;
    } catch {}
  }
  if (revision !== helloRevision || !live(context))
    return;
  const windows = await Promise.all(all.map(async w => {
    const result = {id: w.id, left: w.left, top: w.top, width: w.width, height: w.height,
      lastFocused: w.id === lastFocusedWindow};
    const tab = w.tabs?.find(t => t.active);
    if (tab && /^https?:\/\//.test(tab.url || "")) {
      try {
        const metrics = await chrome.tabs.sendMessage(tab.id, {type: "metrics"}, {frameId: 0});
        if (Array.isArray(metrics?.screen))
          result.screen = metrics.screen;
      } catch {}
    }
    return result;
  }));
  if (revision === helloRevision && live(context))
    post(context, {type: "hello", windows});
}

// The session belongs to the connection that created it. Ending one only ever touches
// the object captured here, so a late end from a superseded connection cannot end, or
// recreate, a newer selection.
async function endSession(context, sessionId) {
  const session = context.session;
  if (!session || (sessionId !== undefined && session.sessionId !== sessionId))
    return;
  context.session = null;
  clearInterval(context.renewal);
  context.renewal = 0;
  await Promise.allSettled([...session.frames].map(([frameId, documentId]) =>
    chrome.tabs.sendMessage(session.tabId, {type: "end", sessionId: session.sessionId}, {frameId, documentId})));
}

function unavailable(context, message) {
  post(context, {type: "unavailable", sessionId: message.sessionId, requestId: message.requestId,
    status: message.status});
}

async function invalidate(context, status) {
  const session = context?.session;
  if (!session)
    return;
  const {sessionId, requestId, frozen, frames, tabId} = session;
  context.session = frozen ? {...session, frames: new Map(), invalid: true} : null;
  clearInterval(context.renewal);
  context.renewal = 0;
  post(context, {type: "invalidated", sessionId, requestId, status});
  await Promise.allSettled([...frames].map(([frameId, documentId]) =>
    chrome.tabs.sendMessage(tabId, {type: "end", sessionId}, {frameId, documentId})));
}

function mapRect(rect, m) {
  const points = [[rect[0], rect[1]], [rect[2], rect[1]], [rect[0], rect[3]], [rect[2], rect[3]]]
    .map(([x, y]) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]);
  return [Math.min(...points.map(p => p[0])), Math.min(...points.map(p => p[1])),
    Math.max(...points.map(p => p[0])), Math.max(...points.map(p => p[1]))];
}

async function inspectFrame(context, tabId, frameId, documentId, point, message, depth = 0) {
  const session = context.session;
  if (depth > 16 || !session || session.sessionId !== message.sessionId)
    throw new Error("target_unavailable");
  session.frames.set(frameId, documentId);
  let reply;
  try {
    reply = await chrome.tabs.sendMessage(tabId,
      {type: "inspect", sessionId: message.sessionId, frozen: message.frozen, point,
        viewportSize: depth === 0 ? message.viewportSize : undefined}, {frameId, documentId});
  } catch {
    reply = {status: "not_authorized"};
  }
  if (!reply || reply.status || !Array.isArray(reply.candidates)) {
    session.frames.delete(frameId);
    await chrome.tabs.sendMessage(tabId, {type: "end", sessionId: message.sessionId}, {frameId, documentId}).catch(() => {});
    throw new Error(reply?.status || "not_authorized");
  }
  if (reply.frame) {
    try {
      const {matrix: m, index} = reply.frame;
      const frames = await chrome.webNavigation.getAllFrames({tabId});
      if (frames.length > 512)
        throw new Error("target_unavailable");
      // Frame identities come from extension APIs; only frame-index metadata is queried among siblings.
      const identities = await Promise.all(frames.filter(f => f.parentFrameId === frameId).map(async f => {
        try {
          const route = await chrome.tabs.sendMessage(tabId, {type: "route"}, {frameId: f.frameId, documentId: f.documentId});
          return route.index === index ? f : null;
        } catch {
          return null;
        }
      }));
      const matches = identities.filter(Boolean);
      if (matches.length !== 1)
        throw new Error("frame_not_authorized");
      const {frameId: child, documentId: childDocument} = matches[0];
      const determinant = m[0] * m[3] - m[1] * m[2];
      const x = point[0] - m[4], y = point[1] - m[5];
      const childPoint = [(m[3] * x - m[2] * y) / determinant, (-m[1] * x + m[0] * y) / determinant];
      const nested = await inspectFrame(context, tabId, child, childDocument, childPoint, message, depth + 1);
      const clip = reply.candidates[0];
      const mapped = nested.candidates.map(r => mapRect(r, m)).map(r =>
        [Math.max(r[0], clip[0]), Math.max(r[1], clip[1]), Math.min(r[2], clip[2]), Math.min(r[3], clip[3])]);
      return {candidates: [...mapped, ...reply.candidates].slice(0, 64),
        page: depth === 0 && reply.page === true && mapped.length + reply.candidates.length <= 64};
    } catch (error) {
      if (!["frame_not_authorized", "not_authorized", "unsupported_frame_transform", "empty_frame",
        "outside_viewport", "empty_hit"].includes(error.message))
        throw error;
      // The containing frame remains selectable when its document cannot be inspected.
    }
  }
  return {candidates: reply.candidates, page: depth === 0 && reply.page === true};
}

async function query(context, message) {
  if (message.version !== 1 || typeof message.sessionId !== "string" || typeof message.requestId !== "string")
    return;
  if (message.type === "end") {
    await endSession(context, message.sessionId);
    return;
  }
  if (!["begin", "inspect", "navigate"].includes(message.type) || message.connectionId !== context.id)
    return;
  try {
    const window = await chrome.windows.get(message.windowId, {populate: true});
    // A dead connection cannot answer anything, so the query stops here rather than
    // touching a session or a page on behalf of a connection nobody can reach.
    if (!live(context))
      return;
    const tab = window.tabs.find(t => t.active);
    if (!tab || !/^https?:\/\//.test(tab.url || ""))
      throw new Error("restricted_page");
    const frame = await chrome.webNavigation.getFrame({tabId: tab.id, frameId: 0});
    if (!live(context))
      return;
    if (!frame?.documentId)
      throw new Error("target_unavailable");
    if (context.session && context.session.sessionId === message.sessionId &&
        (context.session.invalid || context.session.tabId !== tab.id ||
         context.session.documentId !== frame.documentId)) {
      throw new Error("geometry_invalidated");
    }
    if (context.session?.sessionId !== message.sessionId) {
      // Replacing the previous session, or starting the first one. The await below is a
      // deliberate hand-off point: the connection may have been superseded while the
      // old session was being released, so liveness is checked again before the new
      // session - and its lease - is created.
      await endSession(context);
      if (!live(context))
        return;
      const session = {sessionId: message.sessionId, requestId: message.requestId, windowId: window.id,
        tabId: tab.id, documentId: frame.documentId, frames: new Map(), frozen: message.frozen, invalid: false};
      context.session = session;
      context.renewal = setInterval(() => {
        if (context.session !== session || session.invalid)
          return;
        for (const [frameId, documentId] of session.frames) {
          chrome.tabs.sendMessage(session.tabId,
            {type: "begin", sessionId: session.sessionId, frozen: session.frozen, lease: true}, {frameId, documentId})
            .then(reply => {
              if (context.session === session && session.frames.get(frameId) === documentId && !reply?.renewed)
                void invalidate(context, "geometry_invalidated");
            }, () => {
              if (context.session === session && session.frames.get(frameId) === documentId)
                void invalidate(context, "geometry_invalidated");
            });
        }
      }, 10000);
    }
    context.session.requestId = message.requestId;
    const metrics = await chrome.tabs.sendMessage(tab.id, {type: "metrics"}, {frameId: 0, documentId: frame.documentId});
    if (!live(context))
      return;
    const scale = metrics.dpr * metrics.visualScale;
    // innerWidth/innerHeight round to integer CSS pixels, amplified by device/page zoom.
    const tolerance = Math.max(4, metrics.dpr);
    const matches = (message.viewports || [message.viewport]).filter(v => Array.isArray(v) && v.length === 4 && v.every(Number.isFinite) &&
      Math.abs(metrics.width * metrics.dpr - (v[2] - v[0])) <= tolerance &&
      Math.abs(metrics.height * metrics.dpr - (v[3] - v[1])) <= tolerance);
    if (matches.length > 1)
      throw new Error("viewport_ambiguous");
    if (!matches.length)
      throw new Error("viewport_unavailable");
    const v = matches[0];
    if (message.point[0] < v[0] || message.point[0] >= v[2] || message.point[1] < v[1] || message.point[1] >= v[3]) {
      throw new Error("outside_viewport");
    }
    if (!Number.isFinite(scale) || scale < 0.25 || scale > 16 || !Array.isArray(v) || v.length !== 4 ||
        Math.abs(metrics.width * metrics.dpr - (v[2] - v[0])) > tolerance ||
        Math.abs(metrics.height * metrics.dpr - (v[3] - v[1])) > tolerance)
      throw new Error("viewport_unavailable");
    const point = [(message.point[0] - v[0]) / scale + metrics.offsetX,
      (message.point[1] - v[1]) / scale + metrics.offsetY];
    const hit = await inspectFrame(context, tab.id, 0, frame.documentId, point, {...message,
      viewportSize: [(v[2] - v[0]) / scale + metrics.offsetX, (v[3] - v[1]) / scale + metrics.offsetY]});
    if (!live(context))
      return;
    const candidates = hit.candidates.map(r => [r[0] - metrics.offsetX, r[1] - metrics.offsetY,
      r[2] - metrics.offsetX, r[3] - metrics.offsetY]);
    if (!context.session || context.session.sessionId !== message.sessionId || context.session.invalid)
      throw new Error("geometry_invalidated");
    post(context, {type: "result", sessionId: message.sessionId, requestId: message.requestId,
      windowId: window.id, tabId: tab.id, documentId: frame.documentId, frameId: 0, geometryVersion: 1,
      scale, viewport: v, candidates, page: hit.page});
  } catch (error) {
    const known = ["restricted_page", "target_unavailable", "geometry_invalidated", "viewport_unavailable",
      "viewport_ambiguous", "frame_not_authorized", "not_authorized", "empty_hit", "outside_viewport",
      "invalid_frame_transform", "unsupported_frame_transform", "empty_frame"];
    // The desktop matches on the status name, so it has to travel with the reply: an
    // unknown failure is reported as not_authorized instead of leaking an error string.
    const status = known.includes(error?.message) ? error.message : "not_authorized";
    unavailable(context, {...message, status});
    if (live(context))
      console.warn("[zencrop] query failed: " + status);
  }
}

// Ends this connection's ownership. The session is released here, not later, so the
// next connection starts from a clean slate and no late task can revive this one.
function teardown(context) {
  context.state = "closed";
  const session = context.session;
  context.session = null;
  clearInterval(context.renewal);
  context.renewal = 0;
  clearInterval(context.keepalive);
  context.keepalive = 0;
  if (current === context)
    current = null;
  if (session) {
    // Best effort lease cleanup after a disconnect. It must not restart anything.
    void Promise.allSettled([...session.frames].map(([frameId, documentId]) =>
      chrome.tabs.sendMessage(session.tabId, {type: "end", sessionId: session.sessionId}, {frameId, documentId})));
  }
}

function closeCurrent() {
  const context = current;
  if (!context)
    return;
  teardown(context);
  try {
    context.socket.close();
  } catch {}
}

function scheduleReconnect() {
  if (fastRetryTimer) {
    clearTimeout(fastRetryTimer);
    fastRetryTimer = 0;
  }
  if (reconnectMinutes <= 0) {
    // One prompt retry covers a desktop that is restarting; the alarm covers the rest.
    reconnectMinutes = 0.5;
    fastRetryTimer = setTimeout(() => {
      fastRetryTimer = 0;
      void connect();
    }, RECONNECT_FAST_MS);
  }
  void chrome.alarms.create(RECONNECT_ALARM, {delayInMinutes: reconnectMinutes});
  reconnectMinutes = Math.min(RECONNECT_MAX_MINUTES, reconnectMinutes * 2);
}

function failConnection(context, error) {
  if (current !== context)
    return;
  const reason = ["pair_again", "authentication_failed", "invalid_message"].includes(error?.message)
    ? error.message : "invalid_message";
  context.failure = reason;
  context.socket.close();
}

// Authentication is server-first: the desktop challenges, this side proves it holds the
// profile secret, and only then is anything about this browser disclosed.
async function authenticate(context, message) {
  if (context.state === "auth") {
    if (message.version !== 2 || message.type !== "challenge")
      throw new Error("invalid_message");
    if (!validHex(message.instance, 16) || !validHex(message.nonce, 32))
      throw new Error("invalid_message");
    if (message.instance !== context.credential.instance)
      throw new Error("pair_again");
    context.transcript = context.credential.instance + ":" + randomHex(32) + ":" + message.nonce;
    context.state = "proving";
    context.socket.send(JSON.stringify({version: 2, type: "auth_start", profile: context.credential.profile,
      nonce: context.transcript.split(":")[1]}));
    return;
  }
  if (context.state === "proving") {
    if (message.version !== 2 || message.type !== "auth_server" || !validHex(message.proof, 32))
      throw new Error("invalid_message");
    // The desktop proves itself first: an impostor on the port cannot get this far
    // without the secret, and nothing is disclosed until the proof verifies.
    const key = await authKey(context);
    const verified = await crypto.subtle.verify("HMAC", key,
      hexBytes(message.proof), new TextEncoder().encode("server:" + context.transcript));
    if (!verified)
      throw new Error("authentication_failed");
    // Re-checked after every await: a superseded connection must not send a proof.
    if (current !== context || context.state !== "proving")
      return;
    const proof = await signProof(key, "client:" + context.transcript);
    if (current !== context || context.state !== "proving")
      return;
    context.state = "confirmed";
    context.socket.send(JSON.stringify({version: 2, type: "auth_client",
      proof}));
    return;
  }
  if (context.state === "confirmed") {
    if (message.version !== 2 || message.type !== "auth_ok")
      throw new Error("invalid_message");
    context.state = "ready";
    context.lastPong = Date.now();
    reconnectMinutes = 0;
    showStatus("connected");
    void hello(context);
    // The heartbeat keeps the service worker alive as well as the connection: from
    // Chrome 116 on, WebSocket traffic is what extends its lifetime.
    context.keepalive = setInterval(() => {
      if (!live(context))
        return;
      if (Date.now() - context.lastPong > PONG_TIMEOUT_MS) {
        context.socket.close();
        return;
      }
      try {
        context.socket.send(JSON.stringify({version: 2, type: "ping"}));
      } catch {
        context.socket.close();
      }
    }, HEARTBEAT_MS);
    return;
  }
  throw new Error("invalid_message");
}

async function authKey(context) {
  // Derived on first use rather than pre-armed: a loopback desktop can answer the
  // challenge before an eagerly started importKey() has settled, and the handshake must
  // not depend on that race.
  if (!context.key)
    context.key = await hmacKey(context.credential.secret);
  return context.key;
}

async function handleFrame(context, event) {
  if (current !== context)
    return;
  let message;
  try {
    message = JSON.parse(event.data);
  } catch {
    return;
  }
  if (typeof message !== "object" || message === null || Array.isArray(message))
    return;
  if (context.state !== "ready") {
    await authenticate(context, message);
    return;
  }
  if (message.version === 2) {
    if (message.type === "pong")
      context.lastPong = Date.now();
    return;
  }
  if (message.version !== 1 || typeof message.sessionId !== "string" || typeof message.requestId !== "string")
    return;
  // Serial per connection: the desktop relies on one query in flight at a time, and a
  // superseded connection's chain can only ever touch its own context.
  context.sequence = context.sequence.then(() => query(context, message)).catch(() => {});
}

function connectWebSocket(credential) {
  const connection = new WebSocket(WS_URL, WS_SUBPROTOCOL);
  const context = newConnection(connection);
  context.credential = credential;
  current = context;
  showStatus("connecting");
  let authTimer = 0;
  connection.onopen = () => {
    authTimer = setTimeout(() => {
      if (current === context && context.state !== "ready")
        failConnection(context, new Error("authentication_failed"));
    }, AUTH_TIMEOUT_MS);
  };
  connection.onmessage = event => {
    void handleFrame(context, event).catch(error => failConnection(context, error));
  };
  connection.onclose = () => {
    clearTimeout(authTimer);
    if (current !== context)
      return;
    const failure = context.failure;
    teardown(context);
    showStatus(failure ? "error" : "disconnected", failure);
    scheduleReconnect();
  };
  connection.onerror = () => {
    if (current === context)
      connection.close();
  };
}

function connect() {
  if (current || connectPending || pairingPending)
    return connectPending;
  connectPending = (async () => {
    const credential = await loadPairing();
    if (current || pairingPending)
      return;
    if (!validPairing(credential)) {
      showStatus("unpaired");
      return;
    }
    connectWebSocket(credential);
  })().catch(() => showStatus("error", "credential_storage_unavailable"))
    .finally(() => {
      connectPending = null;
    });
  return connectPending;
}

// The only connectNative call site in the extension, and only the trusted connection
// page may reach it. A failed pairing never becomes a loop: a still-valid credential
// resumes the plain WebSocket path instead.
function pair() {
  if (pairingPending)
    return pairingPending;
  let resumeWebSocket = false;
  pairingPending = (async () => {
    await chrome.alarms.clear(RECONNECT_ALARM);
    // Pairing replaces the connection outright: the old context is torn down before the
    // bootstrap starts, so its socket close cannot schedule a reconnect for it.
    closeCurrent();
    const previous = await loadPairing();
    resumeWebSocket = validPairing(previous);
    const profile = validHex(previous?.profile, 16) ? previous.profile : randomHex(16);
    const credential = await new Promise((resolve, reject) => {
      const bootstrap = chrome.runtime.connectNative(HOST_NAME);
      let settled = false;
      const finish = (error, value) => {
        if (settled)
          return;
        settled = true;
        clearTimeout(timer);
        bootstrap.disconnect();
        if (error)
          reject(error);
        else
          resolve(value);
      };
      const timer = setTimeout(() => finish(new Error("pairing_timeout")), PAIRING_TIMEOUT_MS);
      bootstrap.onMessage.addListener(message => {
        if (message?.version !== 1 || message.type !== "paired" || !validPairing(message) || message.profile !== profile)
          finish(new Error(message?.reason || "incompatible_desktop"));
        else
          finish(null, message);
      });
      bootstrap.onDisconnect.addListener(() => {
        const error = chrome.runtime.lastError;
        if (!settled)
          finish(new Error(error?.message || "pairing_disconnected"));
      });
      bootstrap.postMessage({version: 1, type: "pair", ext: chrome.runtime.id, profile});
    });
    await chrome.storage.local.set({[PAIRING_KEY]: credential});
    resumeWebSocket = true;
  })().catch(error => {
    showStatus("error", error.message);
    console.warn("[zencrop] Pairing failed: " + error.message);
  }).finally(() => {
    pairingPending = null;
    if (resumeWebSocket)
      void connect();
  });
  return pairingPending;
}

chrome.runtime.onInstalled.addListener(() => void connect());
chrome.runtime.onStartup.addListener(() => void connect());
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  // Content scripts and pages may invalidate a selection, and nothing else.
  if (sender.id === chrome.runtime.id && sender.tab && sender.documentId) {
    const context = current;
    const session = context?.session;
    if (message.type === "invalidated" && session && session.tabId === sender.tab.id &&
        session.sessionId === message.sessionId && session.frames.get(sender.frameId) === sender.documentId)
      void invalidate(context, "geometry_invalidated");
    return false;
  }
  // Pairing and status come from the extension's own connection page only.
  if (sender.id !== chrome.runtime.id || sender.url !== chrome.runtime.getURL(PAIRING_PAGE) || sender.tab)
    return false;
  if (message?.type === "connection_status") {
    respond(connectionStatus);
    return false;
  }
  if (message?.type === "pair") {
    void pair().then(() => respond(connectionStatus));
    return true;
  }
  return false;
});
chrome.alarms.onAlarm.addListener(alarm => {
  if (alarm.name === RECONNECT_ALARM)
    void connect();
});

// Window bookkeeping exists only to keep hello accurate; it costs nothing while idle
// and never touches a page on its own.
chrome.windows.onFocusChanged.addListener(id => {
  if (id >= 0)
    lastFocusedWindow = id;
  void hello(current);
});
chrome.windows.onCreated.addListener(() => void hello(current));
chrome.windows.onRemoved.addListener(id => {
  if (current?.session?.windowId === id)
    void invalidate(current, "target_unavailable");
  void hello(current);
});
chrome.windows.onBoundsChanged.addListener(window => {
  if (current?.session?.windowId === window.id)
    void invalidate(current, "geometry_invalidated");
  void hello(current);
});
chrome.tabs.onActivated.addListener(info => {
  if (current?.session?.windowId === info.windowId)
    void invalidate(current, "geometry_invalidated");
});
chrome.webNavigation.onCommitted.addListener(info => {
  const session = current?.session;
  if (session?.tabId === info.tabId && (info.frameId === 0 || session.frames.has(info.frameId)))
    void invalidate(current, "geometry_invalidated");
});
void connect();
