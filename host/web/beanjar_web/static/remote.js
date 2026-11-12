// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The remote's page, in a popup window or the phones' full-remote popover: the VM's remote drawn
// from layout.json (remote-draw.js), each key's LG IR code sent over /vm/HOST/NAME/remote: down on press, up on
// release, so holding a key is a long press. Keys without a code are dimmed. While the window has
// the focus, the arrows, Enter and Backspace are the remote's arrows, OK and Back. It closes itself
// when the viewer that opened it goes away. Embedded (the viewer's full-remote popover) the page is
// transparent and the remote's body fills it, so the body is the only frame inside the popover's.
import { drawRemote } from "/remote-draw.js";

const embedded = window.parent !== window;
document.documentElement.classList.toggle("embedded", embedded);
const vm = new URLSearchParams(location.search).get("vm") || "";
const [host, name] = vm.split("/");
const base = `/vm/${encodeURIComponent(host)}/${encodeURIComponent(name)}`;
const wsUrl = `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}${base}/remote`;
const panel = document.getElementById("panel");
const rstatus = document.getElementById("rstatus");
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

function draw() {
  for (const [name, k] of drawRemote(panel, layout, send)) keyEls.set(name, k);
  fit();
}

// the panel scaled to the window and centred in it; embedded, its body rather than the whole panel
function fit() {
  const b = embedded ? layout.body : { x: 0, y: 0, w: layout.width, h: layout.height };
  const s = Math.min(innerWidth / b.w, innerHeight / b.h);
  panel.style.transform = `scale(${s})`;
  panel.style.left = `${(innerWidth - b.w * s) / 2 - b.x * s}px`;
  panel.style.top = `${(innerHeight - b.h * s) / 2 - b.y * s}px`;
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
