/*
 * Intentionally empty translation unit.
 *
 * The MinGW-w64 GCC driver appends its own default-manifest.o to every link
 * (see the "default-manifest.o" entry in gcc -dumpspecs). That produces two
 * RT_MANIFEST resources when the application embeds its own manifest, and the
 * linker reports ".rsrc merge failure: multiple non-default manifests".
 *
 * The build script compiles this stub as build/default-manifest.o and links
 * with -B build, so the driver finds the empty stub first and the manifest
 * from resources/app.rc remains the only one.
 */
