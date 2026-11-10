// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// VM cards, grouped by host. Only visible Running cards with beanjar-vnc get small previews, and
// Off cards their last screen;
// the server coalesces screenshot requests from several pages. No framework or build step.
const main = document.getElementById("vms");
const icon = (name) => `<i class="bi" style="--i: url(/icons/${name}.svg)"></i>`;
const DOT = { Running: "ok", Starting: "warn", Stopping: "warn", Error: "bad" };
const requesting = new Set();
let timer, refreshing = false;
const previews = new Map();
const observer = new IntersectionObserver((entries) => {
  for (const entry of entries) {
    const preview = previews.get(entry.target.dataset.spec);
    if (!preview || preview.card !== entry.target) continue;
    preview.visible = entry.isIntersecting;
    if (preview.visible) updatePreview(preview);
  }
});

async function updatePreview(preview) {
  if (!preview.card.isConnected || !preview.visible || document.hidden || !preview.eligible || preview.pending || Date.now() - preview.updated < 5000) return;
  if (preview.once && preview.updated) return;
  preview.pending = true;
  preview.controller = new AbortController();
  try {
    const response = await fetch(`${preview.base}/screenshot.png?thumbnail=1`, { signal: preview.controller.signal });
    if (!response.ok || response.status === 204) return;
    const blob = await response.blob();
    if (blob.type !== "image/png" || !preview.eligible) return;
    const old = preview.url;
    preview.url = URL.createObjectURL(blob);
    preview.rotation = response.headers.get("X-Beanjar-Rotation") || "0";
    preview.image.dataset.rotation = preview.rotation;
    preview.image.src = preview.url;
    preview.image.hidden = false;
    preview.placeholder.hidden = true;
    if (old) URL.revokeObjectURL(old);
  } catch (_) {
    // The placeholder stays when a VM stops, a host disappears, or a page hides.
  } finally {
    preview.pending = false;
    preview.updated = Date.now();
  }
}
setInterval(() => { for (const preview of previews.values()) updatePreview(preview); }, 1000);
document.addEventListener("visibilitychange", () => {
  for (const preview of previews.values()) {
    if (document.hidden) preview.controller?.abort();
    else updatePreview(preview);
  }
});

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

// gui.fw_label: "<version>-..._DTV_<board>_..." as "<board> <version>"
function fwLabel(fw) {
  const [version, ...rest] = (fw || "").split("-");
  const parts = rest.join("-").split("_");
  const i = parts.indexOf("DTV");
  return i >= 0 && i < parts.length - 1 && /^[\d.]+$/.test(version) ? `${parts[i + 1]} ${version}` : fw || "";
}
const mem = (m) => (m >= 1024 && m % 1024 === 0 ? `${m / 1024} GiB` : `${m} MiB`);
function duration(s) {
  s = Math.floor(s);
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  const mm = String(m).padStart(2, "0"), ss = String(s % 60).padStart(2, "0");
  return d ? `${d}d ${h}:${mm}` : h ? `${h}:${mm}:${ss}` : `${m}:${ss}`;
}

function card(vm) {
  const host = vm.host || "local";
  const spec = `${host}/${vm.name}`;
  const base = `/vm/${encodeURIComponent(host)}/${encodeURIComponent(vm.name)}`;
  const pending = vm.operation?.status === "pending";
  const state = pending ? (vm.operation.action === "start" ? "Starting" : "Stopping") : vm.state;
  const open = state === "Running" && vm.display === "dbus";
  const r = el("article", "vm-card" + (open ? " open" : ""));
  r.dataset.spec = spec;
  const indicatorState = vm.operation?.status === "failed" ? "Error" : state;
  const indicatorDetail = vm.operation?.status === "failed" ? vm.operation.message : vm.detail;
  const stateDetail = indicatorState + (indicatorDetail ? ` (${indicatorDetail})` : "");
  r.setAttribute("aria-label", `${vm.name}: ${stateDetail}`);
  let link;
  if (open) {
    link = el("a", "card-link");
    link.href = `${base}/`;
    link.setAttribute("aria-label", `Open ${spec}`);
    link.title = vm.viewer ? "Open (the GTK viewer holds it now: the page will say so)" : "Open the viewer";
    r.append(link);
  } else {
    r.title = state === "Running" ? `No picture: display ${vm.display || "none"}` : state;
  }
  const thumbnail = el("div", "thumbnail" + (state === "Off" ? " dim" : ""));
  if (indicatorState !== "Off") {
    const dot = el("span", "state-dot " + (DOT[indicatorState] || "off"));
    dot.setAttribute("role", "img");
    dot.setAttribute("aria-label", stateDetail);
    dot.title = stateDetail;
    (link || thumbnail).append(dot);
  }
  const placeholder = el("div", "placeholder");
  placeholder.innerHTML = icon("tv");
  if (state === "Running") placeholder.append(el("span", "", "No preview"));
  thumbnail.append(placeholder);
  const image = el("img", "preview");
  image.alt = `TV picture of ${spec}`;
  image.hidden = true;
  thumbnail.append(image);
  let preview = previews.get(spec);
  if (!preview) {
    preview = { updated: 0, pending: false, url: null };
    previews.set(spec, preview);
  }
  // Off: the last screen the manager kept, dimmed under the Start button (fetched once a state)
  const off = state === "Off";
  if (preview.off !== off) { preview.updated = 0; preview.off = off; }
  Object.assign(preview, { card: r, base, image, placeholder, visible: false, eligible: (open && vm.vnc) || off, once: off });
  if (preview.eligible && preview.url) {
    image.dataset.rotation = preview.rotation || "0";
    image.src = preview.url;
    image.hidden = false;
    placeholder.hidden = true;
  }
  r.append(thumbnail);
  const content = el("div", "card-info");
  content.append(el("h4", "name", vm.name));
  const activity = [vm.platform, state !== "Off" && vm.uptime ? duration(vm.uptime) : ""];
  if (state === "Running" && vm.vnc) activity.push("VNC");
  if (state === "Running" && vm.viewer) activity.push("GTK viewer open");
  const details = el("div", "details", activity.filter(Boolean).join(" · "));
  details.title = [vm.platform, fwLabel(vm.firmware), vm.mem ? mem(vm.mem) : "", vm.smp ? `${vm.smp} CPUs` : ""].filter(Boolean).join(" · ");
  content.append(details);
  if (vm.tags?.length) {
    const tags = el("div", "tags");
    for (const tag of vm.tags) tags.append(el("span", "tag", tag));
    content.append(tags);
  }
  if (vm.reason && state !== "Off") {
    // why it runs (beanjar reason): stop asks before it overrides it
    const reason = el("div", "reason");
    reason.innerHTML = icon("pin-angle");
    reason.append(vm.reason.text);
    reason.title = reasonLine(vm.reason);
    content.append(reason);
  }
  const operation = vm.operation;
  // Routine success is the new state. Retain refusals and the manager's explicit stop fallback.
  const result = operation?.status === "failed" ? operation.message :
    operation?.status === "succeeded" && operation.action === "stop" ?
      operation.message.split("\n").filter((line) => line.includes("syncing its disks and stopping it")).join("\n") : "";
  if (state === "Error" && vm.detail && vm.detail !== result) {
    const error = el("span", "operation error", vm.detail);
    error.setAttribute("role", "status");
    content.append(error);
  }
  if (result) {
    const message = el("span", "operation" + (operation.status === "failed" ? " error" : ""), result);
    message.setAttribute("role", "status");
    content.append(message);
  }
  r.append(content);
  if (state === "Off") {
    const start = el("button", "card-start", "Start");
    start.insertAdjacentHTML("afterbegin", icon("play-fill"));
    start.setAttribute("aria-label", `Start ${spec}`);
    start.title = "Start";
    start.disabled = requesting.has(spec);
    start.addEventListener("click", () => vmAction(spec, base, "start", start));
    thumbnail.append(start);
  } else if (state === "Running") {
    const toggle = el("button", "card-menu-button");
    toggle.innerHTML = icon("three-dots-vertical");
    toggle.setAttribute("aria-label", `Actions for ${spec}`);
    toggle.setAttribute("aria-expanded", "false");
    const menu = el("div", "card-menu");
    menu.setAttribute("popover", "auto");
    menu.id = `vm-menu-${encodeURIComponent(spec)}`;
    toggle.setAttribute("popovertarget", menu.id);
    toggle.setAttribute("aria-controls", menu.id);
    toggle.setAttribute("aria-haspopup", "menu");
    menu.setAttribute("role", "menu");
    if (open) {
      const view = el("a", "menu-item", "Open");
      view.href = `${base}/`;
      view.setAttribute("role", "menuitem");
      view.prepend(document.createRange().createContextualFragment(icon("box-arrow-up-right")));
      menu.append(view);
    }
    const stop = el("button", "menu-item destructive", "Stop");
    stop.setAttribute("role", "menuitem");
    stop.insertAdjacentHTML("afterbegin", icon("power"));
    stop.disabled = requesting.has(spec);
    stop.addEventListener("click", () => {
      menu.hidePopover();
      vmAction(spec, base, "stop", stop, vm.reason);
    });
    menu.append(stop);
    menu.addEventListener("toggle", (event) => {
      toggle.setAttribute("aria-expanded", String(event.newState === "open"));
      if (event.newState !== "open") return;
      const bounds = toggle.getBoundingClientRect();
      menu.style.top = `${bounds.bottom + 4}px`;
      menu.style.left = `${Math.max(8, Math.min(innerWidth - 180, bounds.right - 168))}px`;
      menu.querySelector("a, button").focus();
    });
    menu.addEventListener("keydown", (event) => {
      const items = [...menu.querySelectorAll("a, button")];
      const current = items.indexOf(document.activeElement);
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        items[(current + (event.key === "ArrowDown" ? 1 : -1) + items.length) % items.length].focus();
      }
      if (event.key === "Escape") { menu.hidePopover(); toggle.focus(); }
    });
    r.append(toggle, menu);
  }
  return r;
}

// a reason as `beanjar reason` prints it: its text, then who set it and when
function reasonLine(r) {
  const who = [r.user, r.by].filter(Boolean).join(", ");
  return `${r.text} (set ${r.set || "?"}${who ? ` by ${who}` : ""})`;
}

async function vmAction(spec, base, action, button, reason) {
  const why = reason ? `\n\nIt runs for a reason: ${reasonLine(reason)}\nStop it anyway?` : "";
  if (action === "stop" && !confirm(`Stop ${spec}? The manager will try to power it off, then sync and stop it if the guest refuses or does not shut down.${why}`)) return;
  requesting.add(spec);
  button.disabled = true;
  try {
    // a reason shown and confirmed: stop --force (the manager refuses a reason it has not shown)
    const response = await fetch(`${base}/${action}${reason ? "?force=1" : ""}`, { method: "POST" });
    const body = await response.json();
    if (!response.ok) throw new Error(body.error || response.statusText);
  } catch (e) {
    alert(`Cannot ${action} ${spec}: ${e.message}`);
  } finally {
    requesting.delete(spec);
    await refresh();
  }
}

function status(iconName, title, text) {
  const page = el("div", "status-page");
  const i = el("span", "big");
  i.innerHTML = icon(iconName);
  page.append(i, el("h2", "", title), el("p", "", text));
  main.replaceChildren(page);
}

async function refresh() {
  if (refreshing) return;
  // A menu remains stable under the pointer/finger until the user closes it.
  if (document.querySelector(".card-menu:popover-open")) { timer = setTimeout(refresh, 2000); return; }
  refreshing = true;
  clearTimeout(timer);
  let rows;
  try {
    const r = await fetch("/api/vms");
    rows = await r.json();
    if (!r.ok) throw new Error(rows.error || r.statusText);
  } catch (e) {
    refreshing = false;
    timer = setTimeout(refresh, 10000);
    return status("exclamation-triangle", "Cannot List the VMs", `beanjar list failed: ${e.message}`);
  }
  refreshing = false;
  timer = setTimeout(refresh, rows.some((vm) => vm.operation?.status === "pending") ? 2000 : 10000);
  if (!rows.length)
    return status("tv", "No Virtual Machines",
      "Create a VM from a webOS firmware file (.epk) or extracted firmware with the Beanjar window or beanjar create.");
  const hosts = new Map();
  for (const vm of rows) {
    const h = vm.host || "local";
    if (!hosts.has(h)) hosts.set(h, []);
    hosts.get(h).push(vm);
  }
  const order = [...hosts.keys()].sort((a, b) => (a === "local" ? -1 : b === "local" ? 1 : a.localeCompare(b)));
  const sections = order.map((h) => {
    const sec = el("section");
    const head = el("h3");
    head.innerHTML = icon(h === "local" ? "pc-display" : "hdd-network");
    head.append(h === "local" ? "This Machine" : h);
    const list = el("div", "vm-grid");
    for (const vm of hosts.get(h)) list.append(card(vm));
    sec.append(head);
    sec.append(list);
    return sec;
  });
  observer.disconnect();
  main.replaceChildren(...sections);
  const present = new Set(rows.map((vm) => `${vm.host || "local"}/${vm.name}`));
  for (const [spec, preview] of previews) {
    if (!present.has(spec)) {
      preview.controller?.abort();
      if (preview.url) URL.revokeObjectURL(preview.url);
      previews.delete(spec);
    } else observer.observe(preview.card);
  }
}

document.getElementById("refresh").addEventListener("click", refresh);
// Log Out, with --auth password
fetch("/api/session")
  .then((r) => r.json())
  .then((s) => { document.getElementById("logout").hidden = s.auth !== "password"; })
  .catch(() => {});
refresh();
