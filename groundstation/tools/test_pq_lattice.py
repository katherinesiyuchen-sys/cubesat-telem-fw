from groundstation.backend.pq_lattice import (
    KEM_ALG,
    SIG_ALG,
    LatticeResponse,
    PQLatticeGround,
    PQLatticeUnavailable,
    TRANSCRIPT_ROLE_GROUND_SESSION,
    TRANSCRIPT_ROLE_NODE_IDENTITY,
    build_transcript_digest,
)
from groundstation.models.lattice import (
    LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY,
    LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT,
    LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE,
    LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY,
    LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
)


def _object(responses: list[LatticeResponse], message_type: int) -> bytes:
    for response in responses:
        if response.message_type == message_type:
            return response.object_bytes
    raise AssertionError(f"missing response type {message_type}")


def main() -> None:
    try:
        ground = PQLatticeGround()
    except PQLatticeUnavailable as exc:
        print(f"SKIP: PQ lattice unavailable ({exc})")
        return

    try:
        oqs = ground.oqs
        transfer_id = 0x2244
        old_session_id = 0x12345678

        with oqs.KeyEncapsulation(KEM_ALG) as node_kem:
            node_mlkem_public_key = node_kem.generate_keypair()
            node_sig = oqs.Signature(SIG_ALG)
            try:
                node_mldsa_public_key = node_sig.generate_keypair()
                node_digest = build_transcript_digest(
                    TRANSCRIPT_ROLE_NODE_IDENTITY,
                    transfer_id,
                    old_session_id,
                    node_mlkem_public_key,
                    node_mldsa_public_key,
                )
                node_signature = node_sig.sign(node_digest)
            finally:
                free = getattr(node_sig, "free", None)
                if callable(free):
                    free()

            assert ground.process_node_object(
                "TEST-NODE",
                LATTICE_MSG_NODE_MLKEM_PUBLIC_KEY,
                transfer_id,
                old_session_id,
                node_mlkem_public_key,
            ) == []
            assert ground.process_node_object(
                "TEST-NODE",
                LATTICE_MSG_NODE_MLDSA_PUBLIC_KEY,
                transfer_id,
                old_session_id,
                node_mldsa_public_key,
            ) == []
            responses = ground.process_node_object(
                "TEST-NODE",
                LATTICE_MSG_NODE_HANDSHAKE_SIGNATURE,
                transfer_id,
                old_session_id,
                node_signature,
            )

            seen = {response.message_type for response in responses}
            assert LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY in seen
            assert LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT in seen
            assert LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE in seen

            ciphertext = _object(responses, LATTICE_MSG_GROUND_MLKEM_CIPHERTEXT)
            shared_secret = node_kem.decap_secret(ciphertext)
            session = ground.session_for_node("TEST-NODE")
            assert session is not None
            assert session.shared_secret == shared_secret
            assert len(session.command_auth_key) == 32

            ground_public_key = _object(responses, LATTICE_MSG_GROUND_MLDSA_PUBLIC_KEY)
            ground_signature = _object(responses, LATTICE_MSG_GROUND_HANDSHAKE_SIGNATURE)
            ground_digest = build_transcript_digest(
                TRANSCRIPT_ROLE_GROUND_SESSION,
                transfer_id,
                session.session_id,
                ciphertext,
            )
            verifier = oqs.Signature(SIG_ALG)
            try:
                assert verifier.verify(ground_digest, ground_signature, ground_public_key)
            finally:
                free = getattr(verifier, "free", None)
                if callable(free):
                    free()
    finally:
        ground.close()

    print("PASS: PQ lattice")


if __name__ == "__main__":
    main()
