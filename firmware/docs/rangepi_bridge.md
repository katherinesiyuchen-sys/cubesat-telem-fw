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
