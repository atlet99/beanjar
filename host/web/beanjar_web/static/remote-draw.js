// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The remote drawn from layout.json (the GTK remote's data, CSS pixels on a width x height panel),
// for the remote page (remote.js: its window and the phones' full-remote popover) and the desktop
// viewer's floating remote (viewer.js). Each key with a code sends "down CODE" on press and
// "up CODE" on release through send(), so holding a key is a long press; keys without a code are
// dimmed. A press on a key keeps the focus where it was (the viewer's picture keeps the keyboard).
// Bootstrap Icons, served from the system package just like the viewer toolbar.
export const GLYPHS = {
  POWER: "power", INPUT: "box-arrow-in-right", SEARCH: "search", MUTE: "volume-mute",
  HOME: "house", GEAR: "gear", PLUS: "plus", MINUS: "dash",
  CH_UP: "chevron-up", CH_DOWN: "chevron-down", UP: "chevron-up",
  DOWN: "chevron-down", LEFT: "chevron-left", RIGHT: "chevron-right",
  STOP: "stop-fill", REWIND: "rewind-fill", PLAY: "play-fill", PAUSE: "pause-fill",
  FORWARD: "fast-forward-fill", PREV: "skip-start-fill", NEXT: "skip-end-fill",
};

function box(el, x, y, w, h) {
  Object.assign(el.style, { left: `${x}px`, top: `${y}px`, width: `${w}px`, height: `${h}px` });
  return el;
}

function radius(r) { return r.map((v) => `${v}px`).join(" "); }

// Draw layout into panel (an element of class remote-panel, emptied first); the keys by name
// ({el, code}), for a page that maps the PC's keys to them
export function drawRemote(panel, layout, send) {
  const keyEls = new Map();
  const b = layout.body;
  panel.replaceChildren();
  panel.style.width = `${layout.width}px`;
  panel.style.height = `${layout.height}px`;
  const body = box(document.createElement("div"), b.x, b.y, b.w, b.h);
  body.className = "remote-body";
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
    el.addEventListener("pointerdown", (e) => e.preventDefault()); // no focus change, no selection
    if (k.code !== null) {
      let held = false;
      const up = () => { if (held) { send(`up ${k.code}`); el.classList.remove("held"); } held = false; };
      el.addEventListener("pointerdown", (e) => {
        if (e.button !== 0 || held) return;
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
  return keyEls;
}
