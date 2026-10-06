# SkunkCrafts Updater 4.0 (sku_client)

C rewrite of the SkunkCrafts Updater client for X-Plane. Analysis and decisions: [ANALYSIS.md](ANALYSIS.md).

## Layout

```
src/core/     C11 library without GUI: platform (POSIX/Win32), paths and safety, cfg v1/v4,
              CRC32, log, strmap, http (curl), protocol (levels 1 and 2), CRC cache, thread pool,
              X-Plane instance, scan, addon (check / update, remote lists by conditional GET), packs,
              changelog/readme, report; embedded CA bundle
src/cli/      sku: command-line client, same core as the GUI
src/app/      SDL3 + Nuklear graphical client: main (loop, fonts, instance), prefs, jobs (work queue),
              theme, ui_list / ui_detail / ui_pack / ui_options (options, about, instance picker)
tests/        unit + end-to-end tests (fake X-Plane, depot served by python), run in the container
spike/        proof of the build chain (kept, SKU_BUILD_SPIKE option)
deps/         pinned third-party sources and their builds (ignored by git, see deps/fetch.sh)
cmake/, docker/, macos/, tools/   toolchains, Linux build image, .app bundle, utility scripts
```

## Look

Roboto interface font (Apache 2.0), DejaVu Sans Mono for fixed-width text, DejaVu Sans merged in for the two symbols ▶ and ▫ that Roboto lacks. Two renderings to choose from in Options: flat (thin outline around buttons, tabs and menus, distinct menu background) or relief (`relief` preference, two-tone bevels drawn by `widgets.c` over the Nuklear widgets, since Nuklear knows neither gradients nor shadows). Fixed widths are expressed for 16 px text and follow the text size (`UI_W`); list columns and Options rows are proportional to the window. In the list, the first column is a channel drop-down, present only when the addon declares a channel other than the main one; the internal token `stable` is displayed as "Main" (`UI_MAIN_CHANNEL`). The list and the pack member list have a header row whose boundaries can be dragged with the mouse (double click: default proportions), proportions remembered (`list_cols`). `SKU_FONT_FILE=<ttf>` tries another font without rebuilding, `SKU_RELIEF=1` forces relief.

Window system on Linux: X11 first (XWayland under a Wayland desktop), Wayland if no X server can be reached, `video` preference in Options. Reason: on GNOME Wayland, interactive resizing stutters through SDL's Wayland path (the slow-frame trace shows 40 to 70 ms gaps without any event while our frames cost 1 to 2 ms), and it is smooth through XWayland, which also brings the desktop's title bar instead of libdecor's. Downside: on a fractional-scale display XWayland renders blurry; pick Wayland in that case. `SDL_VIDEO_DRIVER` in the environment overrides the preference. With `SKU_TRACE_FRAMES=1` in the environment and verbose logging on, any slow frame or abnormal event wait during a resize is traced (`slow frame:`); `SKU_NOVSYNC=1` turns vertical sync off. Both are for diagnosis only.

## Documentation

- [docs/USER_GUIDE.md](docs/USER_GUIDE.md): user guide, shipped in every package.
- [docs/PUBLISHING.md](docs/PUBLISHING.md): publishing guide for developers (depots, betas, packs).
- [docs/PUBLISH.md](docs/PUBLISH.md): publishing a version of the client (internal).

## Packs

Publishing guide for developers whose product spans several folders: [docs/PUBLISHING.md](docs/PUBLISHING.md) (depots, multiple betas, packs). The pack keys of the remote cfg are adopted at check time, like the depot identity.

## Per-instance data

In each addon: `skunkcrafts_updater.cfg` remains **the** configuration file, in its place and in plain `key|value` with the 3.x keys only, because aircraft read their version there and 3.x and the publishing tools read it too. `skunkcrafts_updater_ignore.txt` remains the user's exclusion list. The private `skunkcrafts/` folder, never published nor deleted by a depot, holds `crc_cache.txt` (CRC cache: `path|size|mtime|crc32`, one line per file, invalidated by size or date), `channels/<name>.cfg` (one beta channel per file: `channel|`, `module|`, `zone|`, the tester file's format) and `channel.cfg` (`channel|<active>`, `stable` = Main). Tester files (`skunkcrafts_updater_beta*.cfg`) are absorbed there: a file for a depot already followed updates that channel (renamed only by an explicit `channel|`), a file for the main depot adds nothing. Saving never deletes a channel file; only `sku_addon_remove_channel` does (details view, after a confirmation, or `sku remove-channel`), keeping it as `<name>.cfg.removed`. The channel keys of 4.0.0 to 4.0.2 (`channel|`, `channel.<n>.module|`, `channel.<n>.zone|` in the root cfg) and installations converted by betas 1 to 9 (`skunkcrafts/updater.cfg`, `skunkcrafts/ignore.txt`) are migrated at first load. `Output/skunkcrafts/updater/` in the X-Plane folder: `updater_log.txt` (rotated at 2 MB), `cache/<addon folder>-<hash>.<list>` and `.meta` (copies of the remote lists with their HTTP validators), `cache/<…>.changelog.txt` / `.readme.txt`, `reports/<…>.txt` (last report), `custom_addons.txt`. The hash is that of the full path, so two addons with the same name are never confused.

## Updating the client itself

At every start, at most once an hour (`client_check` preference, checkbox in Options), the client checks a depot using the same protocol as the addons, `SKU_CLIENT_DEPOT_URL` at build time, then `client_url` in the preferences: if the remote cfg announces another `module`, the client follows it, which lets the depot move without rebuilding. The depot (`updater/depot/` on the store CDN, see [docs/PUBLISH.md](docs/PUBLISH.md)) publishes the deliverables under their versioned name, which the client derives from the announced version (`SkunkcraftsUpdater-<v>-linux.zip`, …, `sku-cli-<v>.zip`), fixed names remaining accepted as a fallback; `release.sh` prepares them in `dist/<version>/depot/` with the cfg and the changelog (`CHANGELOG.txt`, first line = version, like an aircraft changelog). When a newer version exists, a banner above every view offers "What's new" (changelog in About, new entries in colour), "Update" and "Later". "Update" downloads the package for the platform and install mode, checks its CRC, and installs it in place according to the mode (`ui_client.c`, `client.c`):

| Mode | Target | Mechanism |
|---|---|---|
| Linux AppImage | `$APPIMAGE` | copy next to it, `chmod`, `rename` over it (the running inode survives) |
| Linux standalone | `/proc/self/exe` | `SkunkcraftsUpdater_lin` extracted from the zip (`zip.c`, zlib), same replacement |
| Windows standalone | running executable | `SkunkcraftsUpdater.exe` extracted from the zip; the running executable is renamed `.exe.old` (cannot be replaced but can be renamed), the new one takes its name, `.old` deleted at the next start |
| Windows installed | none | the setup is downloaded, "Install now" starts it through `ShellExecuteEx` (elevation) and the client quits; the setup's final page restarts it |
| macOS | bundle of the running executable | zip extracted next to it (`sku_zip_extract_all`), current bundle renamed `.app.old`, new bundle in its place, `.app.old` deleted at the next start |

Then "Restart now" restarts through `sku_spawn_detached` (`fork`/`execv`, `ShellExecuteEx`) with the same arguments. If the replacement fails, the package stays in `<user config>/downloads` with a button to open it. Test hooks: `SKU_NO_CLIENT_CHECK=1` (no check at startup), `SKU_INSTALL_TARGET=<file>` (install somewhere other than over itself).

## CLI

```bash
sku instances                        # known X-Plane installations (x-plane_install_12.txt / _11.txt)
sku --xp DIR list                    # managed addons
sku --xp DIR --dry-run check [name]  # check without writing anything (no cache, no log, no conversion), planned list
sku --xp DIR check --report [name]   # check, CRC cache and report written to Output/skunkcrafts/updater
sku --xp DIR update [name]           # check then update / repair, actual list of files
sku --xp DIR report [name]           # last report
sku --xp DIR changelog [name]        # changelog (sections newer than the installed version marked ">")
sku --xp DIR readme [name]           # readme
sku --xp DIR channels [name]         # beta channels of each addon, the followed one marked "*"
sku --xp DIR remove-channel CHANNEL name   # only when name designates a single addon having CHANNEL
sku client [download [DIR]]          # is a newer version of the client published? (download: fetch it)
# options: --full (ignore the CRC cache) --verbose (debug log on stderr) --threads N --downloads N
#          --create-members (update: create pack members declared but missing)
```

Without `--xp`, the instance is the one of the current folder or of the executable. `name` is a substring of the addon's name or folder, or of a pack's name/identifier (all its members are then processed). `list` groups members under their pack and reports declared members that are missing.

## Graphical client

```bash
./docker/run.sh ./build.sh linux -DSKU_BUILD_APP=ON     # build/linux/src/app/SkunkcraftsUpdater_lin
./build.sh windows -DSKU_BUILD_APP=ON                   # build/windows/src/app/SkunkcraftsUpdater.exe
./build.sh macos   -DSKU_BUILD_APP=ON                   # build/macos/src/app/SkunkcraftsUpdater (universal)
```

`SkunkcraftsUpdater_lin [--xp DIR]`: without `--xp`, the instance comes from the current folder/executable, then from the last one used, then from `x-plane_install_12.txt`; otherwise the picker is shown. Preferences in the user config folder (`prefs.cfg`), automatic migration from `skunkcrafts_updater_go.json`.

Test helpers: `--screenshot DIR` (one BMP capture per second) and `SKU_AUTOPILOT=file` (lines `<n> click <x> <y>`, `<n> drag <x> <y> <x2> <y2>` or `<n> scroll <x> <y> <ticks>` in capture pixels, fed into Nuklear over three frames: hover, press, release, because Nuklear only activates a window that is hovered with the button up). `SKU_NO_CLIENT_CHECK=1` avoids the client's network check at startup. Without a display, `SDL_VIDEO_DRIVER=offscreen SDL_RENDER_DRIVER=software` runs the GUI headless, captures included. See `tests/` for use with the fake X-Plane of `make_fixture.py`.

## Release

Publishing a packaged version to the client depot and the store CDN: `docs/publish.sh <version>`, described in [docs/PUBLISH.md](docs/PUBLISH.md).

```bash
./release.sh 4.0.0            # or 4.0.0-beta1: version stamped into the three binaries
```

Produces in `dist/<version>/` one download per platform and install mode: `SkunkcraftsUpdater-<v>-windows.zip` and `-linux.zip` (the program alone, to drop into X-Plane or elsewhere), `SkunkcraftsUpdater-<v>-windows-setup.exe` (NSIS, `makensis` required), `SkunkcraftsUpdater-<v>-macos.zip` (universal `.app`, arm64 signed ad hoc — never `strip` after linking, symbols are removed by the linker), `SkunkcraftsUpdater-<v>-x86_64.AppImage` (official static runtime + gzip squashfs made in the container, no `appimagetool`; gzip because libappimage, hence AppImageLauncher integration, does not read zstd), `sku-cli-<v>.zip`.

Icons: `packaging/icons/icon.png` → `.ico` (ImageMagick) and `.icns` (`tools/make_icns.py`). Windows resource (icon, version info): `packaging/windows/app.rc.in`.

Testing trap: this machine has **AppImageLauncher**, which intercepts every AppImage execution through `binfmt_misc` (including inside Docker containers, where execution then fails with "No such file or directory"). To test an AppImage without it, zero the 3 magic bytes at offset 8 of a copy (`printf '\x00\x00\x00' | dd of=copy bs=1 seek=8 conv=notrunc`), then run it normally or with `APPIMAGE_EXTRACT_AND_RUN=1`.

Known incompatibility: **AppImageLauncher 2.2.0** (latest stable release, 2020, the one in the repositories and on this machine) cannot start AppImages built with the modern static runtime, ours as well as `appimagetool`'s ("fuse: memory allocation failed", upstream ticket AppImage/type2-runtime#121). Menu integration works, starting does not. Fixed only in AppImageLauncher 3.0 alpha. Going back to the old dynamic runtime is not an option: it requires libfuse2, absent from Ubuntu 22.04 and later. The zip with `SkunkcraftsUpdater_lin` remains the universal solution.

## Tests

```bash
./docker/run.sh bash -c './build.sh linux && cd build/linux && ctest --output-on-failure'
```

## Build chain

Everything is built from Lionel's Linux machine, for the three targets:

| Target | Compiler | Where | Output |
|---|---|---|---|
| Linux x86_64 | Ubuntu 22.04 gcc (glibc 2.35) | `sku-build-linux` Docker container | binary depending only on libc/libm |
| Windows x86_64 | mingw-w64 (`-posix`), static link | host | exe importing system DLLs only |
| macOS universal | osxcross (o64-clang, SDK 11.3), `-arch x86_64 -arch arm64` | host | fat binary, minos 10.15 (x86_64) / 11.0 (arm64), signed ad hoc |

Libraries (sources pinned in `deps/`, all static): SDL3 (window, input, rendering, dialogs), Nuklear (UI), libcurl (HTTP/HTTPS), mbedTLS (TLS on Linux and macOS; Schannel on Windows), zlib (transport compression, zip reader).

```bash
./deps/fetch.sh                        # downloads and unpacks the sources (once)
./deps/build.sh windows                # libraries for Windows
./deps/build.sh macos                  # libraries for macOS (universal)
./docker/run.sh deps/build.sh linux    # libraries for Linux, in the container

./build.sh windows [cmake options]    # e.g. -DSKU_BUILD_APP=ON, -DSKU_BUILD_SPIKE=ON
./build.sh macos   [cmake options]
./docker/run.sh ./build.sh linux [cmake options]

./check.sh linux   build/linux/spike/sku_spike       # max glibc, dynamic libs
./check.sh windows build/windows/spike/sku_spike.exe # imported DLLs
./check.sh macos   build/macos/spike/sku_spike       # archs, minos, dylibs
```

`docker/run.sh` builds the image the first time (Ubuntu 22.04 + X11/Wayland development packages) and runs the command with the caller's UID.

## Particulars

- **compiler-rt missing from osxcross**: the Objective-C `@available` checks in SDL reference `___isPlatformVersionAtLeast`. `spike/apple_compat.c` provides that function (version read through `sysctl kern.osproductversion`); to link into every macOS build.
- **Schannel**: never pass it a CA bundle (`CURLOPT_CAINFO*`), it uses the Windows store. On Linux/macOS, mbedTLS receives the Mozilla bundle (`deps/dl/cacert.pem`, embedded in the application).
- **zlib on macOS**: CMake prefers the SDK's `libz.tbd` (a system library present on every macOS) over our `libz.a`; harmless.
- Windows: the spike exe uses the console subsystem to show the output of `--cli`.

## Spike (build chain validation, 23/09/2026)

`spike/`: SDL3 + Nuklear window (dark/light theme), HTTPS GET in a thread, `--cli [url]` mode. Results:

- Linux: 5.4 MB, GLIBC_2.34 max, NEEDED = libc, libm. HTTPS GET OK (mbedTLS) on the host and in the 22.04 container. Window OK under Wayland.
- Windows: 6.0 MB, system DLLs only. HTTPS GET OK (Schannel) under wine.
- macOS: 8.2 MB universal, minos 10.15/11.0, system frameworks + libSystem/libz/libobjc, arm64 slice signed ad hoc by ld64. Validated on a MacBook Pro M2.
