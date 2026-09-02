/* 冒烟测试 + 数值验证：Node + 最小 DOM/Canvas stub，真实执行 monitor-demo.html 内的 JS。
   A) 冒烟：驱动主循环、遍历全部视图与交互，捕获运行时异常并断言渲染非空。
   B) 数值：多次独立重复「采集力数据 → 最小二乘求解」，验证重力补偿能解出真值。 */
"use strict";
const fs = require("fs");
const vm = require("vm");
const path = require("path");

const FILE = path.join(__dirname, "monitor-demo.html");
const SRC = fs.readFileSync(FILE, "utf8");
const MM = SRC.match(/<script>([\s\S]*?)<\/script>/);
if (!MM) { console.error("FATAL: 未找到 <script>"); process.exit(1); }
const JS = MM[1];
const HTML = SRC.slice(0, MM.index);

/* ==================== 迷你 HTML 解析 + DOM ==================== */
const VOID = new Set(["area", "base", "br", "col", "embed", "hr", "img",
  "input", "link", "meta", "param", "source", "track", "wbr"]);
let REG = Object.create(null);

function tnode(t) {
  return {
    nodeType: 3, _text: String(t),
    get textContent() { return this._text; },
    set textContent(v) { this._text = String(v); },
  };
}

function El(tag) {
  this.tagName = String(tag).toUpperCase();
  this.nodeType = 1;
  this.attrs = Object.create(null);
  this.childNodes = [];
  this.parentNode = null;
  this.listeners = Object.create(null);
  this.style = {};
  this.dataset = {};
  this._classes = new Set();
  this._text = "";
  this.id = "";
  this.value = "";
  this.checked = false;
  this.disabled = false;
  this._ctx = null;
}
Object.defineProperty(El.prototype, "className", {
  get() { return [...this._classes].join(" "); },
  set(v) { this._classes = new Set(String(v).split(/\s+/).filter(Boolean)); },
});
Object.defineProperty(El.prototype, "classList", {
  get() {
    const el = this;
    const sync = () => { el.attrs.class = el.className; };
    return {
      add() { for (const c of arguments) el._classes.add(c); sync(); },
      remove() { for (const c of arguments) el._classes.delete(c); sync(); },
      toggle(c, force) {
        const on = force === undefined ? !el._classes.has(c) : !!force;
        if (on) el._classes.add(c); else el._classes.delete(c);
        sync(); return on;
      },
      contains(c) { return el._classes.has(c); },
    };
  },
});
// 无真实 layout，给稳定默认值；可按元素用 _cw/_ch 覆盖
["clientWidth", "offsetWidth"].forEach((k) => Object.defineProperty(El.prototype, k, {
  get() { return this._cw !== undefined ? this._cw : 640; },
  set(v) { this._cw = v; },
}));
["clientHeight", "offsetHeight"].forEach((k) => Object.defineProperty(El.prototype, k, {
  get() { return this._ch !== undefined ? this._ch : 240; },
  set(v) { this._ch = v; },
}));
Object.defineProperty(El.prototype, "children", {
  get() { return this.childNodes.filter((c) => c.nodeType === 1); },
});
Object.defineProperty(El.prototype, "firstChild", {
  get() { return this.childNodes[0] || null; },
});
Object.defineProperty(El.prototype, "lastChild", {
  get() { return this.childNodes[this.childNodes.length - 1] || null; },
});
Object.defineProperty(El.prototype, "textContent", {
  get() {
    if (this.nodeType === 3) return this._text;
    return this.childNodes.map((c) => c.textContent).join("");
  },
  set(v) {
    this.childNodes.length = 0;
    this._text = "";
    if (v !== "") this.childNodes.push(tnode(v));
  },
});
Object.defineProperty(El.prototype, "innerHTML", {
  get() { return this.childNodes.map(serial).join(""); },
  set(v) { this.childNodes.length = 0; this._text = ""; parseHTML(String(v), this); },
});
function serial(n) {
  if (n.nodeType === 3) return n._text;
  const a = Object.keys(n.attrs).map((k) => ` ${k}="${n.attrs[k]}"`).join("");
  const t = n.tagName.toLowerCase();
  return `<${t}${a}>${n.childNodes.map(serial).join("")}</${t}>`;
}
El.prototype.appendChild = function (c) { c.parentNode = this; this.childNodes.push(c); return c; };
El.prototype.insertBefore = function (c, ref) {
  c.parentNode = this;
  const i = ref ? this.childNodes.indexOf(ref) : -1;
  if (i < 0) this.childNodes.push(c); else this.childNodes.splice(i, 0, c);
  return c;
};
El.prototype.removeChild = function (c) {
  const i = this.childNodes.indexOf(c);
  if (i >= 0) { this.childNodes.splice(i, 1); c.parentNode = null; }
  return c;
};
El.prototype.remove = function () {
  if (this.parentNode) this.parentNode.removeChild(this);
};
El.prototype.setAttribute = function (k, v) {
  this.attrs[k] = String(v);
  if (k === "class") this.className = v;
  if (k === "id") { this.id = v; REG[v] = this; }
  if (k.startsWith("data-")) this.dataset[k.slice(5).replace(/-(\w)/g, (_, c) => c.toUpperCase())] = String(v);
};
El.prototype.getAttribute = function (k) { return k in this.attrs ? this.attrs[k] : null; };
El.prototype.hasAttribute = function (k) { return k in this.attrs; };
El.prototype.addEventListener = function (t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); };
El.prototype.removeEventListener = function () {};
El.prototype.dispatch = function (t, ev) {
  (this.listeners[t] || []).forEach((f) => f.call(this, ev || { target: this, preventDefault() {}, stopPropagation() {} }));
};
// 与浏览器一致：disabled 元素不派发 click
El.prototype.click = function () {
  if (this.disabled) return false;
  this.dispatch("click");
  return true;
};
El.prototype.focus = function () {};
El.prototype.blur = function () {};
El.prototype.getBoundingClientRect = function () { return { left: 0, top: 0, width: 640, height: 240, right: 640, bottom: 240 }; };
El.prototype.querySelectorAll = function (sel) { return queryAll(this, sel); };
El.prototype.querySelector = function (sel) { return queryAll(this, sel)[0] || null; };
El.prototype.getContext = function (kind) {
  if (kind !== "2d") return null;
  if (this._ctx) return this._ctx;
  const c = {
    canvas: this, ops: 0, texts: [],
    fillStyle: "#000", strokeStyle: "#000", lineWidth: 1, font: "12px sans-serif",
    textAlign: "start", textBaseline: "alphabetic", lineCap: "butt", lineJoin: "miter", globalAlpha: 1,
    save() {}, restore() {}, beginPath() {}, closePath() {}, clip() {},
    moveTo() { c.ops++; }, lineTo() { c.ops++; }, arc() { c.ops++; }, arcTo() { c.ops++; },
    rect() { c.ops++; }, quadraticCurveTo() { c.ops++; }, bezierCurveTo() { c.ops++; },
    clearRect() {}, fillRect() { c.ops++; }, strokeRect() { c.ops++; },
    fill() { c.ops++; }, stroke() { c.ops++; },
    fillText(t) { c.ops++; if (t !== undefined && t !== null) c.texts.push(String(t)); },
    strokeText() { c.ops++; },
    measureText(t) { return { width: String(t).length * 6.2 }; },
    setLineDash() {}, setTransform() {}, translate() {}, rotate() {}, scale() {},
    createLinearGradient() { return { addColorStop() {} }; },
    drawImage() { c.ops++; },
  };
  this._ctx = c;
  return c;
};

function parseAttrs(s, el) {
  const re = /([^\s=/>]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
  let a;
  while ((a = re.exec(s))) {
    const name = a[1];
    if (!name || name === "/") continue;
    const val = a[2] !== undefined ? a[2] : (a[3] !== undefined ? a[3] : (a[4] !== undefined ? a[4] : ""));
    el.attrs[name] = val;
    if (name === "class") el.className = val;
    else if (name === "id") { el.id = val; REG[val] = el; }
    else if (name.startsWith("data-")) el.dataset[name.slice(5).replace(/-(\w)/g, (_, c) => c.toUpperCase())] = val;
    else if (name === "value") el.value = val;
    else if (name === "checked") el.checked = true;
    else if (name === "disabled") el.disabled = true;
    else if (name === "type") el.type = val;
    else if (name === "min" || name === "max" || name === "step") el[name] = val;
  }
}

function parseHTML(str, root) {
  const stack = [root];
  const re = /<!--[\s\S]*?-->|<!DOCTYPE[^>]*>|<\/([a-zA-Z][\w:-]*)\s*>|<([a-zA-Z][\w:-]*)((?:\s+[^\s=>]+(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))?)*)\s*(\/?)>|([^<]+)/g;
  let m;
  while ((m = re.exec(str))) {
    const top = stack[stack.length - 1];
    if (m[0].startsWith("<!--") || m[0].startsWith("<!")) continue;
    if (m[1]) {
      for (let i = stack.length - 1; i > 0; i--) {
        if (stack[i].tagName === m[1].toUpperCase()) { stack.length = i; break; }
      }
    } else if (m[2]) {
      const e = new El(m[2]);
      parseAttrs(m[3] || "", e);
      top.appendChild(e);
      if (!VOID.has(m[2].toLowerCase()) && !m[4]) stack.push(e);
    } else if (m[5] !== undefined) {
      top.appendChild(tnode(m[5]));
    }
  }
}

/* ==================== 选择器引擎（标签 / .class / #id / [attr] / 后代） ==================== */
function parseSimple(s) {
  const out = { tag: null, id: null, cls: [], attrs: [] };
  const re = /\[([^\]]+)\]|([.#]?)([A-Za-z0-9_\-]+)/g;
  let m;
  while ((m = re.exec(s))) {
    if (m[1]) {
      // 属性选择器：支持 [name]（仅存在）与 [name="value"]（精确匹配值）
      const inner = m[1];
      const eq = inner.indexOf("=");
      if (eq >= 0) {
        const name = inner.slice(0, eq);
        let value = inner.slice(eq + 1).replace(/^["']|["']$/g, "");
        out.attrs.push({ name, value });
      } else {
        out.attrs.push({ name: inner, value: null });
      }
    } else if (m[2] === ".") out.cls.push(m[3]);
    else if (m[2] === "#") out.id = m[3];
    else out.tag = m[3].toUpperCase();
  }
  return out;
}
function matchSimple(el, s) {
  if (el.nodeType !== 1) return false;
  if (s.tag && el.tagName !== s.tag) return false;
  if (s.id && el.id !== s.id) return false;
  for (const c of s.cls) if (!el._classes.has(c)) return false;
  for (const a of s.attrs) {
    if (!(a.name in el.attrs)) return false;
    if (a.value !== null && el.attrs[a.name] !== a.value) return false;
  }
  return true;
}
function matchChain(el, parts) {
  let i = parts.length - 2, node = el.parentNode;
  while (i >= 0) {
    let found = false;
    while (node) {
      if (node.nodeType === 1 && matchSimple(node, parts[i])) { found = true; node = node.parentNode; break; }
      node = node.parentNode;
    }
    if (!found) return false;
    i--;
  }
  return true;
}
function queryAll(root, sel) {
  const parts = sel.trim().split(/\s+/).map(parseSimple);
  const last = parts[parts.length - 1];
  const res = [];
  (function walk(e) {
    for (const c of e.childNodes) {
      if (c.nodeType !== 1) continue;
      if (matchSimple(c, last) && matchChain(c, parts)) res.push(c);
      walk(c);
    }
  })(root);
  return res;
}

/* ==================== 运行环境 ==================== */
const CSSVARS = {
  "--card": "#ffffff", "--line": "#e3e8ef", "--line2": "#eef2f7",
  "--tx": "#0f172a", "--tx2": "#475569", "--tx3": "#94a3b8",
  "--acc": "#ff7a1a", "--acc2": "#ffb066", "--ok": "#16a34a",
  "--warn": "#f59e0b", "--err": "#dc2626", "--grid": "#eef2f7", "--card2": "#f8fafc",
};
function getComputedStyle() { return { getPropertyValue(k) { return CSSVARS[k] || "#888888"; } }; }

function boot() {
  REG = Object.create(null);
  const TMP = new El("#root");
  parseHTML(HTML, TMP);
  let docEl = null, bodyEl = null;
  (function find(e) {
    for (const c of e.childNodes) {
      if (c.nodeType !== 1) continue;
      if (c.tagName === "HTML" && !docEl) docEl = c;
      if (c.tagName === "BODY" && !bodyEl) bodyEl = c;
      find(c);
    }
  })(TMP);
  if (!docEl) docEl = new El("html");
  if (!bodyEl) bodyEl = new El("body");

  const document = {
    documentElement: docEl,
    body: bodyEl,
    getElementById(id) { return REG[id] || null; },
    createElement(t) { return new El(t); },
    createTextNode(t) { return tnode(t); },
    querySelectorAll(sel) { return queryAll(TMP, sel); },
    querySelector(sel) { return queryAll(TMP, sel)[0] || null; },
    addEventListener() {},
  };

  let CLOCK = 0;
  let rafQ = [];
  const timeouts = [];
  const errs = [];
  let skipped = 0;

  const sandbox = {
    console, document, getComputedStyle,
    requestAnimationFrame(fn) { rafQ.push(fn); return rafQ.length; },
    cancelAnimationFrame() {},
    setTimeout(fn, ms) { timeouts.push({ fn, ms }); return timeouts.length; },
    clearTimeout() {},
    setInterval(fn, ms) { timeouts.push({ fn, ms, repeat: true }); return timeouts.length; },
    clearInterval() {},
    performance: { now: () => CLOCK },
    devicePixelRatio: 2,
    innerWidth: 1440, innerHeight: 900,
    URL: { createObjectURL: () => "blob:stub", revokeObjectURL() {} },
    Blob: function Blob(p) { this.parts = p; },
    Math, Date, JSON, parseInt, parseFloat, isNaN, isFinite,
    String, Number, Boolean, Array, Object, Error, TypeError, RangeError, RegExp, Map, Set, Promise,
  };
  sandbox.window = {
    devicePixelRatio: 2, innerWidth: 1440, innerHeight: 900,
    addEventListener() {}, requestAnimationFrame: sandbox.requestAnimationFrame,
  };
  sandbox.globalThis = sandbox;
  sandbox.self = sandbox;
  vm.createContext(sandbox);

  const h = {
    sandbox, document, TMP, REG, errs, timeouts,
    get skipped() { return skipped; },
    frame(dt) {
      CLOCK += dt === undefined ? 16.7 : dt;
      const q = rafQ; rafQ = [];
      q.forEach((f) => f(CLOCK));
    },
    frames(n, dt) { for (let i = 0; i < n; i++) h.frame(dt); },
    run() {
      try { vm.runInContext(JS, sandbox, { filename: "demo.js" }); }
      catch (e) { errs.push("初始执行 JS: " + shortErr(e)); }
    },
    guard(label, fn) {
      try { fn(); } catch (e) { errs.push(label + ": " + shortErr(e)); }
    },
    q(sel) { return queryAll(TMP, sel); },
    el(id) { return REG[id] || null; },
    text(id) { const e = REG[id]; return e ? String(e.textContent || "").trim() : ""; },
    click(id) {
      const e = REG[id];
      if (!e) { errs.push("交互目标缺失: #" + id); return false; }
      if (e.disabled) { skipped++; return false; }
      try { e.click(); } catch (err) { errs.push("点击 #" + id + ": " + shortErr(err)); return false; }
      return true;
    },
    // 执行所有挂起的 setTimeout（用于验证指令下发 → 回执链路）
    flushTimers() {
      const q = timeouts.splice(0);
      q.forEach((t) => {
        try { t.fn(); } catch (e) { errs.push("timer: " + shortErr(e)); }
      });
    },
    hasClass(id, c) { const e = REG[id]; return !!e && e._classes.has(c); },
    clickSel(sel, idx) {
      const list = queryAll(TMP, sel);
      const e = list[idx || 0];
      if (!e) { errs.push("交互目标缺失: " + sel + "[" + (idx || 0) + "]"); return false; }
      if (e.disabled) { skipped++; return false; }
      try { e.click(); } catch (err) { errs.push("点击 " + sel + ": " + shortErr(err)); return false; }
      return true;
    },
    canvasOps() {
      return Object.keys(REG)
        .filter((id) => REG[id].tagName === "CANVAS")
        .reduce((s, id) => s + (REG[id]._ctx ? REG[id]._ctx.ops : 0), 0);
    },
  };
  return h;
}
function shortErr(e) {
  return e && e.stack ? e.stack.split("\n").slice(0, 3).join(" | ") : String(e);
}

/* ==================== A. 冒烟测试 ==================== */
const errs = [], notes = [];
const H = boot();
H.run();

const navBtns = H.q(".nav button");
const views = H.q(".view").map((v) => v.id.replace(/^v-/, ""));
notes.push(`导航 ${navBtns.length} 个 | 视图 ${views.length} 个：${views.join(", ")}`);

const perView = {};
views.forEach((v) => {
  const btn = navBtns.find((b) => b.getAttribute("data-view") === v);
  H.guard(`切换视图 ${v}`, () => { if (btn) btn.click(); });
  H.guard(`渲染视图 ${v}`, () => H.frames(40));
  perView[v] = H.canvasOps();
});

// 注：btnLink/btnStart/btnPause/btnStop 是有副作用的「全局控制」按钮——
// 链路断开或机器运行会经 renderMp 传播到移动端（mpStart.disabled = mpCanWrite() || btnStart.disabled），
// 若在盲点阶段把它们拨到非预期态，会让后面整段移动端深度交互用例全部误报。
// 因此它们不放进「盲点全部按钮」列表，改由下方独立的「监控端启停」用例专门覆盖。
["btnApply", "btnRegen", "btnReset", "btnExport", "btnExportTraj", "btnTheme"].forEach((id) => {
  H.click(id); H.frames(8);
});

// 监控端启停（直接驱动主仿真，不经过移动端 mpSend 链路；用例结束须回到「链路连接 + 待机」）
H.guard("监控端启停", () => {
  if (!H.el("btnStart").disabled) { H.click("btnStart"); H.frames(8); }  // 启动
  H.click("btnPause"); H.frames(8);                                       // 暂停
  H.click("btnStop");  H.frames(8);                                       // 停止 → 回到待机
});
// 链路保持连接：btnLink 不在此处盲点，确保移动端 mpCanWrite() 放行（链路默认连接）

// 手眼标定：手动采集 10 组位姿 → 求解
const calibNav = navBtns.find((b) => b.getAttribute("data-view") === "calib");
H.guard("切换到标定视图", () => { if (calibNav) calibNav.click(); H.frames(5); });
for (let i = 0; i < 10; i++) { H.click("btnHeGrab"); }
H.click("btnHeSolve"); H.frames(10);
notes.push(`手眼标定：位姿 ${H.text("heN")} | 点数 ${H.text("hePts")} | 覆盖度 ${H.text("heCov")} | ${H.text("rpRms")}`);
// 自动扫描（求解后按钮 disabled，应被忽略）
H.click("btnAutoScan"); H.frames(10);
// 重力补偿：采满 12 组 → 求解
for (let i = 0; i < 14; i++) { H.click("btnGcGrab"); }
H.click("btnGcSolve"); H.frames(10);
notes.push(`重力补偿：样本 ${H.text("gcN")} | 负载 ${H.text("gcM")} | 零偏 ${H.text("gcB0")} / ${H.text("gcBz")} | 残差 ${H.text("gcRes")}`);
H.click("btnClearCalib"); H.frames(5);

// 小程序端
const miniNav = navBtns.find((b) => b.getAttribute("data-view") === "mini");
H.guard("切换到小程序视图", () => { if (miniNav) miniNav.click(); H.frames(10); });
const mpTabs = H.q(".mp-tab button");
notes.push(`小程序 Tab ${mpTabs.length} 个：${mpTabs.map((b) => b.getAttribute("data-tab")).join(", ")}`);
mpTabs.forEach((b) => {
  H.guard(`小程序 Tab ${b.getAttribute("data-tab")}`, () => { b.click(); H.frames(25); });
});
H.guard("小程序启停联动", () => {
  // 每条写指令都经 mpSend，回执是 setTimeout；frames() 只推进 rAF，必须 flushTimers 才能让回执落地，
  // 否则 MP.pend 一直占用，后续 pause/stop 会被「上一条指令尚未回执」拒绝，机器停在运行态。
  H.click("mpStart"); H.flushTimers(); H.frames(20);   // 下发启动 → 回执 → 运行
  H.click("mpPause"); H.flushTimers(); H.frames(15);   // 下发暂停 → 回执 → 暂停
  H.click("mpStop");  H.flushTimers(); H.frames(10);   // 直接下发停止（已去除二次确认弹层）→ 回到待机
});
H.flushTimers(); H.frames(20);   // 兜底清悬挂回执，确保后续用例在待机态下发
H.click("btnMiniPush"); H.frames(10);

/* ---- 小程序端：Tab 内联交互（无二级页 / 弹层） ---- */
const mpTabOf = (t) => H.q('.mp-tab button').find((b) => b.getAttribute('data-tab') === t);
const goTab = (t) => H.guard('小程序 Tab ' + t, () => { mpTabOf(t).click(); H.frames(12); });

// 1) 三台设备切换，记录各设备的 hero 状态
goTab('home');
const devSeq = [];
H.q('.mp-chip').forEach((c) => {
  const d = c.getAttribute('data-dev');
  H.guard('切换设备 ' + d, () => {
    c.click(); H.frames(18);
    devSeq.push('#' + d + ' ' + H.text('mpState') + '/' + H.text('mpForce') + 'N');
  });
});
notes.push('设备切换: ' + devSeq.join('  |  '));

// 2) 离线设备（#03）不可操作：按钮置灰 + 点击给出原因
H.guard('离线设备拒绝操作', () => {
  H.q('.mp-chip').find((c) => c.getAttribute('data-dev') === '03').click(); H.frames(10);
  if (!H.el('mpStart').disabled) errs.push('#03 离线时启动按钮未置灰');
  if (!H.hasClass('mpHero', 'off')) errs.push('#03 未切换到离线 hero 样式');
  if (H.text('mpHeroOffTx').indexOf('离线') < 0) errs.push('#03 未给出离线原因说明');
});
// 3) 未接入链路设备（#02）同样拒绝，但原因不同
H.guard('未接入链路设备拒绝操作', () => {
  H.q('.mp-chip').find((c) => c.getAttribute('data-dev') === '02').click(); H.frames(10);
  if (!H.el('mpStart').disabled) errs.push('#02 未接入链路时启动按钮未置灰');
  if (H.text('mpHeroOffTx').indexOf('产线') < 0) errs.push('#02 未说明"其他产线网关"原因');
});

// 4) #01 在线：下发启动 → 等待回执 → 回执到达后进入运行
H.guard('指令下发与回执', () => {
  H.q('.mp-chip').find((c) => c.getAttribute('data-dev') === '01').click(); H.frames(10);
  if (H.el('mpStart').disabled) errs.push('#01 在线时启动按钮不应置灰');
  H.click('mpStart');
  if (H.el('mpPend').style.display !== 'flex') errs.push('下发后未显示"等待回执"指示条');
  H.flushTimers(); H.frames(30);
  if (H.el('mpPend').style.display === 'flex') errs.push('回执到达后指示条未收起');
  if (H.text('mpState').indexOf('打磨中') < 0) errs.push('回执后设备状态未变为打磨中');
});
H.frames(400);   // 跑一会儿，产出工件与统计

// 5) 停止：直接下发（已去除二次确认弹层）
H.guard('停止直接下发', () => {
  H.click('mpStop'); H.frames(5);
  if (H.el('mpPend').style.display !== 'flex') errs.push('停止后未显示"等待回执"指示条');
  H.flushTimers(); H.frames(30);
  if (H.el('mpPend').style.display === 'flex') errs.push('停止回执到达后指示条未收起');
  if (H.text('mpState').indexOf('打磨中') >= 0) errs.push('停止后设备仍显示打磨中');
});

// 6) 告警：推送直到出现待确认项 → 内联展开 → 转工单
goTab('home');
H.guard('告警内联展开与转工单', () => {
  let tries = 0;
  while (tries < 40 && !H.q('#mpAlarms .mp-al').some((b) => String(b.textContent).indexOf('未确认') >= 0)) {
    H.click('btnMiniPush'); H.frames(3); tries++;
  }
  if (!H.q('#mpAlarms .mp-al').length) { errs.push('小程序告警列表为空'); return; }
  H.q('#mpAlarms .mp-al')[0].click(); H.frames(5);
  const openCard = H.q('#mpAlarms .mp-al.open')[0];
  if (!openCard) { errs.push('点击告警未内联展开'); return; }
  if (String(openCard.innerHTML).indexOf('建议处置步骤') < 0) errs.push('告警展开缺少建议处置步骤');
  const before = H.q('#mpOrderList .mp-wo').length;
  const woBtn = H.q('#mpAlarms .mp-al.open [data-act]').find((b) => /转为工单/.test(String(b.textContent || '').trim()));
  if (!woBtn) { errs.push('告警展开缺少「转为工单」按钮'); return; }
  woBtn.click(); H.frames(8);
  if (H.q('#mpOrderList .mp-wo').length !== before + 1) errs.push('告警转工单后工单数量未 +1');
});

// 7) 工单状态流转（内联）：待接单 → 处理中 → 待验收 → 已关闭
const woFlow = [];
H.guard('工单状态流转', () => {
  const orders = H.q('#mpOrderList .mp-wo');
  if (!orders.length) { errs.push('无工单可流转'); return; }
  orders[0].click(); H.frames(5);              // 展开首条工单
  for (let k = 0; k < 4; k++) {
    const openCard = H.q('#mpOrderList .mp-wo.open')[0];
    if (!openCard) { errs.push('工单未保持展开'); break; }
    const btn = H.q('#mpOrderList .mp-wo.open [data-act]')
                  .find((x) => /接单|提交验收|验收通过/.test(String(x.textContent || '').trim()));
    if (!btn) break;
    const label = String(btn.textContent || '').trim();
    btn.click(); H.frames(6);
    woFlow.push(label);
    if (label === '验收通过') break;
  }
  if (woFlow.join('>') !== '接单>提交验收>验收通过')
    errs.push('工单状态流转异常: ' + (woFlow.join('>') || '(空)'));
});
notes.push('工单流转: ' + (woFlow.join(' → ') || '（未执行）'));

// 8) 工单筛选
goTab('order');
H.guard('工单筛选', () => {
  H.q('#mpOrderFilter button').forEach((b) => { b.click(); H.frames(6); });
  H.q('#mpOrderFilter button').find((x) => x.getAttribute('data-f') === 'done').click(); H.frames(6);
  if (!H.q('#mpOrderList .mp-wo').length) errs.push('"已关闭"筛选下查不到刚关闭的工单');
  H.q('#mpOrderFilter button').find((x) => x.getAttribute('data-f') === 'all').click(); H.frames(6);
});
// 人工建单（直接创建，无类型选择弹层）
H.guard('人工建单', () => {
  const before = H.q('#mpOrderList .mp-wo').length;
  H.click('mpNewOrder'); H.frames(5);
  if (H.q('#mpOrderList .mp-wo').length !== before + 1) errs.push('人工建单后工单数量未 +1');
});

// 9) 数据页：周期切换
goTab('data');
const periSeq = [];
H.guard('数据周期切换', () => {
  ['1', '7', '30'].forEach((p) => {
    H.q('#mpPeriod button').find((x) => x.getAttribute('data-p') === p).click();
    H.frames(16);
    periSeq.push(p + '日=' + H.text('mpDCount'));
  });
  H.q('#mpPeriod button').find((x) => x.getAttribute('data-p') === '7').click(); H.frames(16);
});
notes.push('数据周期: ' + periSeq.join(' | '));

// 10) 我的：开关 / 阈值 / 权限 / 导出 / 清除 / 关于
goTab('me');
H.guard('推送开关', () => {
  H.click('mpSwVibr'); H.frames(3);
  if (!H.hasClass('mpSwVibr', 'on')) errs.push('振动开关点击后未开启');
  H.click('mpSwVibr'); H.frames(3);
  if (H.hasClass('mpSwVibr', 'on')) errs.push('振动开关再次点击后未关闭');
});
H.guard('推送阈值循环', () => {
  const v0 = H.text('mpThreshV');
  H.click('mpThresh'); H.frames(4);
  const v1 = H.text('mpThreshV');
  if (v1 === v0) errs.push('点击推送阈值后未切换');
  if (v1.indexOf('仅严重') < 0 && v1.indexOf('警告及以上') < 0 && v1.indexOf('全部含提示') < 0)
    errs.push('推送阈值显示异常: ' + v1);
});
H.guard('权限申请直接提交', () => {
  H.click('mpPerm'); H.frames(4);
  if (H.text('mpPermV').indexOf('审批中') < 0) errs.push('权限申请后状态未变为审批中');
});
H.guard('CSV 导出', () => {
  H.click('mpExport'); H.frames(4);
  const t = H.text('mpToast');
  if (t.indexOf('已导出') < 0 && t.indexOf('已生成') < 0) errs.push('CSV 导出未给出结果提示');
});
H.guard('清除缓存直接生效', () => {
  H.click('mpClear'); H.frames(4);
  if (H.text('mpToast').indexOf('已清除') < 0) errs.push('清除缓存未提示成功');
  if (H.q('#mpOrderList .mp-wo').length !== 0) errs.push('清除缓存后工单未清空');
});
H.guard('关于卡片内联展示', () => {
  if (String(H.el('mpMe').innerHTML).indexOf('关于灵犀智磨') < 0) errs.push('「我的」页未内联展示关于卡片');
});
goTab('home');
H.frames(20);

// 参数滑块
["pForce", "pDamp", "pMass", "pFeed", "pLimit", "pRobust", "camRes", "pGain"].forEach((id) => {
  const e = H.el(id);
  if (!e) return;
  H.guard(`滑块 ${id}`, () => {
    e.value = e.getAttribute("max") || e.value;
    e.dispatch("input"); e.dispatch("change");
    H.frames(10);
  });
});

// 长跑 60 s，再遍历一遍视图
H.guard("长跑 60s", () => H.frames(3600, 16.7));
views.forEach((v) => {
  const btn = navBtns.find((b) => b.getAttribute("data-view") === v);
  H.guard(`长跑后渲染 ${v}`, () => { if (btn) btn.click(); H.frames(30); });
});

/* ---- 断言 ---- */
const canvasIds = Object.keys(H.REG).filter((id) => H.REG[id].tagName === "CANVAS");
const neverDrawn = canvasIds.filter((id) => !H.REG[id]._ctx || H.REG[id]._ctx.ops === 0);
if (neverDrawn.length) errs.push(`canvas 从未绘制: ${neverDrawn.join(", ")}`);

["kRms", "kMax", "kCount", "kYield", "tMode", "tSim", "tSing", "heN", "gcN"].forEach((id) => {
  if (!H.el(id)) { errs.push(`缺关键元素 #${id}`); return; }
  if (!H.text(id)) errs.push(`#${id} 文本为空`);
});
["cMini", "cMiniBar", "cMiniDonut"].forEach((id) => {
  const c = H.el(id);
  if (!c || !c._ctx || c._ctx.ops === 0) errs.push(`小程序画布 ${id} 未绘制`);
});
const alarmList = H.el("alarmList");
if (!alarmList || alarmList.childNodes.length === 0) errs.push("告警列表为空（未渲染任何告警）");
const mpAlarms = H.el("mpAlarms");
if (!mpAlarms || mpAlarms.childNodes.length === 0) errs.push("小程序告警区为空");
H.errs.forEach((e) => errs.push(e));

/* ==================== B. 重力补偿最小二乘数值验证 ==================== */
const TRUE_M = 1.85, TRUE_B = [0.32, -0.18, 0.45];
const SIGMA = 0.06;      // grabForce() 注入的测量噪声标准差（N）
const TRIALS = 6;

// 判据分两层：
//   单次粗差上限 —— 只拦"算法坏了"，不拦统计波动。取 ±0.06 N（≈2σ）会导致
//   bz 偶发失败：小倾角下 u_z ≈ −g 近乎常数，与 m 强相关，零偏 z 分量的估计
//   方差本就比 x/y 大（垂直零偏与负载质量的弱可分离性，属物理正确行为）。
//   6 次试验 × 2σ 容差的失败概率约 26%，不可用。
//   无偏性 —— 用多次试验的均值判定，这才是"解算是否正确"的判据。
const GROSS_M = 0.15, GROSS_B = 0.25, GROSS_RES = 0.15, GROSS_RMS = 2.0;
const UNBIAS_M = 0.02, UNBIAS_B = 0.05;   // 均值误差上限
const RES_BAND = [0.4 * SIGMA, 1.6 * SIGMA]; // 残差应落在噪声水平附近
const trials = [];
for (let t = 0; t < TRIALS; t++) {
  const g = boot();
  g.run();
  const nav = g.q(".nav button").find((b) => b.getAttribute("data-view") === "calib");
  g.guard("切到标定视图", () => { if (nav) nav.click(); g.frames(5); });
  for (let i = 0; i < 14; i++) g.click("btnGcGrab");   // 上限 12 组
  g.click("btnGcSolve");
  g.frames(5);
  const m = parseFloat(g.text("gcM"));
  const b0 = g.text("gcB0").split("/").map((s) => parseFloat(s));
  const bz = parseFloat(g.text("gcBz"));
  const res = parseFloat(g.text("gcRes"));
  for (let i = 0; i < 10; i++) g.click("btnHeGrab");
  g.click("btnHeSolve");
  g.frames(5);
  const rms = parseFloat(g.text("rpRms").replace(/[^0-9.\-]/g, ""));
  const cov = parseFloat(g.text("heCov"));
  g.errs.forEach((e) => errs.push("数值试验#" + (t + 1) + " " + e));
  trials.push({ m, b: [b0[0], b0[1], bz], res, rms, cov });
}
const num = (v) => typeof v === "number" && isFinite(v);
const agg = (f) => trials.map(f).filter(num);
const mean = (a) => a.reduce((s, v) => s + v, 0) / a.length;
const std = (a) => (a.length < 2 ? 0 : Math.sqrt(mean(a.map((v) => (v - mean(a)) ** 2))));
const maxAbs = (a) => (a.length ? Math.max(...a.map(Math.abs)) : NaN);

// 第一层：单次试验不得出现粗差
trials.forEach((r, i) => {
  const tag = "试验#" + (i + 1);
  if (!num(r.m)) errs.push(`${tag} 负载未解出（gcM="${r.m}"）`);
  else if (Math.abs(r.m - TRUE_M) > GROSS_M) errs.push(`${tag} 负载粗差 ${(r.m - TRUE_M).toFixed(4)} kg`);
  r.b.forEach((v, k) => {
    if (!num(v)) errs.push(`${tag} 零偏 b${k} 未解出`);
    else if (Math.abs(v - TRUE_B[k]) > GROSS_B) errs.push(`${tag} 零偏 b${k} 粗差 ${(v - TRUE_B[k]).toFixed(4)} N`);
  });
  if (!num(r.res)) errs.push(`${tag} 残差未解出`);
  else if (r.res <= 0 || r.res > GROSS_RES) errs.push(`${tag} 残差 ${r.res.toFixed(4)} N 超出 (0, ${GROSS_RES}]`);
  if (!num(r.rms) || r.rms <= 0 || r.rms > GROSS_RMS) errs.push(`${tag} 手眼重投影 RMS ${r.rms} px 超出 (0, ${GROSS_RMS}]`);
  if (!num(r.cov) || r.cov < 0 || r.cov > 100) errs.push(`${tag} 姿态覆盖度 ${r.cov}% 非法`);
});

// 第二层：多次试验均值必须无偏 —— 这才是解算正确性的判据
if (trials.length && num(trials[0].m)) {
  const em = agg((r) => r.m - TRUE_M);
  if (Math.abs(mean(em)) > UNBIAS_M) errs.push(`负载估计有偏：均值误差 ${mean(em).toFixed(4)} kg`);
  ["bx", "by", "bz"].forEach((n, k) => {
    const eb = agg((r) => r.b[k] - TRUE_B[k]);
    if (Math.abs(mean(eb)) > UNBIAS_B) errs.push(`零偏 ${n} 估计有偏：均值误差 ${mean(eb).toFixed(4)} N`);
  });
  const mr = mean(agg((r) => r.res));
  if (mr < RES_BAND[0] || mr > RES_BAND[1]) {
    errs.push(`残差均值 ${mr.toFixed(4)} N 偏离噪声水平 σ=${SIGMA} N（合理区间 ${RES_BAND[0].toFixed(3)}~${RES_BAND[1].toFixed(3)}）`);
  }
}

/* ==================== 输出 ==================== */
const W = 66;
console.log("=".repeat(W));
console.log("A. 冒烟测试 —— monitor-demo.html");
console.log("=".repeat(W));
notes.forEach((n) => console.log("  " + n));
console.log("-".repeat(W));
console.log("  canvas 累计绘制操作数（同视图内叠加，非独立值）:");
Object.keys(perView).sort().forEach((v) => console.log(`      ${v.padEnd(8)} ${perView[v]}`));
console.log("-".repeat(W));
console.log(`  canvas ${canvasIds.length} 个 | 未绘制 ${neverDrawn.length} 个`);
console.log(`  遥测告警 ${alarmList ? alarmList.childNodes.length : 0} 条 | 小程序告警 ${mpAlarms ? mpAlarms.childNodes.length : 0} 条`);
console.log(`  disabled 未响应点击 ${H.skipped} 次（符合浏览器行为，非错误）`);
console.log("=".repeat(W));
console.log("B. 重力补偿最小二乘数值验证（真值 m=1.85 kg, b=[0.32,-0.18,0.45] N）");
console.log("=".repeat(W));
console.log("   #   负载(kg)   零偏 bx/by/bz (N)              残差(N)   RMS(px)  覆盖度");
trials.forEach((r, i) => {
  const f = (v, n) => (num(v) ? v.toFixed(n) : String(v));
  console.log(`  ${String(i + 1).padStart(2)}   ${f(r.m, 4).padStart(7)}   ` +
    `${f(r.b[0], 3)} / ${f(r.b[1], 3)} / ${f(r.b[2], 3)}`.padEnd(30) +
    ` ${f(r.res, 4).padStart(7)}   ${f(r.rms, 3).padStart(6)}   ${f(r.cov, 0).padStart(5)}%`);
});
console.log("-".repeat(W));
if (trials.length && num(trials[0].m)) {
  const em = agg((r) => r.m - TRUE_M);
  const eb = [0, 1, 2].map((k) => agg((r) => r.b[k] - TRUE_B[k]));
  const show = (name, a, unit, n) => console.log(
    `  ${name.padEnd(9)} 误差均值 ${mean(a).toFixed(n).padStart(8)} ${unit} | 标准差 ${std(a).toFixed(n).padStart(6)} | 最大 |误差| ${maxAbs(a).toFixed(n).padStart(6)}`);
  show("负载", em, "kg", 4);
  ["bx", "by", "bz"].forEach((n, k) => show(n, eb[k], "N ", 4));
  console.log(`  残差       均值 ${mean(agg((r) => r.res)).toFixed(4)} N  （注入噪声 σ=${SIGMA} N，残差≈噪声 ⇒ 解算无偏）`);
  console.log(`  重投影RMS  均值 ${mean(agg((r) => r.rms)).toFixed(3)} px | 最大 ${Math.max(...agg((r) => r.rms)).toFixed(3)} px`);
  console.log(`  姿态覆盖度 均值 ${mean(agg((r) => r.cov)).toFixed(0)} %`);
  console.log(`  判据       单次粗差 |Δm|<${GROSS_M} kg / |Δb|<${GROSS_B} N；` +
    `均值无偏 |E[Δm]|<${UNBIAS_M} kg / |E[Δb]|<${UNBIAS_B} N；残差∈[${RES_BAND[0].toFixed(3)}, ${RES_BAND[1].toFixed(3)}] N`);
}
console.log("-".repeat(W));
if (errs.length) errs.forEach((e) => console.log("[FAIL] " + e));
console.log(`结果: ${errs.length ? "失败 (" + errs.length + ")" : "通过 —— 冒烟无异常，数值解算无偏"}`);
console.log("=".repeat(W));
process.exit(errs.length ? 1 : 0);
