"use strict";
// Extension-side contracts for the ZenCrop browser companion.
//
// The desktop's contract is that every business message arrives through the real
// WebSocket receive entry and that a superseded connection can never act. This fixture
// therefore delivers messages the way the platform does - into the socket - instead of
// calling the worker's internals, which is precisely how a broken dispatch path stayed
// invisible before.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const webcrypto = require("node:crypto").webcrypto;

const WORKER = path.join(__dirname, "../../extensions/browser-companion/worker.js");
const CONTENT = path.join(__dirname, "../../extensions/browser-companion/content.js");
const DETECTOR = path.join(__dirname, "../../src/detect/SmartDetector.cpp");
const MANIFEST = JSON.parse(fs.readFileSync(path.join(path.dirname(WORKER), "manifest.json"), "utf8"));
assert(Number(MANIFEST.minimum_chrome_version.split(".")[0]) >= 140,
  "Local credential storage requires trusted-context access control, supported from Chrome 140");

const hex = buffer => Array.from(new Uint8Array(buffer), b => b.toString(16).padStart(2, "0")).join("");
const randomHex = bytes => hex(webcrypto.getRandomValues(new Uint8Array(bytes)));
const CREDENTIAL = {wsVersion: 2, port: 19227, instance: randomHex(16), profile: randomHex(16), secret: randomHex(32)};

const event = name => {
  const listeners = [];
  return {name, listeners, addListener: listener => listeners.push(listener)};
};
const events = () => ({addListener() {}});

// A stand-in for the platform WebSocket: it records what the worker sent, and it speaks
// the desktop half of the handshake so an authenticated session can be reached without a
// desktop process.
class FakeSocket {
  constructor(url, protocol, options = {}) {
    this.url = url;
    this.protocol = protocol;
    this.options = options;
    this.readyState = 1;
    this.sent = [];
    this.bufferedAmount = 0;
    this.pings = 0;
    this.serverNonce = randomHex(32);
    this.transcript = null;
    this.closed = false;
  }
  send(text) {
    const message = JSON.parse(text);
    this.sent.push(message);
    this.onSend(message);
  }
  onSend(message) {
    if (message.type === "auth_start") {
      // The desktop answers the start frame with its own proof over the same transcript.
      this.transcript = [CREDENTIAL.instance, message.nonce, this.serverNonce];
      queueMicrotask(async () => {
        const key = await webcrypto.subtle.importKey("raw", Buffer.from(CREDENTIAL.secret, "hex"),
          {name: "HMAC", hash: "SHA-256"}, false, ["sign"]);
        const proof = hex(await webcrypto.subtle.sign("HMAC", key,
          Buffer.from("server:" + this.transcript.join(":"))));
        this.deliver({version: 2, type: "auth_server",
          proof: this.options.corruptServerProof ? randomHex(32) : proof}, true);
      });
    } else if (message.type === "auth_client") {
      if (!this.options.dropAck)
        queueMicrotask(() => this.deliver({version: 2, type: "auth_ok"}, true));
    } else if (message.type === "ping") {
      this.pings++;
      if (!this.options.silentPong)
        queueMicrotask(() => this.deliver({version: 2, type: "pong"}, true));
    }
  }
  close() {
    if (this.closed)
      return;
    this.closed = true;
    this.readyState = 3;
    this.onclose?.();
  }
  // The one entry the desktop uses to reach the worker, and the only one this suite uses.
  // A test that delivers into a socket the worker already closed is a test bug, so it
  // throws; the fixture's own queued replies are simply dropped, exactly as a real
  // desktop's in-flight frame is dropped when its peer vanishes.
  deliver(message, queued = false) {
    if (this.closed) {
      if (queued)
        return;
      throw new Error("delivered to a closed socket");
    }
    this.onmessage?.({data: typeof message === "string" ? message : JSON.stringify(message)});
  }
  open() {
    if (this.opened)
      return;
    this.opened = true;
    this.onopen?.();
    // The desktop speaks first: the challenge opens the exchange, and the worker may not
    // disclose anything before it has verified the answer.
    queueMicrotask(() => this.deliver({version: 2, type: "challenge", instance: CREDENTIAL.instance, nonce: this.serverNonce}, true));
  }
}
FakeSocket.CONNECTING = 0;
FakeSocket.OPEN = 1;
FakeSocket.CLOSING = 2;
FakeSocket.CLOSED = 3;

function createSocketFixture(options = {}) {
  const state = {
    sockets: [],
    stored: options.stored ?? {},
    accessLevel: null,
    storageFailure: options.storageFailure === true,
    nativeCalls: [],
    metricReads: 0,
    badge: [],
    alarmsCreated: [],
    enterFrame: options.enterFrame !== false,
    childError: options.childError || null,
    leaseReply: null,
  };
  const sent = [];
  const window = {id: 1, type: "normal", focused: true, left: 0, top: 0, width: 1000, height: 900,
    tabs: [{id: 1, active: true, url: "https://example.test"}]};
  const listeners = {};
  const chrome = {
    runtime: {id: "fixture", getURL: name => "chrome-extension://fixture/" + name,
      onMessage: event("runtime"), onInstalled: events(), onStartup: events(),
      connectNative: host => {
        const call = {host, messages: [], disconnected: false, reply: options.pairingReply ?? null,
          onMessage: event("native"), onDisconnect: event("native")};
        state.nativeCalls.push(call);
        return {
          onMessage: call.onMessage,
          onDisconnect: call.onDisconnect,
          postMessage: message => {
            call.messages.push(message);
            if (call.reply === "silent")
              return;
            queueMicrotask(() => call.onMessage.listeners.forEach(listener => listener(call.reply ?? {
              version: 1, type: "paired", wsVersion: CREDENTIAL.wsVersion, port: CREDENTIAL.port,
              instance: CREDENTIAL.instance, profile: message.profile, secret: randomHex(32)})));
          },
          disconnect: () => call.onDisconnect.listeners.forEach(listener => listener()),
        };
      }},
    storage: {local: {
      setAccessLevel: async request => {
        if (state.storageFailure)
          throw new Error("storage access denied");
        state.accessLevel = request.accessLevel;
      },
      get: async key => {
        assert.equal(state.accessLevel, "TRUSTED_CONTEXTS", "Lock storage before reading credentials");
        return key in state.stored ? {[key]: state.stored[key]} : {};
      },
      set: async items => {
        assert.equal(state.accessLevel, "TRUSTED_CONTEXTS", "Lock storage before writing credentials");
        Object.assign(state.stored, items);
      },
    }},
    windows: {getAll: async () => [window], getLastFocused: async () => window, get: async () => window,
      onCreated: (listeners.created = event("created")), onRemoved: events(),
      onBoundsChanged: (listeners.bounds = event("bounds")),
      onFocusChanged: (listeners.focus = event("focus"))},
    tabs: {onActivated: events(), sendMessage: async (tab, message, opts) => {
      sent.push({message, options: opts});
      if (message.type === "metrics") {
        state.metricReads++;
        return {width: 1000, height: 800, dpr: 1, visualScale: 1, offsetX: 0, offsetY: 0, screen: [0, 0, 2560, 1400]};
      }
      if (message.type === "route")
        return {index: 0};
      if (message.type === "begin")
        return state.leaseReply ? state.leaseReply(opts) : {renewed: true};
      if (message.type === "end")
        return {ended: true};
      if (opts?.frameId !== 0) {
        if (state.childError === "no_script")
          throw new Error("No receiving end");
        if (state.childError)
          return {status: state.childError};
        return {candidates: [[20, 20, 120, 70]]};
      }
      return {candidates: [[0, 0, 300, 200], [0, 0, 1000, 800]], page: true,
        ...(state.enterFrame ? {frame: {index: 0, matrix: [1, 0, 0, 1, 0, 0]}} : {})};
    }},
    webNavigation: {getFrame: async () => ({documentId: "root"}),
      getAllFrames: async () => [{parentFrameId: 0, frameId: 7, documentId: "child"}],
      onCommitted: events()},
    action: {setBadgeText: async text => state.badge.push(text.text), setTitle: async () => {}},
    alarms: {onAlarm: (listeners.alarm = event("alarm")),
      create: (name, info) => state.alarmsCreated.push({name, info}),
      clear: async name => state.alarmsCreated.filter(entry => entry.name === name).length},
  };

  // Every socket this worker opens is registered, so the fixture can drive the one the
  // worker is actually using - and only that one.
  class FixtureSocket extends FakeSocket {
    constructor(url, protocol, socketOptions) {
      // Every socket this fixture opens carries the desktop behaviour the test asked for.
      super(url, protocol, {...(options.socket || {}), ...(socketOptions || {})});
      state.sockets.push(this);
      // A real desktop accepts and challenges immediately, so every socket this worker
      // opens is greeted the same way. A test that wants a silent desktop sets
      // `state.silent` first.
      if (!state.silent)
        queueMicrotask(() => this.open());
    }
  }

  let timerId = 0;
  const timers = new Map();
  const context = vm.createContext({
    chrome,
    crypto: {randomUUID: () => "connection-" + (++timerId), subtle: webcrypto.subtle,
      getRandomValues: webcrypto.getRandomValues.bind(webcrypto)},
    WebSocket: FixtureSocket,
    Map, Promise, JSON, Math, Object, Array, String, Number, Boolean, Error, Uint8Array, ArrayBuffer, TextEncoder,
    console: {log() {}, warn() {}},
    setTimeout: f => { const id = ++timerId; timers.set(id, {f, kind: "timeout"}); return id; },
    clearTimeout: id => timers.delete(id),
    setInterval: f => { const id = ++timerId; timers.set(id, {f, kind: "interval"}); return id; },
    clearInterval: id => timers.delete(id),
  });
  vm.runInContext(fs.readFileSync(WORKER, "utf8"), context);

  // The handshake awaits WebCrypto, whose promises settle on the macrotask queue, so the
  // fixture yields the whole event loop instead of only draining microtasks.
  const flush = async (rounds = 8) => {
    for (let i = 0; i < rounds; i++)
      await new Promise(resolve => setImmediate(resolve));
  };
  // Bounded by real time, not by a round count: a fixed round budget turns a loaded
  // machine into a red gate.
  const waitFor = async (predicate, timeoutMs = 3000) => {
    const deadline = Date.now() + timeoutMs;
    do {
      await flush(2);
      if (predicate())
        return true;
    } while (Date.now() < deadline);
    return predicate();
  };
  const fireTimers = async kind => {
    for (const [id, timer] of [...timers]) {
      if (timer.kind !== kind)
        continue;
      // A one-shot timer is consumed when it fires; an interval keeps running, exactly
      // like the platform timer it stands in for.
      if (kind === "timeout")
        timers.delete(id);
      timer.f();
    }
    await flush(8);
  };
  const run = code => vm.runInContext(code, context);
  const ready = () => run('!!current && current.state === "ready"');
  const openSockets = () => state.sockets.filter(socket => !socket.closed);
  const posts = () => state.sockets.flatMap(socket => socket.sent);
  const lastPost = type => posts().filter(message => message.type === type).pop();
  // Fires a captured browser event the way the platform would.
  const emit = (name, ...args) => listeners[name].listeners.forEach(listener => listener(...args));
  return {context, state, sent, listeners, timers, window, chrome, flush, waitFor, fireTimers, run, ready,
    openSockets, posts, lastPost, emit,
    lastSocket: () => openSockets()[openSockets().length - 1],
    // The identity and the session the live connection owns, read through the worker's
    // own objects rather than re-derived by the test.
    contextId: () => run('current ? current.id : ""'),
    // Returned as a plain string: an array built inside the VM carries the VM realm's
    // prototype and would fail a strict deep comparison for the wrong reason.
    session: () => run(`current && current.session ? {sessionId: current.session.sessionId,
      tabId: current.session.tabId, invalid: !!current.session.invalid, frozen: !!current.session.frozen,
      frames: [...current.session.frames.keys()].sort((a, b) => a - b).join(",")} : null`),
    // The real receive entry: what the desktop does with every frame it sends.
    deliver: message => {
      const socket = openSockets()[openSockets().length - 1];
      assert(socket, "no live socket to deliver into");
      socket.deliver(message);
    }};
}

async function create(options = {}) {
  const harness = createSocketFixture({stored: {zencropPairingV2: CREDENTIAL}, ...options});
  const ready = await harness.waitFor(() => harness.ready());
  assert(ready, "the fixture connection reached the authenticated state");
  await harness.flush();
  return harness;
}

// A query exactly as the desktop sends it, through the socket, answered by the worker.
async function ask(harness, overrides = {}) {
  const message = {type: "inspect", version: 1, sessionId: "session", requestId: String(harness.posts().length + 1),
    connectionId: harness.contextId(), windowId: 1, frozen: true, point: [50, 50],
    viewport: [0, 0, 1000, 800], ...overrides};
  const socket = harness.lastSocket();
  const before = socket.sent.length;
  harness.deliver(message);
  const reply = () => socket.sent.slice(before).find(frame =>
    ["result", "unavailable"].includes(frame.type) && frame.sessionId === message.sessionId &&
    frame.requestId === message.requestId && frame.connectionId === message.connectionId);
  const answered = await harness.waitFor(() => reply());
  assert(answered, "the query was answered through the socket entry");
  return reply();
}

// ---------------------------------------------------------------------- business logic

async function checkPageCandidates() {
  let contentHandler;
  const root = {clientWidth: 1000, clientHeight: 800, parentElement: null,
    getBoundingClientRect: () => ({left: 0, top: 0, right: 1000, bottom: 800}), getRootNode: () => document};
  const panel = {parentElement: root, getBoundingClientRect: () => ({left: 10, top: 10, right: 400, bottom: 300})};
  const leaf = {parentElement: panel, getBoundingClientRect: () => ({left: 20, top: 20, right: 120, bottom: 70})};
  const document = {documentElement: root, elementFromPoint: () => leaf};
  const contentContext = vm.createContext({document, innerWidth: 1000, innerHeight: 800, devicePixelRatio: 1,
    visualViewport: null,
    chrome: {runtime: {id: "fixture", onMessage: {addListener: listener => {contentHandler = listener;}},
      sendMessage: async () => {}}},
    MutationObserver: class {observe() {} disconnect() {}}, HTMLIFrameElement: class {}, HTMLFrameElement: class {},
    addEventListener() {}, removeEventListener() {}, setTimeout: () => 1, clearTimeout() {}});
  vm.runInContext(fs.readFileSync(CONTENT, "utf8"), contentContext);
  const inspect = (sessionId, point = [50, 50]) =>
    new Promise(resolve => contentHandler({type: "inspect", sessionId, point}, {id: "fixture"}, resolve));
  const full = await inspect("page");
  assert.equal(full.page, true, "A root covering the visible document is a page candidate");
  root.getBoundingClientRect = () => ({left: 0, top: 0, right: 1000, bottom: 799.6});
  assert.equal((await inspect("fractional-page")).page, true,
    "Integer clientHeight rounding cannot turn a whole page into a locked ordinary region");
  root.getBoundingClientRect = () => ({left: 0, top: 0, right: 1000, bottom: 800});
  assert.equal((await inspect("page", [-1, 50])).status, "outside_viewport", "Outside viewport is a point miss");
  document.elementFromPoint = () => null;
  assert.equal((await inspect("page")).status, "empty_hit", "Missing element is a point miss");
  document.elementFromPoint = () => ({parentElement: null, getRootNode: () => document,
    getBoundingClientRect: () => ({left: 50, top: 50, right: 51, bottom: 51})});
  assert.equal((await inspect("page")).status, "empty_hit",
    "Empty candidate path never produces an invalid result message");
  document.elementFromPoint = () => leaf;
  assert((await inspect("page")).candidates.length, "Same content session recovers after point misses");
  const edge = {parentElement: root, getBoundingClientRect: () => ({left: 900, top: 10, right: 1200, bottom: 300})};
  document.elementFromPoint = () => edge;
  const fractionalClip = await new Promise(resolve => contentHandler({type: "inspect", sessionId: "fractional-clip",
    point: [950, 50], viewportSize: [1000.28, 800.28]}, {id: "fixture"}, resolve));
  assert.equal(fractionalClip.candidates[0][2], 1000.28,
    "High zoom clips at the precise physical viewport boundary rather than rounded innerWidth");
  document.elementFromPoint = () => leaf;
  const iframe = vm.runInContext("new HTMLIFrameElement()", contentContext);
  Object.assign(iframe, {parentElement: root, offsetWidth: 300, offsetHeight: 200,
    getBoundingClientRect: () => ({left: 0, top: 0, right: 300, bottom: 200, width: 300, height: 200})});
  contentContext.DOMMatrix = class {constructor() {this.is2D = false;}};
  contentContext.getComputedStyle = () => ({transform: "matrix3d(...)"});
  document.elementFromPoint = () => iframe;
  const unsupported = await inspect("unsupported-frame");
  assert(!unsupported.status && unsupported.candidates.length && !unsupported.frame,
    "A 3D iframe keeps its safe containing rectangle");
  document.elementFromPoint = () => leaf;
  root.getBoundingClientRect = () => ({left: 0, top: 0, right: 1000, bottom: 300});
  assert.equal((await inspect("small-root")).page, false,
    "A small terminal root must keep ordinary region locking");

  // The worker's own traversal entry, with the session it is given rather than a global.
  const harness = createSocketFixture({stored: {zencropPairingV2: CREDENTIAL}});
  const {chrome: workerChrome, context} = harness;
  const withSession = (body, childCandidates) => {
    workerChrome.webNavigation.getAllFrames = async () => [{parentFrameId: 0, frameId: 7, documentId: "child"}];
    workerChrome.tabs.sendMessage = async (tab, message, options) => message.type === "route" ? {index: 0}
      : options.frameId === 0 ? {candidates: [[0, 0, 300, 200], [0, 0, 1000, 800]], page: true,
        frame: {matrix: [1, 0, 0, 1, 0, 0], index: 0}} : childCandidates;
    return vm.runInContext(`(() => { const context = {session: {sessionId: 'page', frames: new Map()}};
      return ${body}; })()`, context);
  };
  const plainChild = {candidates: [[20, 20, 120, 70]]};
  assert.equal((await withSession("inspectFrame(context, 1, 0, 'root', [50, 50], {sessionId: 'page'})",
    plainChild)).page, true, "Top document page marker survives routing");
  assert.equal((await withSession("inspectFrame(context, 1, 7, 'child', [50, 50], {sessionId: 'page'}, 1)",
    plainChild)).page, false, "An iframe root never becomes the whole browser page");
  assert.equal((await withSession("inspectFrame(context, 1, 0, 'root', [50, 50], {sessionId: 'page'})",
    {candidates: Array.from({length: 2}, () => [20, 20, 120, 70]), page: true})).page, true,
    "The top document page marker survives iframe traversal");
  assert.equal((await withSession("inspectFrame(context, 1, 0, 'root', [50, 50], {sessionId: 'page'})",
    {candidates: Array.from({length: 64}, () => [20, 20, 120, 70]), page: true})).page, false,
    "A truncated chain cannot mark a child candidate as the whole page");
}

async function checkMutationGeometry() {
  let listener, mutation, scrollListener;
  const observed = [];
  const sent = [];
  const root = {isConnected: true, clientWidth: 1000, clientHeight: 800, parentElement: null,
    getBoundingClientRect: () => ({left: 0, top: 0, right: 1000, bottom: 800}), getRootNode: () => document};
  const panel = {isConnected: true, parentElement: root,
    getBoundingClientRect: () => ({left: 10, top: 10, right: 400, bottom: 300})};
  const leaf = {isConnected: true, parentElement: panel,
    getBoundingClientRect: () => ({left: 20, top: 20, right: 120, bottom: 70})};
  const replacement = {isConnected: true, parentElement: panel,
    getBoundingClientRect: () => ({left: 20, top: 20, right: 120, bottom: 70})};
  const document = {documentElement: root, elementFromPoint: () => leaf};
  const content = vm.createContext({document, innerWidth: 1000, innerHeight: 800, devicePixelRatio: 1,
    visualViewport: null,
    chrome: {runtime: {id: "fixture", sendMessage: async message => {sent.push(message);},
      onMessage: {addListener: value => {listener = value;}}}},
    MutationObserver: class {constructor(cb) {mutation = cb;} observe(target) {observed.push(target);} disconnect() {}},
    HTMLIFrameElement: class {}, HTMLFrameElement: class {},
    addEventListener: (name, cb) => {if (name === "scroll") scrollListener = cb;}, removeEventListener() {},
    setTimeout: () => 1, clearTimeout() {}});
  vm.runInContext(fs.readFileSync(CONTENT, "utf8"), content);
  const inspect = id => new Promise(resolve =>
    listener({type: "inspect", sessionId: id, frozen: true, point: [50, 50]}, {id: "fixture"}, resolve));
  await inspect("stable");
  mutation([{type: "attributes", attributeName: "class", target: {}}]);
  mutation([{type: "attributes", attributeName: "style", target: leaf}]);
  assert.equal(sent.length, 0, "Unrelated carousel mutations and unchanged leaf styles preserve frozen geometry");
  let leafRight = 140;
  leaf.getBoundingClientRect = () => ({left: 20, top: 20, right: leafRight, bottom: 70});
  mutation([{type: "attributes", attributeName: "style", target: leaf}]);
  assert.equal(sent.length, 1, "A resized candidate invalidates frozen geometry");
  assert.equal(sent[0].sessionId, "stable");
  leafRight = 120;
  await inspect("replacement");
  document.elementFromPoint = () => replacement;
  mutation([{type: "childList", target: panel}]);
  assert.equal(sent.length, 2,
    "A replacement or overlay at the hit point invalidates even with the same rectangle");
  document.elementFromPoint = () => leaf;
  await inspect("parent");
  panel.getBoundingClientRect = () => ({left: 10, top: 10, right: 450, bottom: 300});
  mutation([{type: "attributes", target: panel}]);
  assert.equal(sent.length, 3, "A moved ancestor invalidates the wheel candidate path");
  panel.getBoundingClientRect = () => ({left: 10, top: 10, right: 400, bottom: 300});
  await inspect("scroll");
  scrollListener();
  assert.equal(sent.length, 4, "Scrolling remains terminal");
  await inspect("ended");
  listener({type: "end", sessionId: "ended"}, {id: "fixture"}, () => {});
  leafRight = 160;
  mutation([{type: "attributes", target: leaf}]);
  assert.equal(sent.length, 4, "Ending selection removes the active geometry references");
  const shadow = {elementFromPoint: () => leaf, addEventListener() {}, removeEventListener() {}};
  leaf.shadowRoot = shadow;
  await inspect("shadow");
  await inspect("shadow");
  mutation([]);
  assert.equal(observed.filter(target => target === shadow).length, 1,
    "A visited ShadowRoot is observed only once per session");
}

async function checkSessionOwnership() {
  // A query over the socket owns its session, and a disconnect releases it.
  {
    const harness = await create();
    assert.equal((await ask(harness)).type, "result", "A query sent into the socket is answered");
    assert(harness.session(), "The session belongs to the connection that created it");
    assert.equal(harness.session().frames, "0,7",
      "The session leases the root document and the visited iframe it actually read");
    harness.lastSocket().close();
    await harness.flush();
    assert.equal(harness.session(), null, "A disconnected connection releases its session immediately");
  }

  // A query whose release is still in flight must not adopt the new connection: this is
  // the shape that used to let an old task overwrite a newer session.
  {
    const harness = await create();
    assert.equal((await ask(harness, {sessionId: "old"})).type, "result");
    let releaseEnd;
    harness.chrome.tabs.sendMessage = async (tab, message, options) => {
      if (message.type === "end")
        await new Promise(resolve => {releaseEnd = resolve;});
      return {candidates: [[0, 0, 300, 200], [0, 0, 1000, 800]], page: true};
    };
    // A new query waits for the previous session to be released...
    harness.deliver({type: "inspect", version: 1, sessionId: "new", requestId: "2",
      connectionId: harness.contextId(), windowId: 1, frozen: true, point: [50, 50],
      viewport: [0, 0, 1000, 800]});
    await harness.flush();
    // ...and the connection dies while that release is pending.
    harness.lastSocket().close();
    await harness.flush();
    releaseEnd?.();
    await harness.flush(20);
    assert.equal(harness.session(), null, "A query interrupted by disconnect adopts no session");
    assert(!harness.lastPost("result") || harness.lastPost("result").sessionId === "old",
      "No session is created or answered for a connection that is already gone");
  }

  // The lease renews on its own clock while the session lives, and stops with it.
  {
    const lease = await create();
    assert.equal((await ask(lease)).type, "result");
    lease.state.enterFrame = false;
    for (let round = 0; round < 4; round++) {
      await lease.fireTimers("interval");
      assert.equal((await ask(lease)).type, "result");
    }
    assert.equal(lease.sent.filter(entry => entry.options?.frameId === 7 && entry.message.lease).length, 4,
      "Frozen sessions renew the visited iframe lease at the documented interval");
    lease.lastSocket().close();
    await lease.flush();
    const before = lease.sent.length;
    await lease.fireTimers("interval");
    assert.equal(lease.sent.length, before, "A released session stops renewing its lease");
  }

  // A frame that cannot be inspected is dropped from the lease and never retried.
  for (const error of ["no_script", "not_authorized", "frame_not_authorized", "empty_frame",
    "outside_viewport", "unsupported_frame_transform"]) {
    const harness = await create({childError: error});
    assert.equal((await ask(harness)).type, "result",
      "The containing document stays selectable when a frame cannot be inspected");
    assert(!harness.session().frames.split(",").includes("7"), "A failed frame is not leased");
    await harness.fireTimers("interval");
    assert.equal(harness.sent.filter(entry => entry.options?.frameId === 7 && entry.message.lease).length, 0,
      "A failed frame is never retried");
    harness.state.enterFrame = false;
    assert.equal((await ask(harness)).type, "result");
  }

  // A frame that reports invalidated geometry ends the frozen selection.
  {
    const harness = await create({childError: "geometry_invalidated"});
    assert.equal((await ask(harness)).status, "geometry_invalidated");
  }

  // A window event invalidates the session of the connection that owns it, and only
  // that connection's.
  {
    const harness = await create();
    assert.equal((await ask(harness)).type, "result");
    assert(harness.session(), "the session is live before the window moves");
    harness.emit("bounds", harness.window);
    assert(await harness.waitFor(() => harness.lastPost("invalidated")), "the move invalidated the session");
    // A frozen session is downgraded rather than dropped, and its lease is released:
    // the page is no longer read for it.
    assert.deepEqual(JSON.parse(JSON.stringify(harness.session())),
      {sessionId: "session", tabId: 1, invalid: true, frozen: true, frames: ""},
      "A frozen session is marked invalid and releases its lease when its window moves");
  }
}

async function checkCoordinateMapping() {
  {
    const harness = await create();
    assert.equal((await ask(harness)).type, "result");
    const failed = await ask(harness, {requestId: "outside", point: [1500, 50]});
    assert.equal(failed.type, "unavailable", "An earlier result cannot hide the current query's failure");
    assert.equal(failed.requestId, "outside");
    assert.equal(failed.status, "outside_viewport");
  }
  // Page zoom is applied once, from device pixel ratio times visual scale.
  {
    const harness = await create();
    harness.chrome.tabs.sendMessage = async (tab, message, options) =>
      message.type === "metrics"
        ? {width: 1000, height: 800, dpr: 5, visualScale: 1, offsetX: 0, offsetY: 0, screen: [0, 0, 2560, 1400]}
        : {candidates: [[0, 0, 300, 200], [0, 0, 1000, 800]], page: true,
          frame: {matrix: [1, 0, 0, 1, 0, 0], index: 0}};
    const reply = await ask(harness, {point: [250, 250], viewport: [0, 0, 5000, 4000]});
    assert.equal(reply.type, "result", "A 500% page zoom still answers with candidates");
    assert.equal(reply.scale, 5, "Page zoom is reported as a single device scale");
    // The page zoom is carried by `scale`, never folded into the coordinates.
    assert.deepEqual(reply.candidates[0], [0, 0, 300, 200], "Zoomed coordinates are mapped back once");
  }

  // Two viewports that match the page equally are a miss, not a guess.
  {
    const harness = await create();
    assert.equal((await ask(harness, {viewport: [0, 0, 1000, 800],
      viewports: [[0, 0, 1000, 800], [0, 0, 1000, 800]]})).status, "viewport_ambiguous");
  }

  // A point outside the matched viewport is a miss rather than a clamped selection.
  {
    const harness = await create();
    assert.equal((await ask(harness, {point: [1500, 50]})).status, "outside_viewport");
  }
}

async function checkWindowIdentity() {
  const harness = await create();
  harness.window.focused = false;
  harness.emit("focus", -1);
  await harness.waitFor(() => harness.lastPost("hello"));
  const hello = harness.lastPost("hello");
  assert.equal(hello.windows[0].id, 1, "Losing focus does not lose the browser window identity");
  assert.equal(hello.windows[0].lastFocused, true);
  assert.deepEqual(hello.windows[0].screen, [0, 0, 2560, 1400],
    "Screen metrics are reported for mixed-DPI mapping");
}

// ------------------------------------------------------------------ connection contracts

async function checkReconnectIsolation() {
  const harness = await create();
  const firstId = harness.contextId();
  const firstSocket = harness.lastSocket();
  assert(firstId, "The first connection has an identity");
  assert.equal((await ask(harness, {sessionId: "first"})).type, "result", "The first connection answers");
  const firstPosts = firstSocket.sent.length;

  // Drop the connection and let the worker's own prompt retry run: this is the path a
  // desktop restart takes, and it is the only one that proves the new connection exists.
  firstSocket.close();
  await harness.flush();
  assert.equal(harness.state.sockets.length, 1, "A closed connection does not open another by itself");
  assert.equal(harness.timers.size > 0, true, "The worker scheduled its own retry");
  assert.equal(harness.state.alarmsCreated.at(-1).info.delayInMinutes, 0.5,
    "A dropped connection also schedules a 30-second browser alarm");
  await harness.fireTimers("timeout");
  const opened = await harness.waitFor(() => harness.state.sockets.length === 2);
  assert(opened, "The prompt retry opened a second socket");
  assert(await harness.waitFor(() => harness.ready()), "The second connection authenticated");
  const second = harness.lastSocket();
  assert.notEqual(second, firstSocket, "The retry is a new socket, not the closed one");
  const secondId = harness.contextId();
  assert(secondId && secondId !== firstId, "A reconnect gets a fresh connection identity");
  assert.equal(harness.session(), null, "The new connection starts without a session");

  // Work stamped with the old connection's identity is refused by the real entry.
  const before = harness.posts().length;
  harness.deliver({type: "inspect", version: 1, sessionId: "stale", requestId: "99",
    connectionId: firstId, windowId: 1, frozen: true, point: [50, 50], viewport: [0, 0, 1000, 800]});
  await harness.flush(20);
  assert.equal(harness.posts().length, before, "A frame from a superseded connection produces nothing");
  assert.equal(harness.session(), null, "A frame from a superseded connection creates no session");
  assert.equal(firstSocket.sent.length, firstPosts, "The closed connection received nothing further");

  // The live connection still works, on its own session.
  assert.equal((await ask(harness, {sessionId: "second"})).type, "result",
    "The reconnected connection answers normally");
  assert.equal(harness.session().sessionId, "second", "The new session belongs to the new connection");

  const signing = createSocketFixture({stored: {zencropPairingV2: CREDENTIAL}});
  signing.run(`(() => {
    const subtle = crypto.subtle;
    crypto.subtle = {
      importKey: subtle.importKey.bind(subtle), verify: subtle.verify.bind(subtle),
      sign: async (...args) => {
        await new Promise(resolve => globalThis.releaseProof = resolve);
        return subtle.sign(...args);
      }
    };
    globalThis.restoreCrypto = () => { crypto.subtle = subtle; };
  })()`);
  assert(await signing.waitFor(() => signing.run('typeof releaseProof === "function"')));
  const superseded = signing.lastSocket();
  superseded.close();
  signing.run("restoreCrypto()");
  await signing.fireTimers("timeout");
  assert(await signing.waitFor(() => signing.state.sockets.length === 2 && signing.ready()));
  signing.run("releaseProof()");
  await signing.flush(20);
  assert(!superseded.sent.some(frame => frame.type === "auth_client"),
    "Signing that finishes after reconnect cannot send proof on the superseded socket");
}

async function checkTransportContracts() {
  // Unpaired means silent: no window list, and above all no page read.
  {
    const harness = createSocketFixture({stored: {}});
    await harness.flush(40);
    assert.equal(harness.state.sockets.length, 0, "An unpaired worker opens no connection");
    assert.equal(harness.state.nativeCalls.length, 0, "An unpaired worker launches no host");
    assert.equal(harness.state.metricReads, 0, "An unpaired worker reads no page");
    assert.equal(harness.run("connectionStatus.state"), "unpaired");
  }

  // A connection that has not finished authenticating is silent too: the desktop has
  // proved itself, but auth_ok never arrived, so this side is not ready.
  {
    const harness = createSocketFixture({stored: {zencropPairingV2: CREDENTIAL}, socket: {dropAck: true}});
    const proven = await harness.waitFor(() =>
      harness.state.sockets[0]?.sent.some(message => message.type === "auth_client"));
    assert(proven, "The fixture desktop received the client proof");
    const socket = harness.state.sockets[0];
    socket.deliver({version: 1, type: "hello", connectionId: "x", windows: []});
    socket.deliver({type: "inspect", version: 1, sessionId: "s", requestId: "1", connectionId: "x",
      windowId: 1, point: [10, 10], viewport: [0, 0, 1000, 800]});
    await harness.flush(20);
    assert.equal(harness.state.metricReads, 0, "Nothing is read from a page before auth_ok");
    assert.equal(socket.sent.filter(message => message.type === "hello").length, 0,
      "No window list is disclosed before auth_ok");
    assert(!harness.ready(), "A half-authenticated connection is not ready");
  }

  // The desktop must prove itself first: a forged proof stops the exchange.
  {
    const harness = createSocketFixture({stored: {zencropPairingV2: CREDENTIAL},
      socket: {corruptServerProof: true}});
    const closed = await harness.waitFor(() => harness.state.sockets[0]?.closed);
    assert(closed, "A wrong server proof closes the connection");
    const socket = harness.state.sockets[0];
    assert.equal(socket.sent.filter(message => message.type === "auth_client").length, 0,
      "This side never proves itself back to an unproven server");
    assert.equal(socket.sent.filter(message => message.type === "hello").length, 0,
      "No window list is disclosed to an unproven server");
    assert.equal(harness.state.metricReads, 0, "No page is read for an unproven server");
  }

  // The desktop's first frame must be a challenge: anything else is out of order.
  {
    const harness = createSocketFixture({stored: {zencropPairingV2: CREDENTIAL}});
    harness.state.silent = true;
    assert(await harness.waitFor(() => harness.state.sockets.length > 0), "A paired worker opens a connection");
    const socket = harness.state.sockets[0];
    assert.equal(socket.sent.length, 0, "Nothing is sent before the desktop has spoken");
    socket.deliver({version: 2, type: "pong"}, true);
    const closed = await harness.waitFor(() => socket.closed);
    assert(closed, "A control frame out of order closes the connection");
    assert.equal(socket.sent.length, 0, "An out-of-order frame produces no reply");
  }

  // The heartbeat runs only on a ready socket, and a silent desktop ends the connection.
  {
    const harness = await create();
    const socket = harness.lastSocket();
    await harness.fireTimers("interval");
    assert.equal(socket.pings, 1, "A ready connection sends its heartbeat");
    assert.equal(socket.sent.filter(message => message.type === "hello").length, 1,
      "The heartbeat does not resend the window list");
    socket.options.silentPong = true;
    harness.run("current.lastPong = Date.now() - 61000");
    await harness.fireTimers("interval");
    assert(socket.closed, "A desktop that stops answering the heartbeat is dropped");
    await harness.flush(20);
    assert.equal(harness.run("connectionStatus.state"), "disconnected");
  }

  // A malformed frame is ignored; the connection keeps working.
  {
    const harness = await create();
    const socket = harness.lastSocket();
    socket.deliver("{not json");
    socket.deliver("[1,2,3]");
    socket.deliver({version: 3, type: "hello"});
    socket.deliver({version: 1});
    await harness.flush(20);
    assert(!socket.closed, "A malformed frame does not drop a healthy connection");
    assert.equal((await ask(harness)).type, "result", "The connection still answers after malformed frames");
  }

  // Storage access must be locked down before the secret is touched.
  {
    const harness = createSocketFixture({stored: {zencropPairingV2: CREDENTIAL}, storageFailure: true});
    await harness.flush(40);
    assert.equal(harness.state.sockets.length, 0, "A storage access failure stops the connection");
    assert.equal(harness.state.nativeCalls.length, 0, "A storage access failure launches no host");
    assert.equal(harness.run("connectionStatus.state"), "error");
    assert.equal(harness.run("connectionStatus.reason"), "credential_storage_unavailable");
  }

  // The secret is only ever read after the storage access level is set.
  {
    const harness = await create();
    assert.equal(harness.state.accessLevel, "TRUSTED_CONTEXTS",
      "The pairing secret is only read with trusted-context storage access");
  }

  // Exactly one native messaging call site, and only the connection page can reach it.
  {
    const source = fs.readFileSync(WORKER, "utf8");
    assert.equal((source.match(/connectNative\s*\(/g) || []).length, 1,
      "The extension has exactly one native messaging call site");
    const harness = createSocketFixture({stored: {}});
    await harness.flush(40);
    harness.chrome.runtime.onMessage.listeners.forEach(listener => {
      const respond = () => {};
      listener({type: "pair"}, {id: "fixture", url: harness.chrome.runtime.getURL("connection.html")}, respond);
      listener({type: "pair"}, {id: "fixture", tab: {id: 1}, url: "https://page.test"}, respond);
      listener({type: "pair"}, {id: "other", url: harness.chrome.runtime.getURL("connection.html")}, respond);
    });
    await harness.flush(20);
    assert.equal(harness.state.nativeCalls.length, 1,
      "Only the extension's own connection page can start a pairing bootstrap");
    assert.equal(harness.state.stored.zencropPairingV2.profile, harness.state.nativeCalls[0].messages[0].profile);
    assert.equal(harness.state.stored.zencropPairingV2.wsVersion, 2,
      "Pairing stores the negotiated direct-connection credential");
  }

  // The connection page is the only reader of the status, and the status never carries a
  // credential.
  {
    const harness = await create();
    let status;
    harness.chrome.runtime.onMessage.listeners.forEach(listener =>
      listener({type: "connection_status"},
        {id: "fixture", url: harness.chrome.runtime.getURL("connection.html")}, value => {status = value;}));
    assert.equal(status.state, "connected");
    assert(!JSON.stringify(status).includes(CREDENTIAL.secret), "The reported status carries no secret");
  }
}

async function checkPairingContracts() {
  {
    const harness = await create({pairingReply: "silent"});
    harness.lastSocket().close();
    let resumeLoad;
    const original = harness.chrome.storage.local.get;
    harness.chrome.storage.local.get = async key => {
      harness.chrome.storage.local.get = original;
      await new Promise(resolve => { resumeLoad = resolve; });
      return original(key);
    };
    const connecting = harness.run("connect()");
    assert(await harness.waitFor(() => resumeLoad));
    const pairing = harness.run("pair()");
    assert(await harness.waitFor(() => harness.state.nativeCalls.length === 1));
    resumeLoad();
    await connecting;
    assert.equal(harness.openSockets().length, 0, "A pending credential read cannot connect during pairing");
    await harness.fireTimers("timeout");
    await pairing;
  }
  // A failed pairing with no prior credential leaves the worker unpaired and silent.
  {
    const harness = createSocketFixture({stored: {}, pairingReply: {version: 1, type: "pairing_error",
      reason: "desktop_unavailable"}});
    await harness.flush(40);
    await harness.run("pair()");
    await harness.flush(40);
    assert.equal(harness.state.nativeCalls.length, 1, "One explicit pairing starts one bootstrap");
    assert.equal(harness.run("connectionStatus.state"), "error");
    assert.equal(harness.run("connectionStatus.reason"), "desktop_unavailable");
    assert.equal(harness.state.sockets.length, 0, "A failed pairing opens no connection");
  }

  // An incompatible or unrequested reply is refused rather than stored.
  {
    const harness = createSocketFixture({stored: {}, pairingReply: {version: 1, type: "paired",
      wsVersion: 2, port: 19227, instance: CREDENTIAL.instance, profile: randomHex(16), secret: randomHex(32)}});
    await harness.flush(40);
    await harness.run("pair()");
    await harness.flush(40);
    assert.equal(harness.run("connectionStatus.state"), "error");
    assert(!("zencropPairingV2" in harness.state.stored),
      "A reply for another profile is never stored");
  }

  // A failed re-pairing resumes the connection that was already working.
  {
    const harness = await create({pairingReply: {version: 1, type: "pairing_error", reason: "port_unavailable"}});
    const first = harness.lastSocket();
    await harness.run("pair()");
    await harness.flush(40);
    assert(first.closed, "Pairing replaces the established connection");
    assert.equal(harness.state.stored.zencropPairingV2.secret, CREDENTIAL.secret,
      "A failed pairing keeps the previous credential");
    assert(await harness.waitFor(() => harness.ready()), "A failed pairing resumes the working connection");
  }

  // The bootstrap is bounded, and a timeout is reported rather than retried.
  {
    const harness = createSocketFixture({stored: {}, pairingReply: "silent"});
    await harness.flush(40);
    // Deliberately not awaited before the deadline fires: a silent desktop is exactly
    // the case that must be ended by the bootstrap's own timeout.
    const pending = harness.run("pair()");
    await harness.flush(20);
    await harness.fireTimers("timeout");
    await pending;
    await harness.flush(40);
    assert.equal(harness.run("connectionStatus.reason"), "pairing_timeout",
      "A silent desktop ends pairing instead of holding it open");
    assert.equal(harness.state.nativeCalls.length, 1, "A pairing timeout is never retried in a loop");
    assert(!("zencropPairingV2" in harness.state.stored), "A timed-out pairing stores nothing");
  }
}

async function main() {
  // The desktop-side browser guard must still short-circuit before accessibility.
  const detector = fs.readFileSync(DETECTOR, "utf8");
  assert(!/WarmBrowserMsaa|TouchUiaPointThroughOverlay|WM_GETOBJECT|uiautomation\.h/.test(detector),
    "Browser warmup/AX path removed");
  for (const [entry, next] of [["EnsureAccessibleRoot", "MakeElement"], ["GetRectByPoint", "GetParentRect"],
    ["GetParentRect", "GetChildRect"], ["GetChildRect", "ClearCache"]]) {
    const body = detector.slice(detector.indexOf("SmartDetector::" + entry + "("),
      detector.indexOf("SmartDetector::" + next + "("));
    assert(body.includes("browser_companion::IsBrowserWindow"), "Browser guard at " + entry);
    if (entry === "EnsureAccessibleRoot")
      assert(body.indexOf("browser_companion::IsBrowserWindow") < body.indexOf("AccessibleObjectFromWindow"));
  }

  await checkPageCandidates();
  await checkMutationGeometry();
  await checkSessionOwnership();
  await checkCoordinateMapping();
  await checkWindowIdentity();
  await checkReconnectIsolation();
  await checkTransportContracts();
  await checkPairingContracts();
  console.log("PASS: page candidates, mutation geometry, session ownership, coordinate mapping,");
  console.log("      window identity, reconnect isolation, transport and pairing contracts");
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
