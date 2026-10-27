# pocket_io_esp

Firmware factories for PocketJS I2C and UART nodes. The I2C factory borrows an existing IDF bus, copies an address allowlist, and reserves those addresses against other instances. Its executor keeps an accepted script in one IDF defined-operation transaction. The UART factory fixes a port and pin routing; JavaScript can select only the admitted line settings. Both drivers keep bounded storage and retain native users until close completes.

Native code outside these factories must honor the same device ownership. Bus serialization does not protect sensor configuration from a competing owner. ESP-IDF targets are ESP32-P4/S3/C3/C6; hardware validation covers Tab5 ESP32-P4. There is no copied IDF driver source in this component.

See [wire formats, budgets, cancellation and overflow behavior](../../../docs/WORKER.md). The standalone descriptor parser is tested by `tools/test-worker-io.sh`.
