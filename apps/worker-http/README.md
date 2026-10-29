# Worker HTTP

A device check for the worker's `io.http` resource on `programs/m5stack-tab5`
(profile `tab5-home`). The Wi-Fi page scans and joins a network through
`@pocketjs/framework/wifi`, with an on-screen keyboard for the passphrase;
the radio saves a joined network in NVS. When the radio first connects the UI
publishes target 0 on the `request` ring, and the HTTP page publishes a
target's index on each tap; the worker opens `io.http("http")`, streams the
response and publishes
phase, byte count, resource status and HTTP status on the `progress`
snapshot. The worker prints each headers record and error fact on the serial
console.

```sh
bun tools/app.ts build apps/worker-http/pocket.json \
  --runtime-config programs/m5stack-tab5/home.runtime-config.ts
idf.py -C programs/m5stack-tab5 -DPOCKETJS_TAB5_APP=home \
  -DPOCKETJS_TAB5_HOME_PACKAGE=$PWD/.pocket-build/apps/tab5/worker-http.pocket build
```

Embedded as the home package it runs in place of Pocket Home. The baked
glyphs cover printable ASCII; an SSID outside it shows missing glyphs.
