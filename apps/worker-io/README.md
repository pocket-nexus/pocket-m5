# Worker IO example

The UI has three touch pages on a 360 × 640 logical viewport. The worker owns RX8130CE, BMI270 and UART resources. It publishes a 96-byte snapshot every 50 ms and on command status changes, and receives 16-byte commands through a bounded ring. The UI reads a copied snapshot every three frames; it does not run sensor transactions.

- **Motion** displays acceleration, angular rate, sample rate and sample age. The level indicator estimates tilt from acceleration with the screen facing up as zero. Translation affects this estimate; it is not a fused orientation sensor. Samples older than 250 ms are marked stale.
- **Clock** displays the RTC in UTC. Select a date/time field and enter digits with the keypad. `CLR` empties the field and `DEL` removes its last digit. Tap the current clock card to load its value into the draft. Live readings do not overwrite an edited draft. `APPLY UTC TO RTC` submits the displayed calendar; the UI waits for the matching command and verified register readback. The keypad locks during a pending write. An invalid Gregorian date disables Apply.
- **UART** displays transmitted and received byte counts, gaps and mismatches. One test packet is sent each second. Without an echo peer or loopback wire, zero received bytes is expected.

RTC writes occur only after Apply. The driver preserves unrelated control bits, clears the invalid-time flag after matching readback, and reports uncertain writes as failures. See [RX8130CE setting and recovery](drivers/RX8130CE.md). The UI does not have a network or host-clock synchronization source; enter UTC rather than local time.

Build with the worker-enabled Tab5 profile:

```sh
POCKETJS_ROOT=/absolute/path/to/worker-enabled/pocketjs \
  bun tools/app.ts build apps/worker-io/pocket.json \
  --runtime-config programs/tab5/worker.runtime-config.ts
```

The normal firmware uses external UART TX GPIO53/RX GPIO54 with no internal loopback. Connect an appropriate echo peer or loopback wire to test returned bytes. The standalone `tab5-worker` diagnostic enables hardware loopback and does not require a wire. Its echo-only commands do not set the RTC. Invalid RTC flags/calendar fields are reported without inventing a time.

See [worker IO implementation and limits](../../docs/WORKER.md). The BMI270 feature image is from Bosch SensorAPI v2.86.1; [BOSCH-LICENSE.txt](drivers/BOSCH-LICENSE.txt) applies to that image.
