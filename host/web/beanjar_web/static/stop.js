// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Stop: an in-page <dialog>, the list's and the viewer's, in the GTK manager's Shut Down wording.
// Send power-off signal is POST stop (`beanjar stop`: the signal, then the disks synced and QEMU
// stopped if webOS refuses it or has not powered off in time); Unplug is POST stop?unplug=1
// (`beanjar stop --unplug`). A VM that runs for a reason has it in the dialog, which is the
// confirmation stop?force=1 sends. The server's refusal shows in the dialog, which stays open.
let dialog = null;

function build() {
  dialog = document.createElement("dialog");
  dialog.className = "dialog";
  dialog.id = "stop-dialog";
  dialog.setAttribute("aria-labelledby", "stop-title");
  dialog.innerHTML = `<form method="dialog">
<h2 id="stop-title"></h2>
<p class="dialog-body" id="stop-body"></p>
<div class="choices" role="radiogroup" aria-label="How should the VM stop?">
<label class="choice"><input type="radio" name="how" value="signal" checked><span><b>Send power-off signal</b><small>webOS powers the TV off, and the VM stops by itself</small></span></label>
<label class="choice"><input type="radio" name="how" value="unplug"><span><b>Unplug (may lose data)</b><small>Kills the VM at once, as a TV losing its power: what webOS has not written to its disks is lost</small></span></label>
</div>
<p class="dialog-error" role="alert" hidden></p>
<div class="dialog-buttons"><button value="cancel" formnovalidate>Cancel</button><button value="stop" class="suggested">Stop</button></div>
</form>`;
  dialog.setAttribute("aria-describedby", "stop-body");
  document.body.append(dialog);
  const ok = dialog.querySelector('button[value="stop"]');
  // as the GTK dialog: Unplug turns the button destructive and names it
  for (const radio of dialog.querySelectorAll('input[name="how"]')) {
    radio.addEventListener("change", () => {
      const unplug = dialog.querySelector('input[value="unplug"]').checked;
      ok.textContent = unplug ? "Unplug" : "Stop";
      ok.className = unplug ? "destructive" : "suggested";
    });
  }
}

// when and by whom a reason was set, as the desktop manager words it: "set 10:58 by user" (today
// in the browser's time zone), "set 11 Nov 10:58 by user, agent" (before), the stamp kept as is
// if it does not parse
export function reasonNote(r, now = new Date()) {
  let stamp = r.set || "?";
  const when = new Date(String(r.set || "").replace(/([+-]\d\d)(\d\d)$/, "$1:$2"));
  if (!Number.isNaN(when.getTime())) {
    const time = when.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hourCycle: "h23" });
    stamp = when.toDateString() === now.toDateString() ? time
      : `${when.toLocaleDateString([], { day: "numeric", month: "short" })}${
        when.getFullYear() === now.getFullYear() ? "" : " " + when.getFullYear()} ${time}`;
  }
  const who = [r.user, r.by].filter(Boolean).join(", ");
  return `set ${stamp}${who ? ` by ${who}` : ""}`;
}

// why it runs: its text, then who set it and when (every place the page shows a reason uses this)
export function reasonLine(r) {
  return `${r.text} (${reasonNote(r)})`;
}

// Open the dialog for a VM: spec HOST/NAME, base its /vm/HOST/NAME, reason its `reason` or null.
// accepted() runs once the server has taken the stop (202), the dialog closed.
export function stopDialog({ spec, base, reason, accepted }) {
  if (!dialog) build();
  const name = spec.replace(/^local\//, "");
  const form = dialog.querySelector("form");
  const ok = dialog.querySelector('button[value="stop"]');
  const error = dialog.querySelector(".dialog-error");
  dialog.querySelector("h2").textContent = `Stop ${name}?`;
  dialog.querySelector(".dialog-body").textContent =
    (reason ? `It runs for: ${reasonLine(reason)}. ` : "") + "How should the VM stop?";
  dialog.querySelector('input[value="signal"]').checked = true;
  ok.textContent = "Stop";
  ok.className = "suggested";
  ok.disabled = false;
  error.hidden = true;
  form.onsubmit = async (event) => {
    if (event.submitter?.value !== "stop") return; // Cancel closes it
    event.preventDefault();
    const unplug = dialog.querySelector('input[value="unplug"]').checked;
    const query = new URLSearchParams();
    if (reason) query.set("force", "1");
    if (unplug) query.set("unplug", "1");
    ok.disabled = true;
    error.hidden = true;
    try {
      const r = await fetch(`${base}/stop${query.size ? `?${query}` : ""}`, { method: "POST" });
      const body = await r.json().catch(() => ({}));
      if (!r.ok) throw new Error(body.error || r.statusText);
      dialog.close("stop");
      accepted?.(body);
    } catch (e) {
      error.textContent = `Cannot stop ${name}: ${e.message}`;
      error.hidden = false;
      ok.disabled = false;
    }
  };
  dialog.showModal();
  dialog.querySelector('input[value="signal"]').focus();
  return dialog;
}
