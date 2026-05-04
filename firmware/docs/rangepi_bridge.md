# RangePi Ground Bridge

The hardware demo path is:

```text
ESP32 firmware -> SX1262 LoRa packet bytes -> RangePi serial -> Python groundstation
```

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
