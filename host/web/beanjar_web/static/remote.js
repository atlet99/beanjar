// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The popup remote: the VM's remote drawn from layout.json (the GTK remote's data, CSS pixels on a
// width x height panel), each key's LG IR code sent over /vm/HOST/NAME/remote: down on press, up on
// release, so holding a key is a long press. Keys without a code are dimmed. While the window has
// the focus, the arrows, Enter and Backspace are the remote's arrows, OK and Back. It closes itself
// when the viewer that opened it goes away.
const embedded = window.parent !== window;
const vm = new URLSearchParams(location.search).get("vm") || "";
const [host, name] = vm.split("/");
const base = `/vm/${encodeURIComponent(host)}/${encodeURIComponent(name)}`;
const wsUrl = `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}${base}/remote`;
const panel = document.getElementById("panel");
const rstatus = document.getElementById("rstatus");
// Bootstrap Icons, served from the system package just like the viewer toolbar.
const GLYPHS = {
  POWER: "power", INPUT: "box-arrow-in-right", SEARCH: "search", MUTE: "volume-mute",
  HOME: "house", GEAR: "gear", PLUS: "plus", MINUS: "dash",
  CH_UP: "chevron-up", CH_DOWN: "chevron-down", UP: "chevron-up",
  DOWN: "chevron-down", LEFT: "chevron-left", RIGHT: "chevron-right",
  STOP: "stop-fill", REWIND: "rewind-fill", PLAY: "play-fill", PAUSE: "pause-fill",
  FORWARD: "fast-forward-fill", PREV: "skip-start-fill", NEXT: "skip-end-fill",
};
const KEYS = { ArrowUp: "UP", ArrowDown: "DOWN", ArrowLeft: "LEFT", ArrowRight: "RIGHT", Enter: "ENTER", Backspace: "BACK" };
let layout = null, ws = null, queue = [];
const keyEls = new Map();

function say(text) { rstatus.hidden = !text; rstatus.textContent = text || ""; }

function open() {
  if (!ws || ws.readyState > 1) {
    ws = new WebSocket(wsUrl);
    ws.addEventListener("open", () => { say(""); for (const l of queue.splice(0)) ws.send(l); });
    ws.addEventListener("message", (e) => { if (e.data.startsWith("error")) say(e.data); });
    ws.addEventListener("close", (e) => say(e.code === 1000 ? "" : `Remote disconnected${e.reason ? ": " + e.reason : ""}`));
  }
}

function send(line) {
  open();
  if (ws.readyState === 1) ws.send(line); else queue.push(line);
}

function box(el, x, y, w, h) {
  Object.assign(el.style, { left: `${x}px`, top: `${y}px`, width: `${w}px`, height: `${h}px` });
  return el;
}

function radius(r) { return r.map((v) => `${v}px`).join(" "); }

function draw() {
  const b = layout.body;
  panel.style.width = `${layout.width}px`;
  panel.style.height = `${layout.height}px`;
  const body = box(document.createElement("div"), b.x, b.y, b.w, b.h);
  Object.assign(body.style, { background: layout.face, border: `1px solid ${layout.rim}`,
    borderRadius: `${b.top_radius}px ${b.top_radius}px ${b.bottom_radius}px ${b.bottom_radius}px` });
  panel.append(body);
  for (const d of layout.decor) {
    const el = box(document.createElement("div"), d.x, d.y, d.w, d.h);
    Object.assign(el.style, { borderRadius: `${d.radius}px`, background: d.face || "transparent",
      border: d.border ? `1px solid ${d.border}` : "none" });
    panel.append(el);
  }
  for (const k of layout.keys) {
    const el = box(document.createElement("div"), k.x, k.y, k.w, k.h);
    el.className = "rk" + (k.code === null ? " dim" : "");
    el.title = k.name + (k.code === null ? " (sends nothing)" : ` (IR 0x${k.code.toString(16).padStart(2, "0")})`);
    const icon = k.glyph && GLYPHS[k.glyph]; // keys labelled with text (CC, GUIDE, BACK...) keep it
    const text = k.glyph ? (layout.glyphs[k.glyph] || k.glyph) : (k.text || "");
    if (icon) {
      const image = document.createElement("i");
      image.className = "bi";
      image.style.setProperty("--i", `url(/icons/${icon}.svg)`);
      image.setAttribute("aria-hidden", "true");
      el.setAttribute("aria-label", k.name.replaceAll("_", " "));
      if (icon === "volume-mute") image.style.transform = "scale(1.2)"; // as the viewer's speaker
      el.append(image);
    } else el.textContent = text;
    const lines = text.split("\n");
    const longest = Math.max(...lines.map((l) => l.length), 1);
    const cap = !k.glyph ? k.cap : text.length > 2 ? 4.5 : (Math.min(k.w, k.h) * 0.45) / 0.73;
    // One-em masks use the key's ink, with equal-sized symbols across the key groups.
    const px = icon ? Math.min(14, Math.min(k.w, k.h) * 0.65) :
      Math.max(4, Math.min(cap / 0.73, (k.w - 3) / (0.68 * longest), (k.h - 2) / (1.1 * lines.length)));
    Object.assign(el.style, { background: k.face, color: k.ink, borderRadius: radius(k.radii),
      fontSize: `${px}px`, boxShadow: k.ring ? `inset 0 0 0 0.8px ${k.ring}` : "none" });
    if (k.code !== null) {
      let held = false;
      const up = () => { if (held) { send(`up ${k.code}`); el.classList.remove("held"); } held = false; };
      el.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        el.setPointerCapture(e.pointerId);
        held = true;
        el.classList.add("held");
        send(`down ${k.code}`);
      });
      el.addEventListener("pointerup", up);
      el.addEventListener("pointercancel", up);
      el.addEventListener("lostpointercapture", up);
    }
    keyEls.set(k.name, { el, code: k.code });
    panel.append(el);
  }
  for (const l of [...layout.labels.map((l) => ({ ...l, cap: 4.5 })), ...layout.chin]) {
    const el = document.createElement("div");
    el.className = "print";
    Object.assign(el.style, { left: `${l.x}px`, top: `${l.y}px`, color: layout.print,
      fontSize: `${Math.max(2.5, l.cap / 0.73)}px` });
    el.textContent = l.text;
    panel.append(el);
  }
  fit();
}

function fit() {
  const s = Math.min(innerWidth / layout.width, innerHeight / layout.height);
  panel.style.transform = `scale(${s})`;
  panel.style.left = `${(innerWidth - layout.width * s) / 2}px`;
  panel.style.top = `${(innerHeight - layout.height * s) / 2}px`;
}

// the PC keys that act as remote keys while this window has the focus
const pressed = new Map();
addEventListener("keydown", (e) => {
  if (embedded && e.key === "Escape") {
    e.preventDefault();
    parent.postMessage("beanjar-close-remote", location.origin);
    return;
  }
  const k = keyEls.get(KEYS[e.key]);
  if (!k || k.code === null) return;
  e.preventDefault();
  if (e.repeat || pressed.has(e.key)) return;
  pressed.set(e.key, k);
  k.el.classList.add("held");
  send(`down ${k.code}`);
});
addEventListener("keyup", (e) => {
  const k = pressed.get(e.key);
  if (!k) return;
  pressed.delete(e.key);
  k.el.classList.remove("held");
  send(`up ${k.code}`);
});
addEventListener("blur", () => {
  for (const [key, k] of pressed) { k.el.classList.remove("held"); send(`up ${k.code}`); pressed.delete(key); }
});
addEventListener("resize", () => layout && fit());

// gone with the viewer that opened it
if (!embedded) setInterval(() => { if (!window.opener || window.opener.closed) window.close(); }, 500);

fetch(`${base}/layout.json`).then((r) => r.json()).then((l) => {
  if (l.error) throw new Error(l.error);
  layout = l;
  document.title = `${l.title} - ${name}`;
  draw();
  open();
}).catch((e) => say(`No remote: ${e.message}`));
