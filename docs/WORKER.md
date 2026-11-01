# Worker domain

The `pocket_worker_esp` component owns a second QuickJS runtime on a FreeRTOS task. The task runs the shared runner in PocketJS `engine/worker/src/runner.c`: the component supplies the FreeRTOS mutex, task notification, one-shot timer wait and allocator, and the runner holds bootstrap, the step/deadline/wait loop, fault retention and destroy. The worker receives bounded native facts in turns and sleeps until a fact, channel publication, or deadline wakes it. Application code awaits those facts through the PocketJS worker SDK: `device.run(script)` and `settle(request)` resolve with a completion, `sleep` and `io.timer` with a deadline, `observe()` loops over stream and channel records; every Promise settles inside a turn, in fact order. There is no execution time limit; a turn runs until it returns, and the CPU share is off because the UI task runs on core 0 and the worker on core 1. The manifest declares the worker under `domains.worker`; UI code reads its channels through `@pocketjs/framework/worker/channels`. QuickJS allocations use internal RAM unless the composition sets `heap_caps`; the task stack likewise with `task_stack_caps`. No JS value crosses the UI/worker boundary. Console calls copy at most four 1023-byte records into fixed native storage per turn. Excess records are counted and long records truncated. The runner drains that batch after JS execution has returned, outside the turn. A blocked console delays the next worker turn; native queues retain their declared bounds.

`pocket_http_esp` provides the `http` node: `io.http` requests over `esp_http_client`, made by the device itself. `pocket_io_esp` provides `i2c` and `uart` nodes. `programs/m5stack-tab5` gives every app's worker domain an `http` node, the `sys` and `porta` I2C nodes and the `rs485` UART node; see [HTTP](#http) and [I2C and UART](#i2c-and-uart).

## Source checkout and builds

The worker core, runner and `tools/build-worker.ts` come from PocketJS `engine/worker`, which reached pocketjs `main` through pocket-nexus/pocketjs#623; the await-based SDK and the removal of the turn time limits come from pocket-nexus/pocketjs#626. The I2C `receive` step comes from pocket-nexus/pocketjs#639. The pinned `third_party/pocketjs` includes them; `POCKETJS_ROOT` selects another checkout for `pocket_worker_esp`.

```sh
bun install --frozen-lockfile
. /absolute/path/to/esp-idf/export.sh
idf.py -C programs/m5stack-tab5 build
```

The runner uses `esp_timer_stop_blocking` and requires ESP-IDF **6.2.x**; the remaining components accept their declared IDF ranges. Only ESP32-P4 was exercised on hardware. QuickJS-NG is pinned to 0.14.0. The runner builds its QuickJS dependency with `-fno-strict-aliasing`, matching the existing guest component.

## HTTP

`programs/m5stack-tab5` boots a worker domain for an app whose package carries one (section 8 of the variant it admitted, or section 9 as bytecode of the firmware's QuickJS-NG build, evaluated instead of source when its engine-ABI digest matches) and gives it an `http` node. The worker's QuickJS heap (1 MiB cap) and its 24 KiB task stack are in PSRAM (`heap_caps`, `task_stack_caps`), because that program's render strips take the largest internal RAM block; the worker runs on core 1 at priority 3, below the render lane at 5. Its channels latch in the runner's `before_tick`, before each frame. Stopping an app stops the runner, detaches the bridge, stops the worker and then destroys the realm. A worker fault returns an app to Pocket Shell with the failure shown; Pocket Shell itself keeps running without its worker's channels.

`pocket_http_esp` holds two requests at once, each on its own task (12 KB PSRAM stack, priority 2). The descriptor is the JSON of `io.http(name).open(...)`, at most 1024 bytes, with the limits of the 3DS provider: the methods `GET HEAD POST PUT PATCH DELETE OPTIONS`, `timeoutMs` 1..120000 (30000) and `maxBytes` 0..262144 (262144). A request emits one headers fact `{"status","url","headers"}` within 1024 bytes, data facts of at most 1024 bytes with their offsets, and an end fact. When the worker's eight event slots are full the request task stops reading the socket, so TCP holds the sender; the body is never buffered whole. TLS uses the ESP certificate bundle; the SHA and AES accelerators take a TX and an RX channel of the AXI GDMA, whose three pairs the display's one `async_memcpy` copier (strips and retained moves share it) and the card's SPI bus leave them; mbedTLS takes its buffers from PSRAM and, with `CONFIG_MBEDTLS_HAVE_TIME_DATE`, checks certificate dates against the SNTP clock, so an `https://` request waits for the first synchronization within its timeout. Redirects (at most three) are followed natively, with the companion's method rewriting and credential dropping.

A failure emits an error fact, `code: message` with the companion's portable codes, before the end fact. The end status is `failed` when nothing was sent or the body failed after the head, `unknown` when the request was sent and no head arrived or a cancel came after sending, and `cancelled` for a cancel before sending. Waits for the head and the body are 500 ms reads, so a cancel ends them within that time; a connect or TLS handshake runs until it completes or the deadline passes. `apps/worker-http` exercises the node on the device.

## I2C and UART

`pocket_io_esp` creates an I2C node on an IDF bus handle or, without one, on two pins as a software bus (open drain, internal pull-ups, clock stretching up to the script's timeout). A worker opens any 7-bit address from 0x08 to 0x77, eight handles at once. Opcode 1 is a script of at most eight steps: one IDF defined-operation transaction whose steps fit the controller's eight hardware command slots (a write takes two, a register read of one byte five and of more bytes six, a receive of one byte three and of more bytes four, STOP one), so a two-byte register read is one transaction. A receive (step op 3, `receive(layout)` in pocketjs) reads after START and the address with the read bit, without a register byte: the SHT3x answers its 16-bit commands and the RCWL-9620 its trigger on such a read. `[write(0x2c, 0x06), receive([bytes(6)])]` is one SHT3x single-shot measurement with clock stretching in seven slots. The software bus executes the same steps and enforces the same limits. Opcode 2 is an address-only probe (`I2CDevice.probe()`); it runs through the same transaction path, not `i2c_master_probe`, which leaves the IDF ISR's read position from an earlier read that ended at a NACK in place. An executor task per node (priority 4, its stack in PSRAM on the Tab5) runs one request at a time and completes it ok, unknown (a failed script: IDF cannot report which writes took effect) or failed (an unanswered probe).

The UART node fixes a port and its pins; the worker selects the baud rate (at most the node's maximum), data bits, stop bits, parity and RTS/CTS flow control. With `rs485` the port runs in RS485 half-duplex mode and RTS drives the transceiver's driver enable while bytes leave the FIFO. Received bytes stream as data facts; a FIFO overflow, frame or parity error, or break ends the stream with a gap, since the hardware does not count the lost bytes.

On the Tab5 the `sys` node shares the BSP's system bus with the IO expanders, touch controller, codecs, camera, RX8130CE, BMI270 and INA226. The worker's transfers share the PSRAM cache with the renderer: the Tab5 demo held 30 fps with no I2C traffic and 17 fps with the BMI270 read at 50 Hz on its own timer, 23 fps after one 20 Hz tick read the IMU and the battery monitor together.

## Validation

```sh
./tools/test-worker-io.sh
bun run format:check
```

The host check runs the HTTP descriptor parser, URL resolution and headers record (`pocket_http_esp/src/record.c`) with AddressSanitizer and UndefinedBehaviorSanitizer. Keep raw logs, firmware hashes and source-diff receipts of device runs under `.pocket-build/validation/worker/<run>/`; do not commit per-run evidence.

The worker's QuickJS heap cap (1 MiB) is not a total worker-memory limit: task stacks, native request/queue buffers, UI mirrors, and allocator headers are additional memory. The Tab5 demo's worker peaks at 300 KB after loading (its bundle carries the BMI270's 8 KB feature image as hex); under the former 384 KiB cap its collector ran so often that core 1 never idled. Measured heap results belong in validation receipts, including startup peak and steady allocation.

The firmware's worker domains have four nodes: `http`, `sys`, `porta` and `rs485`. MQTT, a wall clock in the worker's turn context and sleep-retained worker state are not implemented. Worker-domain failures terminate the session. The shared core's request/fact/channel limits remain part of admission.
