import hashlib
import hmac
import os
import platform
import shutil
import warnings
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from groundstation.models.lattice import (
    LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY,
    LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT,
    LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY,
    LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
    MLDSA44_PUBLIC_KEY_LEN,
    MLDSA44_SIGNATURE_LEN,
    MLKEM512_CIPHERTEXT_LEN,
    MLKEM512_PUBLIC_KEY_LEN,
)

KEM_ALG = "ML-KEM-512"
SIG_ALG = "ML-DSA-44"
TRANSCRIPT_LABEL = b"cubesat-lattice-transcript-v1"
TRANSCRIPT_ROLE_NODE_IDENTITY = 1
TRANSCRIPT_ROLE_GROUND_SESSION = 2
SESSION_XOR_MASK = 0x0C0BE5A7
COMMAND_AUTH_KEY_ID = 1
TX_KEY_LABEL = b"CubeSat LoRa TX key v1"
RX_KEY_LABEL = b"CubeSat LoRa RX key v1"


class PQLatticeError(RuntimeError):
    pass


class PQLatticeUnavailable(PQLatticeError):
    pass


@dataclass
class LatticeResponse:
    message_type: int
    transfer_id: int
    object_bytes: bytes
    packet_session_id: int
    new_session_id: int | None = None


@dataclass
class PQLatticeSession:
    node_name: str
    transfer_id: int
    previous_session_id: int
    session_id: int
    shared_secret: bytes
    command_auth_key: bytes
    telemetry_auth_key: bytes
    node_mldsa_public_key: bytes
    ground_mldsa_public_key: bytes


@dataclass
class _NodeHandshake:
    session_id: int
    mlkem_public_key: bytes | None = None
    mldsa_public_key: bytes | None = None
    signature: bytes | None = None


def _win_oqs_dll_names() -> tuple[str, ...]:
    return ("oqs.dll", "liboqs.dll")


def _ensure_windows_oqs_alias(install_dir: Path) -> None:
    if platform.system() != "Windows":
        return
    bin_dir = install_dir / "bin"
    oqs_dll = bin_dir / "oqs.dll"
    liboqs_dll = bin_dir / "liboqs.dll"
    if not oqs_dll.exists() and liboqs_dll.exists():
        shutil.copyfile(liboqs_dll, oqs_dll)


def _install_dir_has_liboqs(install_dir: Path) -> bool:
    if platform.system() == "Windows":
        return any((install_dir / "bin" / name).exists() for name in _win_oqs_dll_names())
    return (install_dir / "lib" / "liboqs.so").exists() or (install_dir / "lib64" / "liboqs.so").exists()


def _prepare_oqs_environment() -> Path | None:
    explicit = os.environ.get("OQS_INSTALL_PATH")
    install_dir = Path(explicit).expanduser() if explicit else Path.home() / "_oqs"
    if not _install_dir_has_liboqs(install_dir):
        return None

    _ensure_windows_oqs_alias(install_dir)
    os.environ.setdefault("OQS_INSTALL_PATH", str(install_dir))
    return install_dir


def load_oqs() -> Any:
    if _prepare_oqs_environment() is None:
        raise PQLatticeUnavailable(
            "liboqs shared library not found; run groundstation/tools/install_liboqs_windows.ps1"
        )
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            import oqs  # type: ignore
    except BaseException as exc:
        raise PQLatticeUnavailable(f"liboqs-python unavailable: {exc}") from exc

    enabled_kems = set(oqs.get_enabled_kem_mechanisms())
    enabled_sigs = set(oqs.get_enabled_sig_mechanisms())
    if KEM_ALG not in enabled_kems or SIG_ALG not in enabled_sigs:
        raise PQLatticeUnavailable(f"liboqs must enable {KEM_ALG} and {SIG_ALG}")
    return oqs


def build_transcript_digest(
    role: int,
    transfer_id: int,
    session_id: int,
    primary: bytes,
    secondary: bytes = b"",
) -> bytes:
    if len(primary) > 0xFFFF or len(secondary) > 0xFFFF:
        raise ValueError("transcript object too large")
    metadata = bytes([role & 0xFF])
    metadata += (transfer_id & 0xFFFF).to_bytes(2, "big")
    metadata += (session_id & 0xFFFFFFFF).to_bytes(4, "big")
    metadata += len(primary).to_bytes(2, "big")
    metadata += len(secondary).to_bytes(2, "big")
    return hashlib.sha256(TRANSCRIPT_LABEL + metadata + primary + secondary).digest()


def derive_session_id(shared_secret: bytes) -> int:
    if len(shared_secret) < 4:
        raise ValueError("shared secret too short")
    session_id = int.from_bytes(shared_secret[:4], "big") ^ SESSION_XOR_MASK
    return session_id or SESSION_XOR_MASK


def derive_packet_key(shared_secret: bytes, label: bytes) -> bytes:
    return hmac.new(shared_secret, label, hashlib.sha256).digest()


class PQLatticeGround:
    def __init__(self) -> None:
        self.oqs = load_oqs()
        self._ground_sig = self.oqs.Signature(SIG_ALG)
        self.ground_mldsa_public_key = self._ground_sig.generate_keypair()
        self._handshakes: dict[tuple[str, int], _NodeHandshake] = {}
        self._sessions_by_node: dict[str, PQLatticeSession] = {}

    def close(self) -> None:
        free = getattr(self._ground_sig, "free", None)
        if callable(free):
            free()

    def command_auth_key(self, node_name: str, session_id: int) -> bytes | None:
        session = self._sessions_by_node.get(node_name)
        if session is None or session.session_id != session_id:
            return None
        return session.command_auth_key

    def session_for_node(self, node_name: str) -> PQLatticeSession | None:
        return self._sessions_by_node.get(node_name)

    def session_count(self) -> int:
        return len(self._sessions_by_node)

    def session_summaries(self) -> list[tuple[str, int, int]]:
        return [
            (session.node_name, session.session_id, session.transfer_id)
            for session in self._sessions_by_node.values()
        ]

    def process_node_object(
        self,
        node_name: str,
        message_type: int,
        transfer_id: int,
        packet_session_id: int,
        object_bytes: bytes,
    ) -> list[LatticeResponse]:
        handshake = self._handshakes.setdefault((node_name, transfer_id), _NodeHandshake(packet_session_id))

        if message_type == LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY:
            if len(object_bytes) != MLKEM512_PUBLIC_KEY_LEN:
                raise PQLatticeError("node ML-KEM public key length mismatch")
            handshake.mlkem_public_key = object_bytes
            return []

        if message_type == LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY:
            if len(object_bytes) != MLDSA44_PUBLIC_KEY_LEN:
                raise PQLatticeError("node ML-DSA public key length mismatch")
            handshake.mldsa_public_key = object_bytes
            return []

        if message_type != LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE:
            return []

        if len(object_bytes) != MLDSA44_SIGNATURE_LEN:
            raise PQLatticeError("node ML-DSA signature length mismatch")
        handshake.signature = object_bytes

        if handshake.mlkem_public_key is None or handshake.mldsa_public_key is None:
            raise PQLatticeError("node signature arrived before public keys")

        node_digest = build_transcript_digest(
            TRANSCRIPT_ROLE_NODE_IDENTITY,
            transfer_id,
            handshake.session_id,
            handshake.mlkem_public_key,
            handshake.mldsa_public_key,
        )
        verifier = self.oqs.Signature(SIG_ALG)
        try:
            if not verifier.verify(node_digest, handshake.signature, handshake.mldsa_public_key):
                raise PQLatticeError("node handshake signature rejected")
        finally:
            free = getattr(verifier, "free", None)
            if callable(free):
                free()

        with self.oqs.KeyEncapsulation(KEM_ALG) as kem:
            ciphertext, shared_secret = kem.encap_secret(handshake.mlkem_public_key)

        if len(ciphertext) != MLKEM512_CIPHERTEXT_LEN:
            raise PQLatticeError("ML-KEM ciphertext length mismatch")

        new_session_id = derive_session_id(shared_secret)
        command_auth_key = derive_packet_key(shared_secret, RX_KEY_LABEL)
        telemetry_auth_key = derive_packet_key(shared_secret, TX_KEY_LABEL)
        ground_digest = build_transcript_digest(
            TRANSCRIPT_ROLE_GROUND_SESSION,
            transfer_id,
            new_session_id,
            ciphertext,
        )
        ground_signature = self._ground_sig.sign(ground_digest)
        if len(ground_signature) != MLDSA44_SIGNATURE_LEN:
            raise PQLatticeError("ground ML-DSA signature length mismatch")

        self._sessions_by_node[node_name] = PQLatticeSession(
            node_name=node_name,
            transfer_id=transfer_id,
            previous_session_id=handshake.session_id,
            session_id=new_session_id,
            shared_secret=shared_secret,
            command_auth_key=command_auth_key,
            telemetry_auth_key=telemetry_auth_key,
            node_mldsa_public_key=handshake.mldsa_public_key,
            ground_mldsa_public_key=self.ground_mldsa_public_key,
        )
        self._handshakes.pop((node_name, transfer_id), None)

        return [
            LatticeResponse(
                LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY,
                transfer_id,
                self.ground_mldsa_public_key,
                handshake.session_id,
            ),
            LatticeResponse(
                LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT,
                transfer_id,
                ciphertext,
                handshake.session_id,
                new_session_id,
            ),
            LatticeResponse(
                LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE,
                transfer_id,
                ground_signature,
                new_session_id,
                new_session_id,
            ),
        ]
