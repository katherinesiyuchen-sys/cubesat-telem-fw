# CubeSat Security Notes

## Current demo mode

The default firmware build does not perform post-quantum cryptography. It uses
packet counters and replay filtering so the ground demo can reject duplicate or
stale telemetry packets.

With the default `CUBESAT_USE_LIBOQS=n`, these APIs validate their buffers and
return `ESP_ERR_NOT_SUPPORTED`:

- `mlkem_session_generate_keypair`
- `mlkem_session_encapsulate`
- `mlkem_session_decapsulate`
- `mldsa_auth_generate_keypair`
- `mldsa_auth_sign`
- `mldsa_auth_verify`

The lattice packet wire format is still present in demo mode. `rotate` commands
can move handshake fragments through the RangePi simulator and dashboard, but
real key generation, encapsulation, decapsulation, and signatures require
`CUBESAT_USE_LIBOQS=y`.

## Lattice-enabled mode

To enable real lattice cryptography, add liboqs as an ESP-IDF component named
`oqs`, then enable `CUBESAT_USE_LIBOQS` in menuconfig.

The firmware adapters call:

- ML-KEM-512 for session establishment
- ML-DSA-44 for handshake transcript signatures

After ML-KEM establishes a shared secret, `packet_crypto_derive_keys` derives
32-byte TX/RX keys with HMAC-SHA256. `packet_crypto_auth_tag` can then generate
packet authentication tags over header-associated data plus payload.

The LoRa packet type `HOPE_PACKET_TYPE_HANDSHAKE` now carries fragmented lattice
objects. This is necessary because ML-KEM/ML-DSA objects are much larger than the
128-byte CubeSat payload:

- node ML-KEM-512 public key: 800 bytes
- node ML-DSA-44 public key: 1312 bytes
- ground ML-KEM-512 ciphertext: 768 bytes
- ML-DSA-44 signature: 2420 bytes

`lattice_protocol.c` splits these objects into 116-byte fragments and
reassembles them before the security layer consumes them. When the ESP32
receives a complete ground ML-KEM ciphertext and liboqs is enabled,
`lattice_security_accept_mlkem_ciphertext` decapsulates it, derives packet keys,
and installs a new session ID.

The handshake also has explicit signature objects:

- `node-handshake-signature` signs the node ML-KEM public key plus node ML-DSA
  public key transcript.
- `ground-handshake-signature` signs the ground ML-KEM ciphertext transcript.

The signed transcript is a SHA-256 domain-separated digest of the role,
transfer ID, session ID, object lengths, and object bytes. The digest is signed
with ML-DSA-44 through the liboqs adapter. This keeps the LoRa fragments simple
while still binding the large lattice objects to the session they create.

## Command packets

Command packets now reserve explicit authentication fields:

- `auth_key_id` selects the future command-authentication key.
- `auth_tag[16]` carries a truncated HMAC-SHA256 tag from ML-KEM-derived packet
  keys.
- `COMMAND_FLAG_AUTH_PRESENT` marks a command as carrying the tag.

With `CUBESAT_REQUIRE_COMMAND_AUTH=n`, unauthenticated commands are accepted for
radio bring-up, while authenticated commands are verified if the flag is present.
With `CUBESAT_REQUIRE_COMMAND_AUTH=y`, command packets are rejected unless a
lattice session exists and the tag verifies.

TX counters and the last accepted RX counter are persisted in NVS so a reboot
does not reset the replay window.

## Groundstation PQ backend

The Python groundstation can now run the real PC-side half of the handshake with
`liboqs-python` plus a local `liboqs` shared library. Install it on Windows with:

```powershell
python -m pip install --user -r groundstation\requirements-pq.txt
powershell -ExecutionPolicy Bypass -File groundstation\tools\install_liboqs_windows.ps1
```

The installer builds a minimal `liboqs` containing only `ML-KEM-512` and
`ML-DSA-44` into `%USERPROFILE%\_oqs`. The dashboard auto-detects that path.

When a node sends complete `node-mlkem-public-key`, `node-mldsa-public-key`, and
`node-handshake-signature` objects, the groundstation verifies the node ML-DSA
signature, encapsulates an ML-KEM shared secret, sends the ground ML-DSA public
key, ML-KEM ciphertext, and ground ML-DSA signature back through the RangePi
bridge, then uses the derived command key for authenticated command packets.

## Remaining production hardening

This is now a real protocol boundary, but not a finished flight security
system. The next hardening pass should pin the ground ML-DSA public key during
manufacturing/pairing on both the ESP32 and groundstation, package a vetted
ESP-IDF `oqs` component for ESP32 builds, add explicit session-confirm packets
from both sides, and encrypt sensitive payloads after authentication is stable.
