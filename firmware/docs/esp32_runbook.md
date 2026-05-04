# CubeSat ESP32 Runbook

## 1. Install ESP-IDF

Install ESP-IDF 5.x with the Espressif Windows installer, then open the
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

In another terminal, use the RangePi serial port:

```powershell
python -m groundstation.ui.mastercontrol_app --rangepi-port COM5 --baud 115200 --no-sim
```

Replace `COM5` with your RangePi port. Leave off `--no-sim` if you want fake
dashboard traffic and live RangePi packets together.

## 7. Production switch

For the GNSS-only milestone, turn off the bench fallback:

```powershell
idf.py menuconfig
```

Then disable:

```text
CubeSat runtime -> Transmit bench telemetry when GNSS has no fix
```
