# CubeSat Firmware and Groundstation

ESP32 CubeSat ground-demo firmware, Python groundstation, RangePi LoRa bridge
tools, Wi-Fi UDP backup transport, replay protection, GNSS telemetry parsing,
and a post-quantum lattice protocol path for ML-KEM/ML-DSA pairing work.

The practical demo goal is:

```text
ESP32 PCB + GNSS + SX1262 -> LoRa RF -> RangePi + SX1262 -> USB serial -> CubeSat Master Control
```

For bench recovery and testing, the same packet protocol can also run over:

```text
ESP32 Wi-Fi UDP <-> CubeSat Master Control
```

## Current Status

- ESP-IDF firmware builds for `esp32` with ESP-IDF 6.0.1.
- SX1262 LoRa packet path is implemented.
- GNSS NMEA parsing and bench fallback telemetry are implemented.
- GNSS v2 telemetry validates NMEA checksums and carries fix age, altitude, HDOP, speed, course, UTC time, and source flags.
- Python dashboard can run with simulator, RangePi serial, or Wi-Fi UDP.
- Revamped browser-based CubeSat Master Control is the default simulator UI.
- The original Tk dashboard remains available with `deploy\run_mastercontrol_sim.ps1 -Classic`.
- Dashboard nodes can be added, paired, saved, reloaded, audited, and exported.
- A hardware bring-up panel checks firmware build output, registry state, links, GNSS quality, command queue, replay guard, and PQ backend readiness.
- Replay filtering rejects stale or duplicate packet counters.
- Command packets, ACKs, diagnostics, telemetry, and lattice handshake fragments use the same HOPE packet framing.
- Wi-Fi UDP backup can be toggled from the app terminal.
- Bluetooth transport is reserved in the command protocol, but a BLE GATT packet service is not implemented yet.
- Full ESP32-side post-quantum crypto requires adding a liboqs ESP-IDF component. The protocol boundary and PC-side support are in place.

## Repository Map

```text
firmware/
  main/                         ESP32 app entry and system init
  components/board/             pinout, board config, Kconfig
  components/drivers/           SX1262, GNSS, sensors, Wi-Fi UDP transport
  components/comms/             HOPE packet, telemetry, command, diagnostic, lattice protocols
  components/security/          replay, session, packet auth, ML-KEM/ML-DSA adapters
  components/storage/           NVS-backed counters, config, logs
  components/tasks/             telemetry/LoRa task, GNSS task, sensor tasks
  docs/                         ESP32, RangePi, and security notes

groundstation/
  ui/mastercontrol_app.py       CubeSat Master Control dashboard
  backend/                      packet parsing, RangePi bridge, event engine, PQ backend
  models/                       telemetry and command models
  tools/                        receiver, viewer, tests, install helpers

deploy/
  esp_build.ps1                activates ESP-IDF and builds firmware
  esp_flash_monitor.ps1        flashes ESP32 and opens monitor
  esp_menuconfig.ps1           opens ESP-IDF pin/runtime configuration
  install_groundstation.ps1     creates Python venv and installs deps
  list_serial_ports.ps1         lists COM ports
  run_mastercontrol_sim.ps1     dashboard with built-in simulator
  run_mastercontrol_web.ps1     browser-based Master Control launcher
  run_mastercontrol_rangepi.ps1 dashboard with RangePi serial bridge
  run_mastercontrol_wifi.ps1    dashboard with ESP32 Wi-Fi UDP backup
  run_rangepi_viewer.ps1        raw RangePi serial/static viewer
  rangepi_console.ps1           lower-level interactive RangePi console
  build_portable.ps1            packages portable groundstation zip
```

## Hardware

Expected demo hardware:

- ESP32 board or PCB
- SX1262 LoRa radio, `SX1262IMLTRT`
- GNSS module that outputs UART NMEA
- RangePi or similar USB-attached LoRa bridge with matching SX1262 radio
- Antennas for both LoRa radios
- Windows PC for ESP-IDF and Python dashboard

Default radio settings:

| Setting | Default |
| --- | --- |
| Frequency | `915000000` Hz |
| Spreading factor | `7` |
| Bandwidth | `125000` Hz |
| Coding rate | `1`, LoRa 4/5 |
| TX power | `14` dBm |

Use a frequency and power level legal for your hardware and region.

## Groundstation Quick Start

From the repo root:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\install_groundstation.ps1
```

List serial devices:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\list_serial_ports.ps1
```

Run the dashboard with no hardware:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_sim.ps1
```

That opens the newer local web UI at `http://127.0.0.1:8765/`. To keep the
server running without opening a browser:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_sim.ps1 -NoBrowser
```

To run the older Tk UI:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_sim.ps1 -Classic
```

Run the RangePi raw viewer:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_rangepi_viewer.ps1 -Port COM5
```

Run the dashboard against a real RangePi serial bridge:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_rangepi.ps1 -Port COM5
```

Run the dashboard against ESP32 Wi-Fi UDP backup:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_wifi.ps1 -Esp32Host 192.168.1.42
```

Replace `COM5` and `192.168.1.42` with your actual RangePi COM port and ESP32
IP address.

## Firmware Quick Start

Open an ESP-IDF PowerShell. If `idf.py` is not on PATH, activate it manually:

```powershell
. C:\esp\v6.0.1\esp-idf\export.ps1
```

The helper scripts do the same activation automatically when ESP-IDF is in the
default installer path:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\esp_menuconfig.ps1 -Target esp32
powershell -ExecutionPolicy Bypass -File deploy\esp_build.ps1 -Target esp32
powershell -ExecutionPolicy Bypass -File deploy\esp_flash_monitor.ps1 -Port COM6 -Target esp32
```

Build and flash:

```powershell
cd C:\Users\alexa\cubeSat_firmware\firmware
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p COM6 flash monitor
```

Replace `COM6` with the ESP32 serial port.

If your board is ESP32-S3, use:

```powershell
idf.py set-target esp32s3
```

## ESP32 Menuconfig

Open:

```powershell
idf.py menuconfig
```

Important menus:

```text
CubeSat board pins
CubeSat runtime
CubeSat security
```

Default development pinout:

| Signal | Default ESP32 GPIO |
| --- | --- |
| SX1262 MOSI | GPIO23 |
| SX1262 MISO | GPIO19 |
| SX1262 SCLK | GPIO18 |
| SX1262 CS/NSS | GPIO5 |
| SX1262 RESET | GPIO14 |
| SX1262 BUSY | GPIO27 |
| SX1262 DIO1 | GPIO26 |
| GNSS TX to ESP32 RX | GPIO16 |
| GNSS RX from ESP32 TX | GPIO17 |
| I2C SDA | GPIO21 |
| I2C SCL | GPIO22 |

Useful board settings:

```text
CubeSat board pins -> CubeSat node ID
CubeSat board pins -> Groundstation node ID
CubeSat board pins -> Demo session ID
CubeSat board pins -> LoRa frequency in Hz
CubeSat board pins -> LoRa spreading factor
CubeSat board pins -> LoRa bandwidth in Hz
CubeSat board pins -> LoRa coding rate register value
CubeSat board pins -> LoRa TX power in dBm
CubeSat board pins -> Backup Wi-Fi SSID
CubeSat board pins -> Backup Wi-Fi password
CubeSat board pins -> Groundstation UDP host IPv4
CubeSat board pins -> ESP32 local UDP command port
CubeSat board pins -> Groundstation UDP telemetry port
```

Useful runtime settings:

```text
CubeSat runtime -> Transmit bench telemetry when GNSS has no fix
CubeSat runtime -> Log encoded packet bytes as TX_HEX
CubeSat runtime -> Run hardware bring-up diagnostics only
CubeSat runtime -> Primary packet transport
CubeSat runtime -> LoRa failures before Wi-Fi fallback
```

Useful security settings:

```text
CubeSat security -> Use liboqs for ML-KEM and ML-DSA
CubeSat security -> Require lattice-derived command authentication
```

For first bring-up, keep bench telemetry enabled and command auth optional.

## GNSS

The firmware expects a UART NMEA GNSS module. It accepts talker variants ending
in:

```text
RMC: $GNRMC, $GPRMC, ...
GGA: $GNGGA, $GPGGA, ...
```

GNSS behavior:

- Validates NMEA checksums when a `*XX` checksum is present.
- Rejects no-fix RMC/GGA sentences.
- Preserves empty NMEA fields while parsing.
- Merges RMC and GGA into one latest fix cache.
- Uses RMC for UTC date/time, speed, and course.
- Uses GGA for fix quality, satellites, HDOP, and altitude.
- Treats cached fixes older than about 5 seconds as stale.
- Emits bench telemetry if GNSS times out and bench fallback is enabled.

The dashboard shows GNSS satellites, fix age, HDOP, altitude, speed, and course.
It raises alerts for stale fixes, high HDOP, or low satellite count.

## Transport Modes

The packet bytes do not change between transports. Only the carrier changes.

| Mode | Meaning |
| --- | --- |
| `lora` | SX1262 LoRa only |
| `wifi` | Wi-Fi UDP only |
| `auto` | LoRa primary, Wi-Fi UDP fallback after repeated LoRa TX failures |
| `ble` | Reserved, not yet a working BLE packet service |

Firmware default is configured in:

```text
CubeSat runtime -> Primary packet transport
```

In CubeSat Master Control, use the terminal:

```text
transport lora
transport wifi
transport auto
transport ble
```

The firmware ACKs a transport command on the current link before switching.
For example, send `transport wifi` while LoRa is still reachable or while the
UDP path is already configured.

## RangePi Bridge

The real radio path is:

```text
ESP32 SX1262 <RF> RangePi SX1262 -> USB serial -> Python dashboard
```

The RangePi radio must match the ESP32 radio settings exactly.

Serial contract:

```text
PC to RangePi: TX <packet-hex>
RangePi to PC: RX: <packet-hex> RSSI=<dbm> SNR=<db>
```

Accepted receive line examples:

```text
010100010002123456780000000100000000000C...
RX: 010100010002123456780000000100000000000C... RSSI=-72 SNR=9
+RCV=1,32,010100010002123456780000000100000000000C...,-70,9
```

Open the viewer:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_rangepi_viewer.ps1 -Port COM5
```

Open the lower-level console:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\rangepi_console.ps1 -Port COM5
```

Console examples:

```text
cmd ping
cmd selftest
cmd downlink
cmd rotate
tx 010600020001...
raw <device-specific serial command>
```

More detail: [firmware/docs/rangepi_bridge.md](firmware/docs/rangepi_bridge.md).

## Wi-Fi UDP Backup

Configure ESP32 Wi-Fi in `idf.py menuconfig`:

```text
CubeSat board pins -> Backup Wi-Fi SSID
CubeSat board pins -> Backup Wi-Fi password
CubeSat board pins -> Groundstation UDP host IPv4
CubeSat board pins -> ESP32 local UDP command port
CubeSat board pins -> Groundstation UDP telemetry port
CubeSat runtime -> Primary packet transport
```

Defaults:

| Endpoint | Default |
| --- | --- |
| ESP32 command UDP port | `5010` |
| PC telemetry UDP port | `5011` |

Launch Master Control UDP mode:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_wifi.ps1 -Esp32Host 192.168.1.42
```

Direct Python equivalent:

```powershell
$env:PYTHONPATH=(Get-Location).Path
.venv-groundstation\Scripts\python.exe -m groundstation.ui.mastercontrol_app --udp-listen 5011 --udp-target-host 192.168.1.42 --udp-target-port 5010 --no-sim
```

## CubeSat Master Control Commands

Open the Operations workspace terminal in the app and type `help`.

Target selection:

| Command | Meaning |
| --- | --- |
| `nodes` or `fleet` | list nodes |
| `use all` | commands target all nodes |
| `use selected` | commands follow selected map/list node |
| `use <node-name>` | target a named node |
| `use <1-5>` | target by list index |
| `target` | show current target |

Link and status:

| Command | Meaning |
| --- | --- |
| `status` | selected node status summary |
| `node` | node role, online state, command state, crypto state |
| `sessions` | session IDs and counters |
| `schedule` | contact windows and reasons |
| `pq` or `lattice` | post-quantum backend/session state |
| `transport lora` | switch firmware and app route to LoRa |
| `transport wifi` | switch firmware and app route to Wi-Fi UDP |
| `transport auto` | LoRa primary with Wi-Fi fallback |
| `rangepi <raw-cmd>` | send raw text to RangePi serial bridge |
| `bringup` | open the hardware bring-up checklist |
| `save` | persist dashboard node registry to `groundstation/state/nodes.json` |
| `export all` | export nodes, command queue, packets, audit log, and summary |

Operational commands:

| Command | Meaning |
| --- | --- |
| `ping` | request telemetry now |
| `selftest` | request diagnostic/self-test report |
| `downlink` | open telemetry/downlink state |
| `connect` | resume/reconnect node |
| `isolate` | quarantine node and pause telemetry |
| `arm` | mark node armed |
| `rotate` | rotate/advertise lattice session identity |
| `replay` | inject a replay packet locally to test rejection |
| `handoff` | force selected active target handoff |
| `pause` | pause or resume dashboard simulation |

Node management and pairing:

| Command | Meaning |
| --- | --- |
| `addnode <name> <role>` | add a simulated/dashboard node |
| `delnode <name>` | remove a node |
| `pair kem` | pair selected node as ML-KEM-ready in dashboard state |
| `pair pin` | pair selected node using PIN-style demo state |
| `pair manual` | pair selected node manually |
| `pairwizard` | open add/pair dialog |

Persistence and export:

| Command | Meaning |
| --- | --- |
| `save` | save the current node registry |
| `export nodes` | write node registry JSON to `groundstation/exports` |
| `export commands` | write command queue CSV |
| `export packets` | write packet log CSV |
| `export audit` | write audit log CSV |
| `export summary` | write mission summary JSON |
| `export all` | write every export type |

Map and environment:

| Command | Meaning |
| --- | --- |
| `track <1-5>` | select/track node by index |
| `google` or `maps` | open selected node in Google Maps |
| `street` | open selected node in Street View |
| `zoomin` | zoom map in |
| `zoomout` | zoom map out |
| `env storm` | simulate degraded contact environment |
| `env clear` | restore nominal environment |
| `env eclipse` | toggle eclipse/power condition |

## Pairing Model

There are two pairing layers:

1. Dashboard/demo pairing, used to manage simulated nodes and UI state.
2. Firmware/groundstation lattice pairing, used for real ML-KEM/ML-DSA session establishment.

Dashboard pairing commands:

```text
addnode TEST-NODE sensor
use TEST-NODE
pair kem
status
```

Firmware handshake flow:

```text
use <real-node>
ping
selftest
rotate
pq
```

What `rotate` does:

- Groundstation sends a command packet with `COMMAND_OPCODE_ROTATE_SESSION`.
- ESP32 ACKs the command.
- ESP32 sends fragmented lattice identity packets:
  - node ML-KEM public key
  - node ML-DSA public key
  - node handshake signature
- Groundstation reassembles fragments.
- With PC liboqs installed, groundstation verifies/signs and sends back:
  - ground ML-DSA public key
  - ML-KEM ciphertext
  - ground handshake signature
- With ESP32 liboqs enabled, firmware decapsulates the ciphertext and installs a new session.

Important: with `CubeSat security -> Use liboqs for ML-KEM and ML-DSA = n`,
the packet format and dashboard flow still work, but ESP32 real keygen/signing
returns `ESP_ERR_NOT_SUPPORTED`.

More detail: [firmware/docs/security.md](firmware/docs/security.md).

## Packet Protocol

All carriers use the same HOPE packet format.

Header:

| Field | Size |
| --- | --- |
| version | 1 byte |
| type | 1 byte |
| src_id | 2 bytes |
| dst_id | 2 bytes |
| session_id | 4 bytes |
| counter | 4 bytes |
| timestamp | 4 bytes |
| payload_len | encoded by packet codec |
| payload | max 128 bytes |

Packet types:

| Type | ID |
| --- | --- |
| Telemetry | `1` |
| Alert | `2` |
| Handshake | `3` |
| ACK | `4` |
| Diagnostic | `5` |
| Command | `6` |

Command opcodes:

| Command | Opcode |
| --- | --- |
| self-test | `1` |
| ping | `2` |
| telemetry now | `3` |
| pause telemetry | `4` |
| resume telemetry | `5` |
| rotate session | `6` |
| open downlink | `7` |
| isolate | `8` |
| connect | `9` |
| arm | `10` |
| set transport | `11` |

Replay protection is strictly increasing counters per session. Counter `0`,
duplicates, and stale counters are rejected.

Telemetry payloads are backward-compatible:

| Payload | Size | Fields |
| --- | --- | --- |
| v1 | 12 bytes | latitude, longitude, temperature, fix type, satellites |
| v2 | 36 bytes | v1 fields plus altitude, HDOP, speed, course, GNSS flags, fix age, UTC time, UTC date |

## Post-Quantum Setup

PC-side optional PQ install:

```powershell
python -m pip install --user -r groundstation\requirements-pq.txt
powershell -ExecutionPolicy Bypass -File groundstation\tools\install_liboqs_windows.ps1
```

Groundstation optional install through deploy script:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\install_groundstation.ps1 -WithPQ
```

ESP32-side full PQ requires an ESP-IDF component named `oqs`, then:

```text
CubeSat security -> Use liboqs for ML-KEM and ML-DSA = y
CubeSat security -> Require lattice-derived command authentication = y
```

Leave command auth optional until pairing works reliably over your live link.

## Build Portable Groundstation

Package a portable zip:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\build_portable.ps1
```

The output is written under `dist/`.

## Tests And Checks

Firmware build:

```powershell
cd C:\Users\alexa\cubeSat_firmware\firmware
. C:\esp\v6.0.1\esp-idf\export.ps1
idf.py build
```

VS Code task equivalents:

```text
Terminal -> Run Task -> ESP-IDF: build firmware
Terminal -> Run Task -> ESP-IDF: menuconfig
Terminal -> Run Task -> ESP-IDF: flash and monitor
Terminal -> Run Task -> ESP-IDF: monitor
Terminal -> Run Task -> ESP-IDF: fullclean
Terminal -> Run Task -> Groundstation: run simulator
Terminal -> Run Task -> Groundstation: list serial ports
```

Python checks from repo root:

```powershell
$env:PYTHONPATH=(Get-Location).Path
.venv-groundstation\Scripts\python.exe -m py_compile groundstation\backend\node_registry.py groundstation\models\command.py groundstation\ui\mastercontrol_app.py groundstation\ui\mastercontrol_web.py groundstation\tools\rangepi_viewer.py
.venv-groundstation\Scripts\python.exe groundstation\tools\test_node_registry.py
.venv-groundstation\Scripts\python.exe groundstation\tools\test_packet_parser.py
.venv-groundstation\Scripts\python.exe groundstation\tools\test_rangepi_simulator.py
.venv-groundstation\Scripts\python.exe groundstation\tools\test_mastercontrol_web.py
.venv-groundstation\Scripts\python.exe groundstation\tools\test_mastercontrol_smoke.py
```

Transport command payload quick check:

```powershell
$env:PYTHONPATH=(Get-Location).Path
@'
from groundstation.models.command import build_command_payload, parse_command_payload, COMMAND_OPCODE_SET_TRANSPORT
payload = build_command_payload(command_id=77, opcode=COMMAND_OPCODE_SET_TRANSPORT, arg="wifi")
req = parse_command_payload(payload)
assert req.opcode == COMMAND_OPCODE_SET_TRANSPORT
assert req.arg == b"wifi"
print("PASS: transport command payload")
'@ | .venv-groundstation\Scripts\python.exe -
```

## Typical Bring-Up Flow

No hardware:

```powershell
deploy\install_groundstation.ps1
deploy\run_mastercontrol_sim.ps1
```

Then in the app:

```text
nodes
use 1
ping
selftest
rotate
pq
replay
```

RangePi plugged in, ESP32 not ready:

```powershell
deploy\list_serial_ports.ps1
deploy\run_rangepi_viewer.ps1 -Port COM5
```

ESP32 and RangePi live:

```powershell
cd firmware
idf.py -p COM6 flash monitor
```

In another terminal:

```powershell
deploy\run_mastercontrol_rangepi.ps1 -Port COM5
```

Then in the app:

```text
use selected
ping
selftest
downlink
rotate
transport auto
```

Wi-Fi backup live:

```powershell
deploy\run_mastercontrol_wifi.ps1 -Esp32Host 192.168.1.42
```

Then in the app:

```text
transport wifi
ping
selftest
transport auto
```

## Troubleshooting

`groundstation` import error:

```powershell
$env:PYTHONPATH=(Get-Location).Path
```

`mastercontrol` is not recognized:

Run the script or Python module, not a bare command:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_sim.ps1
```

or:

```powershell
$env:PYTHONPATH=(Get-Location).Path
.venv-groundstation\Scripts\python.exe -m groundstation.ui.mastercontrol_app
```

No RangePi data:

- Confirm the COM port with `deploy\list_serial_ports.ps1`.
- Close any other app holding the COM port.
- Open `deploy\run_rangepi_viewer.ps1 -Port COMx`.
- Confirm the RangePi bridge prints `RX: <hex>` lines.
- Confirm ESP32 and RangePi radio settings match.
- Check antennas and power.

ESP32 builds but no telemetry:

- Leave bench fallback enabled for desk testing.
- Watch `idf.py monitor` for `TX_HEX`.
- Check GNSS UART pins and baud.
- Check SX1262 BUSY, DIO1, RESET, and CS pins.
- Try `CubeSat runtime -> Run hardware bring-up diagnostics only`.

Wi-Fi UDP not working:

- Confirm ESP32 and PC are on the same network.
- Use the PC IPv4 address in `Groundstation UDP host IPv4`.
- Confirm ESP32 monitor shows Wi-Fi connected.
- Start the app with `deploy\run_mastercontrol_wifi.ps1 -Esp32Host <esp32-ip>`.
- Check firewall rules for UDP ports `5010` and `5011`.

Transport switch does not happen:

- The firmware rejects `transport wifi` if Wi-Fi is not ready.
- The firmware rejects `transport lora` if SX1262 init failed.
- `transport ble` is reserved and will not activate a BLE link yet.
- Send the switch command over the currently working link first.

Replay rejects too much:

- Counters are per session and must strictly increase.
- A reboot should load persisted counters from NVS.
- If you are intentionally replay-testing, use a new session or clear test state.

## Production Hardening Notes

Before treating this as anything beyond a ground demo:

- Pin ground ML-DSA identity during manufacturing or explicit enrollment.
- Package and audit an ESP32-ready liboqs component.
- Add session-confirm packets after ML-KEM decapsulation.
- Add authenticated packet tags to telemetry and commands by default.
- Encrypt sensitive payloads after authentication is stable.
- Add persistent node registry and key storage migration paths.
- Add hardware-in-loop tests for SX1262, GNSS, Wi-Fi UDP, and replay counters.
- Review RF compliance for the selected region and antenna gain.

## Detailed Docs

- [ESP32 runbook](firmware/docs/esp32_runbook.md)
- [RangePi bridge](firmware/docs/rangepi_bridge.md)
- [Security notes](firmware/docs/security.md)
