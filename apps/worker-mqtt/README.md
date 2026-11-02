# Worker MQTT

A device check for the worker's `io.mqtt` resource on `programs/m5stack-tab5`
(host `tab5-home`). The worker keeps one session at a time through
`io.mqtt("mqtt")`; the UI sends it JSON commands on the `command` ring and
shows the `status` snapshot and the `log` ring. Each session subscribes to
`pocket-m5/<client id>/#` at QoS 2 after a CONNACK without a session, keeps a
retained `online` under `<base>/status` and leaves an `offline` will there.

When the radio first connects, the app opens the saved broker (`mqtt.json`
in its `data.fs` root; `mqtt://broker.emqx.io:1883` with MQTT 5 before the
first save) and runs the checks. The Broker page picks one of the public
EMQX endpoints (`mqtt`, `mqtts`, `ws`, `wss`) or edits the URL, user name and
password on an on-screen keyboard, and switches between MQTT 3.1.1 and 5;
Connect saves the broker. A TLS broker is verified against the firmware's
certificate bundle. The app has no Wi-Fi page: join a network in Pocket
Shell or Worker HTTP first.

| Check | What it shows |
| --- | --- |
| QoS 0, 1 and 2 loopback | publish, the acknowledgement, and the copy that comes back at the same QoS |
| 8 KiB and 32 KiB staged loopback | a payload above the 1024-byte request written to the staging buffer, and a record split across data facts |
| oversize message dropped | 80 KiB against `maxMessageBytes` 64 KiB: under 3.1.1 the host drops it and counts it lost; under MQTT 5 the broker withholds it |
| retained message | a retained copy on a new subscription, then the empty retained publish that clears it |
| unsubscribe stops delivery | nothing arrives after the UNSUBACK |
| MQTT 5 message properties | content type, response topic, correlation data, payload format and two user properties with the same key survive the round trip |
| MQTT 5 subscription identifier | identifier 42 on a message that matched it |
| MQTT 5 no local | the broker does not return the session's own message |

Results, messages and errors print on the serial console (`mqtt: ...`).
The firmware bundles the app (`POCKETJS_TAB5_BUNDLED_APPS`): Pocket Shell
lists it as "Worker MQTT".

```sh
bun run app build apps/worker-mqtt/pocket.app.json
```
