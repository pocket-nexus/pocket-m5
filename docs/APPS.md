# Apps: build, install and switch

An app is a directory with a `pocket.json` manifest. The firmware runs
`.pocket` packages compiled for its program's profile; the package records the
profile hash, and the device rejects a package without a matching variant.

## Build a package

```sh
bun run app build apps/hello/pocket.json
# -> .pocket-build/apps/tab5/hello.pocket
```

`tools/app.ts` runs `pocket compile` with
`programs/<board>/pocket.runtime-config.ts` (`--board tab5` by default). The
manifest must request only capabilities in the profile (`input.touch`,
`text.glyphs.baked`) and a viewport the profile offers (`360×640`, `native`).

## Install on the device

1. Copy the package to `/pocket/inbox/` on the microSD card (FAT). With the
   card mounted on the build machine:
   `bun run app install apps/hello/pocket.json --sd /Volumes/<card>`.
2. Insert the card and reset the Tab5.

At boot the host validates each inbox package and moves it to
`/pocket/apps/<output>.pocket`; a package with the same `app.output` is
replaced. A rejected package is renamed `<file>.rejected` and listed with its
reason on the home screen. Packages copied directly into `/pocket/apps/` are
listed without the checksum step. Deleting a file from `/pocket/apps/`
uninstalls the app.

## Switch between apps

- Tap an app on the home screen to launch it. `launchApp(output)` returns
  `true` when the switch is scheduled; it takes effect after the current
  frame.
- Swipe up from the bottom 16 logical pixels by at least 48 pixels to return
  home. The app does not receive that contact. Home shows `RESUME` on the app
  that was left; launching it starts it fresh.
- Each switch destroys the QuickJS realm and runtime and boots the next
  package on a new runtime. **App state does not survive a switch.**

## Errors

An app that fails to boot or throws from a frame returns the device to home,
which shows the app output and the error text from `appTable().lastError`.
Serial output (`idf.py monitor`) carries the same message under the
`pocket_host` tag, plus free internal RAM and PSRAM at every launch.

## Home app

`apps/home` is compiled during the firmware build and embedded in the image.
It reads the host's `appTable` JSON through `@pocketjs/framework/host` for
the fields beyond the launcher API (`storage`, `lastError`, `installErrors`)
and launches through `@pocketjs/framework/launcher`. Rebuilding the firmware
is required after changing it.
