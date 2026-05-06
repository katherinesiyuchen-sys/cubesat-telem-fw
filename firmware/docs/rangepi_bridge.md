# RangePi Ground Bridge

The hardware demo path is:

```text
ESP32 firmware -> SX1262 LoRa packet bytes -> RangePi serial -> Python groundstation
```

For the full PCB path, think of the RangePi as a USB LoRa modem:

```text
ESP32 PCB UART/USB console       PC terminal running ESP-IDF monitor
ESP32 PCB SX1262IMLTRT    <RF>   RangePi SX1262 radio
RangePi USB serial              PC Python dashboard
```

The ESP32 and RangePi radios must match these settings:

| Setting | Firmware default | Where to change |
| --- | --- | --- |
| Frequency | `915000000` Hz | `idf.py menuconfig` -> CubeSat board pins -> LoRa frequency |
| Spreading factor | `7` | `CONFIG_CUBESAT_LORA_SPREADING_FACTOR` |
| Bandwidth | `125000` Hz | `CONFIG_CUBESAT_LORA_BANDWIDTH_HZ` |
| Coding rate | `1` / LoRa 4/5 | `CONFIG_CUBESAT_LORA_CODING_RATE` |
| TX power | `14` dBm | `CONFIG_CUBESAT_LORA_TX_POWER_DBM` |

Use the frequency allowed for your hardware region. In the US ISM band, the
default `915000000` Hz is the intended ground-demo value.

The ESP32 transmits the raw HOPE packet over LoRa. The same encoded packet is
also logged on the ESP32 USB console as:

```text
TX_HEX 010100010002123456780000000100000000000C...
```

The RangePi should forward each received LoRa payload to the computer as one
line of hex. These forms are accepted by the Python parser:

```text
010100010002123456780000000100000000000C...
RX: 010100010002123456780000000100000000000C... RSSI=-72
+RCV=1,32,010100010002123456780000000100000000000C...,-70,9
```

Run the command-line receiver:

```powershell
python -m groundstation.tools.rangepi_receiver --port COM5 --baud 115200
```

Run the dashboard against the RangePi:

```powershell
python -m groundstation.ui.mastercontrol_app --rangepi-port COM5 --baud 115200 --no-sim
```

Use `--no-sim` when you only want live hardware packets. Leave it off if you
want fake demo traffic and RangePi packets at the same time.

Inside the dashboard terminal, `rangepi <raw command>` writes a line directly to
the RangePi serial port. The exact command set depends on the RangePi firmware
or radio bridge you are running.

## Deploy launcher flow

From the repo root, install the groundstation environment once:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\install_groundstation.ps1
```

Then list the available serial devices:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\list_serial_ports.ps1
```

Launch the dashboard against your real RangePi COM port:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_rangepi.ps1 -Port COM5
```

If the ESP32 is not transmitting yet, open the RangePi viewer first:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_rangepi_viewer.ps1 -Port COM5
```

The viewer proves the USB serial port opens, shows raw serial bytes or boot
text, animates idle/active link state, and parses `RX: <packet-hex>` frames when
the RangePi bridge starts forwarding LoRa payloads.

For lower-level bring-up, open the RangePi console:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\rangepi_console.ps1 -Port COM5
```

Useful console commands:

```text
cmd ping
cmd selftest
cmd rotate
tx <raw-packet-hex>
raw <device-specific-line>
```

## No-hardware simulator

Before the RangePi radio process is ready, run the dashboard against the in-memory
bridge:

```powershell
python -m groundstation.ui.mastercontrol_app --rangepi-port sim://cubesat --no-sim
```

The simulator accepts the same outgoing line as the real bridge:

```text
TX <packet-hex>
```

It decodes command packets, emits ACK packets, and for `ping`, `downlink`,
`connect`, and `selftest` emits follow-on telemetry or diagnostics. This tests
the dashboard command queue, retry path, replay parser, and ACK handling without
an ESP32 or RangePi attached.

The command-line bridge also supports the simulator:

```powershell
python -m groundstation.tools.rangepi_receiver --port sim://cubesat --interactive
```

Then type:

```text
cmd ping
cmd selftest
```

## Real RangePi bridge contract

The real RangePi process should implement the same text contract:

- From PC to RangePi: `TX <packet-hex>`
- From RangePi to PC: `RX: <packet-hex> RSSI=<dbm> SNR=<db>`

The bytes inside `<packet-hex>` are the raw HOPE packet bytes. The bridge should
not reinterpret fields, change counters, or add framing bytes before LoRa TX.

## Wi-Fi backup transport

LoRa remains the primary flight-like path, but the ESP32 firmware also supports
a Wi-Fi UDP backup carrier for bench tests and close-range recovery. The packet
format does not change:

```text
ESP32 HOPE packet bytes -> UDP datagram -> Python dashboard
Python command bytes -> UDP datagram -> ESP32 local command port
```

Set the ESP32 network values in `idf.py menuconfig`:

```text
CubeSat board pins -> Backup Wi-Fi SSID
CubeSat board pins -> Backup Wi-Fi password
CubeSat board pins -> Groundstation UDP host IPv4
CubeSat runtime -> Primary packet transport
```

Run Master Control in UDP mode:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_wifi.ps1 -Esp32Host 192.168.1.42
```

Then switch links from the in-app terminal:

```text
transport lora
transport wifi
transport auto
```

`transport auto` uses LoRa first and sends over Wi-Fi after repeated LoRa TX
failures. Bluetooth is reserved in the protocol as `transport ble`, but there is
no BLE GATT packet service yet.

## Bring-up checklist

1. Flash the ESP32 with the LoRa pins and radio settings for your PCB.
2. Connect the ESP32 PCB power, GNSS, and SX1262 antenna.
3. Connect the RangePi/SX1262 antenna and USB serial to the PC.
4. Confirm the RangePi bridge prints received LoRa frames as `RX: <hex> ...`.
5. Run `deploy\run_mastercontrol_rangepi.ps1 -Port COMx`.
6. In the dashboard terminal, run `ping`, `selftest`, and then `rotate`.
7. After `rotate`, `pq` should show an authenticated PQ session once the full
   ML-KEM/ML-DSA bridge path is active.

If telemetry appears in the RangePi console but not in the dashboard, the serial
line format is usually the issue. The accepted packet text is one full received
LoRa payload per line, with only hex packet bytes inside the `RX:` field.
