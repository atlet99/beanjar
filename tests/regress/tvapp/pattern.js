// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The draw step's pattern (scripts/regress --tv; tests/regress/lib/tvsuite.py reads this same file):
// each mode's background and the solid patches, rect = [x, y, width, height] as fractions of the
// window. Keep it one JSON value: the host parses it.
var PATTERN = {
 "modes": {"canvas": [64, 64, 64], "webgl": [40, 40, 112]},
 "patches": [
  {"name": "red", "rgb": [255, 0, 0], "rect": [0.08, 0.12, 0.22, 0.30]},
  {"name": "green", "rgb": [0, 255, 0], "rect": [0.39, 0.12, 0.22, 0.30]},
  {"name": "blue", "rgb": [0, 0, 255], "rect": [0.70, 0.12, 0.22, 0.30]},
  {"name": "white", "rgb": [255, 255, 255], "rect": [0.08, 0.58, 0.22, 0.30]},
  {"name": "yellow", "rgb": [255, 255, 0], "rect": [0.39, 0.58, 0.22, 0.30]},
  {"name": "cyan", "rgb": [0, 255, 255], "rect": [0.70, 0.58, 0.22, 0.30]}
 ],
 "nowebgl": [255, 0, 255]
};
