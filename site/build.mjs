// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Builds the landing page into build/site: index.html and images/ as they are, and site.css
// compiled from scss/site.scss, with every selector the page never matches dropped
// (PurgeCSS), then prefixed for the browserslist targets in package.json and minified
// (Lightning CSS).

import { cp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import browserslist from "browserslist";
import { browserslistToTargets, transform } from "lightningcss";
import { PurgeCSS } from "purgecss";
import * as sass from "sass";

const here = path.dirname(fileURLToPath(import.meta.url));
const out = path.resolve(here, "../build/site");
const page = path.join(here, "index.html");

const compiled = sass.compile(path.join(here, "scss/site.scss"), {
  loadPaths: [path.join(here, "node_modules")],
  quietDeps: true,
  // Bootstrap 5.3 is written with @import; it moves to @use in Bootstrap 6.
  silenceDeprecations: ["import"],
});

const [purged] = await new PurgeCSS().purge({
  content: [page],
  css: [{ raw: compiled.css }],
  // Keep every custom property and @keyframes, used or not: Bootstrap's :root and
  // [data-bs-theme] variables are read through var() chains PurgeCSS cannot follow.
  variables: false,
  keyframes: false,
  fontFace: false,
});

const { code } = transform({
  filename: "site.css",
  code: Buffer.from(purged.css),
  minify: true,
  targets: browserslistToTargets(browserslist(undefined, { path: here })),
});

await rm(out, { recursive: true, force: true });
await mkdir(out, { recursive: true });
await writeFile(path.join(out, "site.css"), code);
await writeFile(path.join(out, "index.html"), await readFile(page));
await cp(path.join(here, "images"), path.join(out, "images"), { recursive: true });

console.log(`build/site: site.css ${code.length} bytes (${compiled.css.length} before purging)`);
