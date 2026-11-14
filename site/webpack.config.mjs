// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Builds the site into build/site: the Coming soon page (index.html, with coming-soon.css), the
// new landing page (index-new.html) and images/ as they are, and the new page's site.css compiled
// from scss/site.scss. A production build (npm run build) drops every selector the new page
// never matches (PurgeCSS), then prefixes for the browserslist targets in package.json
// and minifies (Lightning CSS); it emits no JavaScript. A development build (npm run dev)
// keeps the whole stylesheet unminified and serves the page with live reload.

import { fileURLToPath } from "node:url";
import path from "node:path";
import browserslist from "browserslist";
import CopyPlugin from "copy-webpack-plugin";
import CssMinimizerPlugin from "css-minimizer-webpack-plugin";
import HtmlWebpackPlugin from "html-webpack-plugin";
import { browserslistToTargets } from "lightningcss";
import MiniCssExtractPlugin from "mini-css-extract-plugin";
import { PurgeCSSPlugin } from "purgecss-webpack-plugin";
import RemoveEmptyScriptsPlugin from "webpack-remove-empty-scripts";

const here = path.dirname(fileURLToPath(import.meta.url));
const page = path.join(here, "index-new.html");

// The pages link their stylesheets themselves, so the HTML plugin adds no stylesheet tag. In
// development it adds the script that carries the dev server's live-reload client; in
// production there is no script to add.
class PageTags {
  constructor(production) {
    this.production = production;
  }

  apply(compiler) {
    compiler.hooks.compilation.tap("PageTags", (compilation) => {
      HtmlWebpackPlugin.getCompilationHooks(compilation).alterAssetTags.tap("PageTags", (data) => {
        data.assetTags.styles = [];
        if (this.production) data.assetTags.scripts = [];
        return data;
      });
    });
  }
}

export default (_env, argv) => {
  const production = argv.mode !== "development";
  return {
    mode: production ? "production" : "development",
    context: here,
    entry: { site: "./scss/site.scss" },
    output: {
      path: path.resolve(here, "../build/site"),
      clean: true,
    },
    devtool: false,
    module: {
      rules: [
        {
          test: /\.scss$/,
          use: [
            MiniCssExtractPlugin.loader,
            // Bootstrap's url()s are inline SVG data; the page's images are copied as they are.
            { loader: "css-loader", options: { url: false, import: false } },
            {
              loader: "sass-loader",
              options: {
                sassOptions: {
                  quietDeps: true,
                  // Bootstrap 5.3 is written with @import; it moves to @use in Bootstrap 6.
                  silenceDeprecations: ["import"],
                },
              },
            },
          ],
        },
      ],
    },
    plugins: [
      new MiniCssExtractPlugin({ filename: "site.css" }),
      new HtmlWebpackPlugin({ template: page, filename: "index-new.html", inject: "body", minify: false }),
      new HtmlWebpackPlugin({ template: "index.html", inject: false, minify: false }),
      new PageTags(production),
      new CopyPlugin({ patterns: [{ from: "images", to: "images" }, "coming-soon.css"] }),
      ...(production
        ? [
            new RemoveEmptyScriptsPlugin(),
            new PurgeCSSPlugin({
              paths: [page],
              // Keep every custom property and @keyframes, used or not: Bootstrap's :root and
              // [data-bs-theme] variables are read through var() chains PurgeCSS cannot follow.
              variables: false,
              keyframes: false,
              fontFace: false,
            }),
          ]
        : []),
    ],
    optimization: {
      minimize: production,
      minimizer: [
        new CssMinimizerPlugin({
          minify: CssMinimizerPlugin.lightningCssMinify,
          minimizerOptions: {
            targets: browserslistToTargets(browserslist(undefined, { path: here })),
          },
        }),
      ],
    },
    performance: { hints: false },
    devServer: {
      host: "127.0.0.1",
      port: 8090,
      static: false,
      hot: false,
      liveReload: true,
      // Sass imports are watched as build dependencies; the page is watched to reload it.
      watchFiles: [page, "index.html", "coming-soon.css"],
    },
  };
};
