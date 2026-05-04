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

## Lattice-enabled mode

To enable real lattice cryptography, add liboqs as an ESP-IDF component named
`oqs`, then enable `CUBESAT_USE_LIBOQS` in menuconfig.

The firmware adapters call:

- ML-KEM-512 for session establishment
- ML-DSA-44 for signatures

After ML-KEM establishes a shared secret, `packet_crypto_derive_keys` derives
32-byte TX/RX keys with HMAC-SHA256. `packet_crypto_auth_tag` can then generate
packet authentication tags over header-associated data plus payload.

The telemetry packet format has not yet been changed to carry ML-KEM handshake
messages, ML-DSA signatures, or authentication tags. Until that wire protocol is
added, the demo should not be described as encrypted or lattice-authenticated.
