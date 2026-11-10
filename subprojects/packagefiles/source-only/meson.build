# Copied into the unpacked tree of a wrap whose package does not build with Meson (its
# patch_directory): Meson's wrap handling (`meson subprojects download`, meson/fetch.py) fetches,
# checks, unpacks and patches only trees with a build file it knows. The tree is built by its own
# build system from a custom target (docs/dev/guides/build.md, "Downloads"); this project is never configured.
project('source-only', meson_version: '>= 1.7')
error('a source tree for a custom target, not a Meson subproject')
