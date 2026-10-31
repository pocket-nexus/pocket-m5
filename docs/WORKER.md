# Worker domain

The `pocket_worker_esp` component owns a second QuickJS runtime on a FreeRTOS task. The task runs the shared runner in PocketJS `engine/worker/src/runner.c`: the component supplies the FreeRTOS mutex, task notification, one-shot timer wait and allocator, and the runner holds bootstrap, the step/deadline/wait loop, fault retention and destroy. The worker receives bounded native facts in turns and sleeps until a fact, channel publication, or deadline wakes it. Application code awaits those facts through the PocketJS worker SDK: `device.run(script)` and `settle(request)` resolve with a completion, `sleep` and `io.timer` with a deadline, `observe()` loops over stream and channel records; every Promise settles inside a turn, in fact order. There is no execution time limit; a turn runs until it returns, and the CPU share is off because the UI task runs on core 0 and the worker on core 1. The manifest declares the worker under `domains.worker`; UI code reads its channels through `@pocketjs/framework/worker/channels`. QuickJS allocations use internal RAM unless the composition sets `heap_caps`; the task stack likewise with `task_stack_caps`. No JS value crosses the UI/worker boundary. Console calls copy at most four 1023-byte records into fixed native storage per turn. Excess records are counted and long records truncated. The runner drains that batch after JS execution has returned, outside the turn. A blocked console delays the next worker turn; native queues retain their declared bounds.

`pocket_http_esp` provides the `http` node: `io.http` requests over `esp_http_client`, made by the device itself. `programs/m5stack-tab5` gives every app's worker domain that node; see [HTTP](#http).

## Source checkout and builds

The worker core, runner and `tools/build-worker.ts` come from PocketJS `engine/worker`, which reached pocketjs `main` through pocket-nexus/pocketjs#623; the await-based SDK and the removal of the turn time limits come from pocket-nexus/pocketjs#626. The pinned `third_party/pocketjs` includes them; `POCKETJS_ROOT` selects another checkout for `pocket_worker_esp`.

```sh
bun install --frozen-lockfile
. /absolute/path/to/esp-idf/export.sh
idf.py -C programs/m5stack-tab5 build
```

The runner uses `esp_timer_stop_blocking` and requires ESP-IDF **6.2.x**; the remaining components accept their declared IDF ranges. Only ESP32-P4 was exercised on hardware. QuickJS-NG is pinned to 0.14.0. The runner builds its QuickJS dependency with `-fno-strict-aliasing`, matching the existing guest component.

## HTTP

`programs/m5stack-tab5` boots a worker domain for an app whose package carries one (section 8 of the variant it admitted) and gives it a single `http` node. The worker's QuickJS heap (384 KiB cap) and its 24 KiB task stack are in PSRAM (`heap_caps`, `task_stack_caps`), because that program's render strips take the largest internal RAM block; the worker runs on core 1 at priority 3, below the render lane at 5. Its channels latch in the runner's `before_tick`, before each frame. Stopping an app stops the runner, detaches the bridge, stops the worker and then destroys the realm. A worker fault returns an app to Pocket Home with the failure shown; Pocket Home itself keeps running without its worker's channels.

`pocket_http_esp` holds two requests at once, each on its own task (12 KB PSRAM stack, priority 2). The descriptor is the JSON of `io.http(name).open(...)`, at most 1024 bytes, with the limits of the 3DS provider: the methods `GET HEAD POST PUT PATCH DELETE OPTIONS`, `timeoutMs` 1..120000 (30000) and `maxBytes` 0..262144 (262144). A request emits one headers fact `{"status","url","headers"}` within 1024 bytes, data facts of at most 1024 bytes with their offsets, and an end fact. When the worker's eight event slots are full the request task stops reading the socket, so TCP holds the sender; the body is never buffered whole. TLS uses the ESP certificate bundle; the SHA and AES accelerators take a TX and an RX channel of the AXI GDMA, whose three pairs the display's one `async_memcpy` copier (strips and retained moves share it) and the card's SPI bus leave them; mbedTLS takes its buffers from PSRAM and, with `CONFIG_MBEDTLS_HAVE_TIME_DATE`, checks certificate dates against the SNTP clock, so an `https://` request waits for the first synchronization within its timeout. Redirects (at most three) are followed natively, with the companion's method rewriting and credential dropping.

A failure emits an error fact, `code: message` with the companion's portable codes, before the end fact. The end status is `failed` when nothing was sent or the body failed after the head, `unknown` when the request was sent and no head arrived or a cancel came after sending, and `cancelled` for a cancel before sending. Waits for the head and the body are 500 ms reads, so a cancel ends them within that time; a connect or TLS handshake runs until it completes or the deadline passes. `apps/worker-http` exercises the node on the device.

## Validation

```sh
./tools/test-worker-io.sh
bun run format:check
```

The host check runs the HTTP descriptor parser, URL resolution and headers record (`pocket_http_esp/src/record.c`) with AddressSanitizer and UndefinedBehaviorSanitizer. Keep raw logs, firmware hashes and source-diff receipts of device runs under `.pocket-build/validation/worker/<run>/`; do not commit per-run evidence.

The worker's QuickJS heap cap (384 KiB) is not a total worker-memory limit: task stacks, native request/queue buffers, UI mirrors, and allocator headers are additional memory. Measured heap results belong in validation receipts, including startup peak and steady allocation.

The firmware's worker domains have one node, `http`. MQTT, a wall clock in the worker's turn context and sleep-retained worker state are not implemented. Worker-domain failures terminate the session. The shared core's request/fact/channel limits remain part of admission.
