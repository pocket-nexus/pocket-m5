# Pocket Shell

The launcher of `programs/m5stack-tab5`: the springboard of installed apps,
Settings (Wi-Fi, Mac pairing by QR code, the paired Mac's app catalog) and
the clock. The firmware compiles this directory with
`programs/m5stack-tab5/pocket.host.config.ts` and embeds it as the launcher
package; the floating home ball returns to it.

The code comes from
[pocket-shell](https://github.com/pocket-nexus/pocket-shell) at `188c46c`:
`shells/3ds/home` and the files of `shells/3ds/ui` it imports. The files keep
their `SPDX-License-Identifier: GPL-3.0-or-later` line. This repository no
longer pins pocket-shell; changes to the launcher land here.

| Field | Value |
| --- | --- |
| Package id | `dev.pocket-nexus.shell` |
| Output | `pocket-shell` |
| Tick rate | 30 Hz (`app.tickHz`), the Tab5's cadence |
| Surface | adaptive auxiliary, 320×240 to 640×720; 640×360 on the Tab5, presented at 2x |

## Files

| Path | Contents |
| --- | --- |
| `main.tsx`, `app.tsx` | entry and root component: the springboard, the page stack, launch failures |
| `springboard.tsx`, `layout.ts` | the Home screen: icon pages, page dots and the dock; the order is kept in `layout.json` in Pocket Shell's `data.fs` root |
| `pages.tsx`, `scanner.tsx`, `store.tsx` | Settings: Wi-Fi, Pair a Mac (QR code from the camera), the Mac's catalog and installs, an install code's page |
| `system.ts` | the launcher's app table, the Pocket Link and Wi-Fi state, polled every 30 frames (6 while a connection or pairing changes) |
| `clock.ts`, `display.ts` | wall-clock fields from `Date.now()` with a fixed UTC offset; the display mode for Settings |
| `ui/` | the iOS 6 kit: navigation stack and bar, grouped tables, alerts, text fitting, the touch router |
| `art/`, `images.json` | SVG artwork and the icon textures, each with an `@2x` texture for a density-2 build |

## On the Tab5

- The springboard fits columns of about 96 px and rows of at least 74 px
  over the dock: six by three on 640×360. The dock holds up to four icons.
- Installed apps are the firmware's bundled apps (`POCKETJS_TAB5_BUNDLED_APPS`)
  and packages installed over the Pocket Link from a paired Mac's catalog.
- The animation class is `repaint` (no compositor): the wallpaper is flat and
  page changes and the navigation stack cut instead of animating.
- Settings > Wi-Fi scans, joins with a password typed on the on-screen or the
  Tab5 keyboard, and forgets saved networks (`wifiCanJoin()` is true on the
  Tab5).
- Installs from a code: the firmware binds `globalThis.installer` into the
  launcher, and where it is present the Mac store shows Scan a Code... The
  scanner reads an install URI
  (`programs/m5stack-tab5/components/pocket_system/README.md`, "Installs from
  a code"), closes the camera and shows the app's title and the server's host
  with Install and Cancel; the download's progress, then Open or the
  firmware's error, follow on that page. `system.ts` parses the URI, starts
  the download and polls the installer every frame while it runs. Either
  scanner entry takes either code: a pairing code pairs. A host without the
  installer shows no row.

## Icons

The build reads the committed PNGs in `art/icons/`. `bun
tools/pocket-shell-icons.ts [--only hn,mac]` re-renders them: Blender
(`BLENDER`, default `/Applications/Blender.app/Contents/MacOS/Blender`) runs
`tools/pocket-shell-icons/icons.py` into `.pocket-build/pocket-shell-icons/render`,
`nexus.ts` draws the Pocket Nexus picture with `@napi-rs/canvas`, and
`bake.ts` writes the 24, 46 and 64 px textures and a contact sheet in
`.pocket-build/validation/pocket-shell-icons`. Cycles output differs a little between
runs, so a re-render is a new set of pictures to review. GitHub's
`github-{24,46,64}.png` (and `@2x`) are not rendered here: they are
pocket-shell's launcher textures (`shells/3ds/shell/art/icons`), GitHub's
mark on the dark glass tile the applets repository draws
(pocket-nexus/applets `tools/icons`).

## Tests

`tests/pocket-shell-clock.test.ts` checks the clock arithmetic;
`tools/test-worker-io.sh` runs it with the other host tests.
