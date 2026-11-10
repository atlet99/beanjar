# The layouts' widget-ID contract

Each `ui/*.blp` template exposes named objects; the controller listed beside
it reaches them as `Gtk.Template.Child()`. Renaming either side breaks the
other at runtime (no build error), so keep this table in step.

| Layout (`ui/`)      | Template (`$…`)      | Controller                              | Child IDs |
|---|---|---|---|
| `window.blp`        | `$ViewerWindow`      | `beanjar_ui/viewer/window.py ViewerWindow` | `toolbar`, `title_label`, `subtitle_label`, `back_button`, `home_button`, `settings_button`, `screenshot_button`, `rotate_button`, `mouse_button`, `fullscreen_button`, `remote_button`, `toasts`, `local_key_group`, `local_key_button`, `joystick_button`, `joystick_popover`, `joystick_up`, `joystick_down`, `joystick_left`, `joystick_right`, `joystick_press` |
| `pane.blp`          | `$ViewerPane`        | `beanjar_ui/viewer/pane.py ViewerPane`     | none (Python appends `TvView`, `StatusBar`) |
| `remote-window.blp` | `$ViewerRemoteWindow`| `beanjar_ui/viewer/remote_window.py RemoteWindow` | `remote_menu` (a `menu`, wired to the `remote.*` actions) |
| `statusbar.blp`     | `$ViewerStatusBar`   | `beanjar_ui/viewer/statusbar.py`           | `cpu`, `disk`, `net` |
| `statusfield.blp`   | `$ViewerStatusField` | `beanjar_ui/viewer/statusbar.py`           | `icon` |

Template callbacks (`$on_…()` in the layouts) are the `@Gtk.Template.Callback()`
methods of the same controllers; renaming those breaks the same way.
