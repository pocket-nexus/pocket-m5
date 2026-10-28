# Tab5 worker diagnostic

This firmware runs two 15-second worker generations beside a separate QuickJS UI diagnostic realm at 50 Hz. It exercises the actual I2C controller with onboard RX8130CE/BMI270, the UART FIFO/interrupt path with internal loopback, and UI command/worker snapshot channels. It logs panel detection but does not render a screen. The test does not enable PORT.A's 5 V rail or write RTC registers.

```sh
. /absolute/path/to/esp-idf/export.sh
idf.py -C programs/tab5-worker build
idf.py -C programs/tab5-worker -p /dev/cu.usbmodemDEVICE -b 1500000 flash monitor
```

Back up existing flash when it needs to be preserved. The test firmware uses its own partition table. Restore the original flash after validation when the board is not dedicated to this program.

Acceptance conditions per generation: no worker fault, about 750 UI turns over 15 seconds, BMI state `ready` with more than 900 valid samples by the 14-second report, at least 14 RTC transfers, UART sent/received counts equal at the report boundaries, zero gaps/mismatches, and channel acknowledgements equal to sent commands with zero channel errors. Both drivers must destroy with status 0 after the second generation. RTC data can be invalid; log raw registers and the rejected flags/calendar fields. A failed device transfer is distinct from an invalid clock.

Record board silicon revision and detected panel, firmware SHA-256, dependency versions, source revisions/diffs, heap peak/steady allocation, and serial output under `.pocket-build/validation/worker/<run>/`. No screenshots or render-rate claims follow from this diagnostic. Build configuration and driver limits are described in [docs/WORKER.md](../../docs/WORKER.md).
