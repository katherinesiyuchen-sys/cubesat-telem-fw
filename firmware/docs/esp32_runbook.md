# CubeSat ESP32 Runbook

## 1. Install ESP-IDF

Install ESP-IDF 6.0.1 with the Espressif Windows installer, then open the
`ESP-IDF PowerShell` shortcut so `idf.py` is on your PATH.

Verify:

```powershell
idf.py --version
```

## 2. Set the target

From the firmware directory:

```powershell
cd C:\Users\alexa\cubeSat_firmware\firmware
idf.py set-target esp32
```

Use `esp32s3` instead if your board is an ESP32-S3.

## 3. Check pins

Open the pin page:

```powershell
idf.py menuconfig
```

Then go to:

```text
CubeSat board pins
```

Default development wiring is:

```text
SX1262 MOSI  -> GPIO23
SX1262 MISO  -> GPIO19
SX1262 SCLK  -> GPIO18
SX1262 CS    -> GPIO5
SX1262 RESET -> GPIO14
SX1262 BUSY  -> GPIO27
SX1262 DIO1  -> GPIO26
GNSS TX      -> ESP32 GPIO16
GNSS RX      -> ESP32 GPIO17
```

Change the menuconfig values if your board uses different pins. The firmware
prints the compiled pin map at boot so you can confirm the exact configuration
in `idf.py monitor`.

Radio settings live in the same menu:

```text
LoRa frequency in Hz
LoRa spreading factor
LoRa bandwidth in Hz
LoRa coding rate register value
LoRa TX power in dBm
```

Both the ESP32 and the RangePi radio must use the same settings.

Wi-Fi backup settings also live in the board menu:

```text
Backup Wi-Fi SSID
Backup Wi-Fi password
Groundstation UDP host IPv4
ESP32 local UDP command port
Groundstation UDP telemetry port
```

The groundstation host must be your PC IPv4 address on the same Wi-Fi network.
The defaults are ESP32 command port `5010` and PC telemetry port `5011`.

## 4. Build, flash, monitor

Find your ESP32 COM port in Device Manager, then run:

```powershell
idf.py build
idf.py -p COM6 flash monitor
```

Replace `COM6` with your ESP32 port.

## 5. First expected output

Before GNSS gets a fix, bench telemetry is enabled by default. You should see:

```text
GNSS fix timeout; using bench telemetry fallback
TX_HEX 010100...
TX telemetry: counter=1 ... source=bench
```

Once GNSS has a valid NMEA fix, the source changes to `gnss`.

## 6. Connect RangePi groundstation

In another terminal, use the RangePi serial port. The deploy scripts are the
least error-prone path on Windows:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\install_groundstation.ps1
powershell -ExecutionPolicy Bypass -File deploy\list_serial_ports.ps1
powershell -ExecutionPolicy Bypass -File deploy\run_rangepi_viewer.ps1 -Port COM5
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_rangepi.ps1 -Port COM5
```

Replace `COM5` with your RangePi port. The direct Python command is:

```powershell
python -m groundstation.ui.mastercontrol_app --rangepi-port COM5 --baud 115200 --no-sim
```

Leave off `--no-sim` if you want fake dashboard traffic and live RangePi packets
together.

Without hardware, run the dashboard against the bridge simulator:

```powershell
python -m groundstation.ui.mastercontrol_app --rangepi-port sim://cubesat --no-sim
```

Then use the dashboard terminal commands such as `ping`, `selftest`, and
`downlink`; the simulator replies with real ACK/telemetry/diagnostic packets
through the same parser path.

## 6.5. Wi-Fi UDP backup path

Enable Wi-Fi credentials and select a transport:

```powershell
idf.py menuconfig
```

Menu locations:

```text
CubeSat board pins -> Backup Wi-Fi SSID
CubeSat board pins -> Backup Wi-Fi password
CubeSat board pins -> Groundstation UDP host IPv4
CubeSat runtime -> Primary packet transport
```

Use `Auto fallback: LoRa primary, Wi-Fi UDP backup` for the normal demo. Use
`Wi-Fi UDP only` when you are testing without the RangePi radio path.

Launch the dashboard UDP listener:

```powershell
powershell -ExecutionPolicy Bypass -File deploy\run_mastercontrol_wifi.ps1 -Esp32Host 192.168.1.42
```

Replace `192.168.1.42` with the ESP32 IP from `idf.py monitor`. In the dashboard
terminal:

```text
transport lora
transport wifi
transport auto
```

The firmware ACKs the transport command on the current link before changing
links, so `transport wifi` should be sent while LoRa is still reachable or while
the UDP path is already configured.

For a lower-level serial bridge, run:

```powershell
python groundstation\tools\rangepi_receiver.py --port COM5 --interactive
```

Interactive bridge commands:

```text
cmd ping
cmd selftest
cmd downlink
tx 010600020001...
raw <device-specific serial command>
```

The bridge sends command packets using:

```text
TX <packet-hex>
```

Your RangePi radio process must treat that line as raw bytes to transmit over
LoRa.

## 7. Production switch

For the GNSS-only milestone, turn off the bench fallback:

```powershell
idf.py menuconfig
```

Then disable:

```text
CubeSat runtime -> Transmit bench telemetry when GNSS has no fix
```

For wiring/debug only, enable:

```text
CubeSat runtime -> Run hardware bring-up diagnostics only
```

That mode loops the self-test path and does not start normal telemetry tasks.

## 8. Lattice security switch

The default build keeps lattice crypto disabled so the firmware builds without a
third-party PQC component:

```text
CubeSat security -> Use liboqs for ML-KEM and ML-DSA = n
```

After adding liboqs as an ESP-IDF component named `oqs`, enable:

```text
CubeSat security -> Use liboqs for ML-KEM and ML-DSA = y
```

For early radio testing, leave command auth optional. Once pairing works and
the groundstation is sending authenticated commands, enable:

```text
CubeSat security -> Require lattice-derived command authentication = y
```

The dashboard/simulator can exercise the lattice handshake packet path now:

```powershell
python -m groundstation.ui.mastercontrol_app --rangepi-port sim://cubesat --no-sim
```

Then run `rotate` in the app terminal. The simulator emits fragmented
`HOPE_PACKET_TYPE_HANDSHAKE` packets for the node ML-KEM and ML-DSA public keys,
which should show up as `RANGEPI LATTICE ... fragment x/y` lines.

## 9. PC-side post-quantum groundstation

The groundstation can run real ML-KEM/ML-DSA on your PC before the ESP32 has a
liboqs component installed:

```powershell
cd C:\Users\alexa\cubeSat_firmware
python -m pip install --user -r groundstation\requirements-pq.txt
powershell -ExecutionPolicy Bypass -File groundstation\tools\install_liboqs_windows.ps1
```

Then start the dashboard as usual. On boot it prints:

```text
PQ lattice backend: PC liboqs ready
```

When firmware lattice mode is later enabled and the ESP32 sends a signed node
handshake, the dashboard verifies the node signature, sends the ground ML-KEM
ciphertext and ML-DSA signature back over RangePi, and starts sending
authenticated command packets for the new session.
