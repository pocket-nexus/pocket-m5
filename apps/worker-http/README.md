# Worker HTTP

A device check for the worker's `io.http` resource on `programs/m5stack-tab5`
(host `tab5-home`). The Wi-Fi page scans and joins a network through
`@pocketjs/framework/wifi`, with an on-screen keyboard for the passphrase;
the radio saves a joined network in NVS. When the radio first connects the UI
publishes target 0 on the `request` ring, and the HTTP page publishes a
target's index on each tap; the worker opens `io.http("http")`, streams the
response and publishes
phase, byte count, resource status and HTTP status on the `progress`
snapshot. The worker prints each headers record and error fact on the serial
console.

The firmware bundles the app (`POCKETJS_TAB5_BUNDLED_APPS`): Pocket Home
lists it as "Worker HTTP". Built on its own and embedded as the home
package, it runs in place of Pocket Home:

```sh
bun run app build apps/worker-http/pocket.app.json
idf.py -C programs/m5stack-tab5 \
  -DPOCKETJS_TAB5_HOME_PACKAGE=$PWD/.pocket-build/apps/worker-http.pocket build
```

The baked
glyphs cover printable ASCII; an SSID outside it shows missing glyphs.
