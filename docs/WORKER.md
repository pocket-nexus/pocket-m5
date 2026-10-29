# ESP32 worker IO

The `pocket_worker_esp` component owns a second QuickJS runtime on a FreeRTOS task. The task runs the shared runner in PocketJS `engine/worker/src/runner.c`: the component supplies the FreeRTOS mutex, task notification, one-shot timer wait and allocator, and the runner holds bootstrap, the step/deadline/wait loop, fault retention and destroy. The worker receives bounded native facts in turns and sleeps until a fact, channel publication, or deadline wakes it. Application code awaits those facts through the PocketJS worker SDK: `device.run(script)` and `settle(request)` resolve with a completion, `sleep` and `io.timer` with a deadline, `observe()` loops over stream and channel records; every Promise settles inside a turn, in fact order. There is no execution time limit; a turn runs until it returns, and the CPU share is off because the UI task runs on core 0 and the worker on core 1. The manifest declares the worker under `domains.worker`; UI code reads its channels through `@pocketjs/framework/worker/channels`. QuickJS allocations use internal RAM unless the composition sets `heap_caps`; the task stack likewise with `task_stack_caps`. No JS value crosses the UI/worker boundary. Console calls copy at most four 1023-byte records into fixed native storage per turn. Excess records are counted and long records truncated. The runner drains that batch after JS execution has returned, outside the turn. A blocked console delays the next worker turn; native queues retain their declared bounds.

`pocket_io_esp` provides firmware-owned I2C and UART nodes. JavaScript selects a declared node; it cannot select pins, create a bus, or access an undeclared I2C address. The Tab5 composition grants `i2c0` addresses `0x32` (RX8130CE) and `0x68` (BMI270), and `uart1` on TX GPIO53/RX GPIO54. The BSP's I2C bus uses SDA31/SCL32. The normal firmware does not enable the PORT.A 5 V rail.

`pocket_http_esp` provides the `http` node: `io.http` requests over `esp_http_client`, made by the device itself. `programs/m5stack-tab5` gives every app's worker domain that node; see [HTTP](#http).

## Source checkout and builds

The worker core, runner and `tools/build-worker.ts` come from PocketJS `engine/worker`, which reached pocketjs `main` through pocket-nexus/pocketjs#623; the await-based SDK and the removal of the turn time limits come from pocket-nexus/pocketjs#626. The pinned `third_party/pocketjs` includes them; `POCKETJS_ROOT` still selects another checkout for local integration builds.

```sh
bun install --frozen-lockfile
. /absolute/path/to/esp-idf/export.sh
idf.py -C programs/tab5-worker build
```

The runner uses `esp_timer_stop_blocking` and requires ESP-IDF **6.2.x**; the remaining board components accept their declared IDF ranges. Only ESP32-P4 was exercised on hardware. QuickJS-NG is pinned to 0.14.0. The runner builds its QuickJS dependency with `-fno-strict-aliasing`, matching the existing guest component.

To enable workers in the normal launcher, use a fresh build/configuration with both the CMake dependency switch and Kconfig setting:

```sh
idf.py -C programs/tab5 -DPOCKETJS_ENABLE_WORKER=ON \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.worker.defaults' build
bun tools/app.ts build apps/worker-io/pocket.json \
  --runtime-config programs/tab5/worker.runtime-config.ts
```

For an existing `sdkconfig`, enable `CONFIG_POCKET_APP_HOST_WORKER=y` with `menuconfig`; defaults do not overwrite an existing value. The default launcher profile omits worker capabilities. The worker-enabled Tab5 composition places the 128 KiB UI owner task stack in PSRAM and keeps its 64 KiB UI QuickJS stack limit. The worker task stack and QuickJS allocations remain in internal RAM. The PSRAM UI owner must not execute cache-disabled flash/NVS or deep-sleep operations; this composition uses SDMMC/FAT storage. The worker defaults select USB Serial/JTAG for console output, avoiding the UART0 console transmission delay inside diagnostic turns. The worker profile has a different profile identity, so its installed apps must be rebuilt with the same runtime config. `tools/app.ts install` accepts the same `--runtime-config` and copies the compiled package into an SD card's inbox. A `POCKETJS_ROOT` override can change `programs/tab5/dependencies.lock` to that checkout; retain that resolved lock with validation output and restore the default dependency lock before committing.

The host admits the package before worker startup, selects section 8 by exact target, host ABI, and profile hash, bootstraps the worker, and attaches the UI channel bridge before UI evaluation. Worker turns start after UI evaluation returns, so package parsing does not compete with the first worker turn. Native resources and channel queues keep their bounds during this bootstrap interval. A frame copies channel records before `pocket_runtime_tick`; snapshots persist between publications, and rings expose the records received for that frame. App switching detaches the bridge, joins native IO and the worker, then frees the UI realm and package. A worker fault retains the core until the UI owner detaches and stops it. The normal launcher returns to home on a worker fault.

## IO limits and ownership

I2C accepts at most 8 descriptor steps, **8 hardware command slots**, 256 wire bytes, and a 20 ms timeout in the Tab5 composition at 400 kHz. Command slots, not descriptor count alone, constrain a script: a one-byte register read costs 5 slots, a longer read costs 6, a write costs 2, and the final STOP costs 1. A register-only write is valid. A read must request at least one byte. Admission rejects trailing bytes, unsupported operations, result overflow, timeouts outside the configured range, and scripts above the command/byte limits before reserving a request. A configuration's clock and byte limits must allow its selected timeout; clock stretching and bus contention can still produce a timeout.

One script is one IDF `i2c_master_execute_defined_operations` call, with repeated STARTs and one final STOP. IDF owns the shared bus lock for the whole script, so native touch/expander transactions cannot split it. Separate scripts can interleave. Each driver reserves its allowed `(bus,address)` pairs in a bounded registry, and each address has one active JS handle. **Native code that bypasses this registry must not configure/read the reserved RTC or IMU while the worker owns them.** The Tab5 launcher composition does not create a native IMU driver. Existing `m5stack-tab5` firmware with its native IMU path is a separate program and is not changed to expose the same device to a worker.

The I2C executor copies accepted descriptors into 8 fixed request slots. Cancellation before execution completes as cancelled. Once execution starts, failed or timed-out operations report unknown outcome because IDF does not report the last completed write. Buffers stay alive until execution finishes and completion has returned. Closing waits for outstanding native users.

UART has a 2048-byte IDF RX ring, a 1024-byte TX ring, 256-byte chunks, and a 16-entry native event queue. `write` copies an accepted prefix and returns its size; zero is backpressure. TX credit notifications retry if the worker event queue is full. RX reads stop at each `UART_DATA.size` boundary before consuming subsequent error events. Without RTS/CTS, a full worker queue produces an explicit counted gap. FIFO overflow, missing native data events, frame/parity errors, and break have unknown lost-byte counts; they emit an unknown gap and terminal outcome. Reopen the resource to resume parsing. With configured RTS/CTS pins, a pending RX chunk is retained while hardware flow control limits the sender. The Tab5 example has no RTS/CTS pins and uses 115200 baud, 8N1.

## HTTP

`programs/m5stack-tab5` boots a worker domain for an app whose package carries one (section 8 of the variant it admitted) and gives it a single `http` node. The worker's QuickJS heap (384 KiB cap) and its 24 KiB task stack are in PSRAM (`heap_caps`, `task_stack_caps`), because that program's render strips take the largest internal RAM block; the worker runs on core 1 at priority 3, below the render lane at 5. Its channels latch in the runner's `before_tick`, before each frame. Stopping an app stops the runner, detaches the bridge, stops the worker and then destroys the realm. A worker fault returns an app to Pocket Home with the failure shown; Pocket Home itself keeps running without its worker's channels.

`pocket_http_esp` holds two requests at once, each on its own task (12 KB PSRAM stack, priority 2). The descriptor is the JSON of `io.http(name).open(...)`, at most 1024 bytes, with the limits of the 3DS provider: the methods `GET HEAD POST PUT PATCH DELETE OPTIONS`, `timeoutMs` 1..120000 (30000) and `maxBytes` 0..262144 (262144). A request emits one headers fact `{"status","url","headers"}` within 1024 bytes, data facts of at most 1024 bytes with their offsets, and an end fact. When the worker's eight event slots are full the request task stops reading the socket, so TCP holds the sender; the body is never buffered whole. TLS uses the ESP certificate bundle; the SHA and AES accelerators take a TX and an RX channel of the AXI GDMA, whose three pairs the display's one `async_memcpy` copier (strips and retained moves share it) and the card's SPI bus leave them; mbedTLS takes its buffers from PSRAM and, with `CONFIG_MBEDTLS_HAVE_TIME_DATE`, checks certificate dates against the SNTP clock, so an `https://` request waits for the first synchronization within its timeout. Redirects (at most three) are followed natively, with the companion's method rewriting and credential dropping.

A failure emits an error fact, `code: message` with the companion's portable codes, before the end fact. The end status is `failed` when nothing was sent or the body failed after the head, `unknown` when the request was sent and no head arrived or a cancel came after sending, and `cancelled` for a cancel before sending. Waits for the head and the body are 500 ms reads, so a cancel ends them within that time; a connect or TLS handshake runs until it completes or the deadline passes. `apps/worker-http` exercises the node on the device.

## Sensor behavior

`apps/worker-io/drivers/rx8130ce.ts` polls registers `0x10..0x1e` without writes. It validates BCD, calendar ranges, one-hot weekday, VLF, STOP, and TEST before publishing a time. An invalid read increments the invalid counter and keeps raw bytes for diagnosis. `currentValid` distinguishes a fresh valid reading from the retained `latest` sample. A successful I2C transfer does not establish a valid clock.

The Clock page can submit a UTC calendar through the command ring. **A setting succeeds only after register readback matches.** STOP, calendar write and restart share one seven-command transaction. The driver then checks the result, clears VLF without clearing other event flags, and reads back again. An uncertain write gets at most three STOP recovery attempts and is never reported as success. See [RTC setting and recovery](../apps/worker-io/drivers/RX8130CE.md) for the sequence and forced-shutdown limits. The [test UI](../apps/worker-io/README.md) reads motion, clock and UART snapshots every three UI frames. The worker publishes a timed snapshot every 50 ms and on command status changes. Periodic console reports contain counters, status and RTC integers; sensor floating-point values travel in the binary snapshot to avoid decimal formatting in the worker.

`bmi270.ts` validates chip ID `0x24`, resets the IMU, uploads Bosch's 8192-byte configuration in 128-byte blocks, checks initialization status, configures 100 Hz accelerometer/gyro at ±4 g and ±1000 degrees/s, and reads back the range/ODR registers. INIT_ADDR and INIT_DATA use separate requests with STOP between them. Sampling reads one contiguous register range on a 10 ms timer tick and requires both data-ready flags; a read without them yields no sample, and a tick that passes during a transfer folds into the next tick's `missed` count. Values use m/s² and rad/s in sensor axes; the retained 24-bit sensor counter is not presented as wall-clock time.

The feature image comes from Bosch BMI270 SensorAPI v2.86.1, commit `41129fcfe39c583ee5462d79195741945d51c1fe`. Its BSD-3-Clause notice is retained beside the generated TypeScript asset. The image uses a single hexadecimal literal decoded once to avoid per-element parsing allocations on QuickJS.

Sources: [Tab5 pin map](https://docs.m5stack.com/en/core/Tab5), [RX8130CE application manual ETM50E-10](https://download.epsondevice.com/td/pdf/app/RX8130CE_en.pdf), [Bosch BMI270 SensorAPI](https://github.com/boschsensortec/BMI270_SensorAPI/tree/41129fcfe39c583ee5462d79195741945d51c1fe), [Bosch BMI270 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf).

## Validation

```sh
./tools/test-worker-io.sh
bun run format:check
```

The host checks run the descriptor and package parsers with AddressSanitizer and UndefinedBehaviorSanitizer, including malformed lengths, command/result limits, register-only writes, and exact-target selection. Bun tests cover RTC invalid flags/calendar boundaries, setting readback and partial-write recovery through the SDK scheduler against an I2C register model, UI command layouts and calendar input, BMI layout/scaling/data readiness, and the pinned configuration image checksum.

`programs/tab5-worker` is the hardware diagnostic. It runs two fresh worker generations for 15 seconds each, a separate UI diagnostic realm at 50 Hz, RTC reads, BMI initialization/sampling, UART hardware loopback, and command/snapshot channels. It does not initialize or measure display rendering. See its README for flash commands and acceptance conditions. Keep raw logs, firmware hashes, resolved dependency locks, and source-diff receipts under `.pocket-build/validation/worker/<run>/`; do not commit per-run evidence.

The current example exceeds the PRD's 100 KiB heap target. The composition caps the worker at **384 KiB**; measured heap results belong in validation receipts, including startup peak and steady allocation. Task stacks, native request/queue buffers, UI mirrors, and allocator headers are additional memory. Do not infer a total worker-memory limit from QuickJS's heap cap.

The first version does not implement native repeated I2C sampling, GPIO interrupts, SPI, DMA channels, MQTT, a wall clock in the worker's turn context, sleep-retained worker state, or automatic recovery after unknown UART loss. Worker-domain failures terminate the session. The shared core's request/fact/channel limits remain part of admission.
