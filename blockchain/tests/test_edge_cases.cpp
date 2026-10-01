#include "blockchain.hpp"
#include "crypto.hpp"
#include "binary_buffer.hpp"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace Sanjeev;

// 1. Edge Case: Tampered Ciphertext, IV, and Authentication Tag
void test_tampered_crypto() {
    std::cout << "[EdgeCase 1] Testing cryptographic tampering detection...\n";

    std::string plaintext = "CONFIDENTIAL MEDICAL LAB RESULT: HIV Negative, Hepatitis B Non-Reactive";
    std::vector<uint8_t> key = Crypto::generate_random_bytes(32);

    EncryptedData enc = Crypto::encrypt_aes_gcm(plaintext, key);

    // Baseline: legitimate decryption passes
    std::string decrypted = Crypto::decrypt_aes_gcm(enc, key);
    assert(decrypted == plaintext);

    // Tamper 1: Modify 1 bit in ciphertext
    EncryptedData tampered_ct = enc;
    std::vector<uint8_t> ct_bytes = Crypto::from_base64(tampered_ct.ciphertext_b64);
    ct_bytes[0] ^= 0x01; // flip 1 bit
    tampered_ct.ciphertext_b64 = Crypto::to_base64(ct_bytes.data(), ct_bytes.size());
    try {
        Crypto::decrypt_aes_gcm(tampered_ct, key);
        assert(false && "Should have thrown on tampered ciphertext!");
    } catch (const std::exception& e) {
        // Expected
    }

    // Tamper 2: Modify 1 bit in authentication tag
    EncryptedData tampered_tag = enc;
    std::vector<uint8_t> tag_bytes = Crypto::from_base64(tampered_tag.tag_b64);
    tag_bytes[0] ^= 0x80;
    tampered_tag.tag_b64 = Crypto::to_base64(tag_bytes.data(), tag_bytes.size());
    try {
        Crypto::decrypt_aes_gcm(tampered_tag, key);
        assert(false && "Should have thrown on tampered auth tag!");
    } catch (const std::exception& e) {
        // Expected
    }

    // Tamper 3: Wrong symmetric key
    std::vector<uint8_t> wrong_key = Crypto::generate_random_bytes(32);
    try {
        Crypto::decrypt_aes_gcm(enc, wrong_key);
        assert(false && "Should have thrown on wrong decryption key!");
    } catch (const std::exception& e) {
        // Expected
    }

    std::cout << "  ✓ AES-256-GCM detects any bit modification in ciphertext, tag, or key.\n";
}

// 2. Edge Case: Tampered Signatures & Malformed Signatures
void test_tampered_signatures() {
    std::cout << "[EdgeCase 2] Testing signature forgery and transaction tampering...\n";

    KeyPair alice = Crypto::generate_ec_keypair();
    KeyPair eve = Crypto::generate_ec_keypair();

    std::string message = "TRANSFER_PERMISSION_TO_DOCTOR";
    std::vector<uint8_t> valid_sig = Crypto::sign_bytes(alice.private_key_pem,
        reinterpret_cast<const uint8_t*>(message.data()), message.size());

    // Valid check
    assert(Crypto::verify_bytes(alice.public_key_pem,
        reinterpret_cast<const uint8_t*>(message.data()), message.size(), valid_sig) == true);

    // Tamper message
    std::string tampered_msg = message + "!";
    assert(Crypto::verify_bytes(alice.public_key_pem,
        reinterpret_cast<const uint8_t*>(tampered_msg.data()), tampered_msg.size(), valid_sig) == false);

    // Imposter public key (Eve's key verifying Alice's signature)
    assert(Crypto::verify_bytes(eve.public_key_pem,
        reinterpret_cast<const uint8_t*>(message.data()), message.size(), valid_sig) == false);

    // Corrupted signature bytes
    std::vector<uint8_t> corrupt_sig = valid_sig;
    corrupt_sig[corrupt_sig.size() / 2] ^= 0xFF;
    assert(Crypto::verify_bytes(alice.public_key_pem,
        reinterpret_cast<const uint8_t*>(message.data()), message.size(), corrupt_sig) == false);

    std::cout << "  ✓ Signature tampering and imposter key checks strictly rejected.\n";
}

// 3. Edge Case: Exact Temporal Boundary Conditions
void test_temporal_boundary_conditions() {
    std::cout << "[EdgeCase 3] Testing exact temporal boundary conditions...\n";

    TemporalAccessToken token;
    token.token_id.fill(0x01);
    token.target_blob_id.fill(0x02);
    token.valid_from = 1000;
    token.valid_until = 2000;
    token.status = 1; // Active

    // Sub-second before start: must fail
    assert(token.is_temporally_valid(999) == false);

    // Exact start: must pass
    assert(token.is_temporally_valid(1000) == true);

    // Midpoint: must pass
    assert(token.is_temporally_valid(1500) == true);

    // Exact expiration: must pass
    assert(token.is_temporally_valid(2000) == true);

    // One tick after expiration: must fail
    assert(token.is_temporally_valid(2001) == false);

    // Far future: must fail
    assert(token.is_temporally_valid(999999999) == false);

    std::cout << "  ✓ Precise boundary conditions verified: [valid_from, valid_until] inclusive.\n";
}

// 4. Edge Case: Early Token Revocation & Imposter Revocation
void test_early_revocation() {
    std::cout << "[EdgeCase 4] Testing early token revocation and unauthorized revocation...\n";

    const std::string db_file = "test_edge_revocation.db";
    std::remove(db_file.c_str());

    Blockchain chain;
    assert(chain.init(db_file) == true);

    KeyPair auth_kp = Crypto::generate_ec_keypair();
    Address auth_addr = Crypto::derive_address_bytes(auth_kp.public_key_pem);
    chain.register_authority(auth_addr, "Authority", auth_kp.public_key_pem);
    chain.create_genesis_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Authority");

    KeyPair alice = Crypto::generate_ec_keypair();
    Address alice_addr = Crypto::derive_address_bytes(alice.public_key_pem);

    KeyPair doctor = Crypto::generate_ec_keypair();
    Address doc_addr = Crypto::derive_address_bytes(doctor.public_key_pem);

    KeyPair eve = Crypto::generate_ec_keypair();
    Address eve_addr = Crypto::derive_address_bytes(eve.public_key_pem);

    // 1. Alice grants 24-hr token
    Hash256 token_id{};
    token_id.fill(0x77);
    TemporalAccessToken t;
    t.token_id = token_id;
    t.target_blob_id.fill(0x10);
    t.grantor_address = alice_addr;
    t.recipient_address = doc_addr;
    t.valid_from = 1000;
    t.valid_until = 5000;
    t.encrypted_symkey = {0x01, 0x02};
    t.status = 1;

    Transaction grant_tx;
    grant_tx.type = TxType::TOKEN_GRANT;
    grant_tx.timestamp = 1000;
    grant_tx.sender = alice_addr;
    grant_tx.payload = TokenGrantPayload{t};
    assert(chain.add_transaction(grant_tx) == true);
    chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Authority");

    // Doctor can access at t=1200
    std::vector<uint8_t> key_out;
    assert(chain.request_decryption(token_id, doc_addr, 1200, key_out) == true);

    // Imposter Eve tries to revoke Alice's token -> MUST BE REJECTED
    Transaction imposter_revoke;
    imposter_revoke.type = TxType::TOKEN_REVOKE;
    imposter_revoke.timestamp = 1300;
    imposter_revoke.sender = eve_addr; // Eve is NOT the grantor
    imposter_revoke.payload = TokenRevokePayload{token_id, "Malicious revocation"};
    assert(chain.add_transaction(imposter_revoke) == false);

    // Alice revokes her own token early
    Transaction legit_revoke;
    legit_revoke.type = TxType::TOKEN_REVOKE;
    legit_revoke.timestamp = 1350;
    legit_revoke.sender = alice_addr;
    legit_revoke.payload = TokenRevokePayload{token_id, "Patient revoking access early"};
    assert(chain.add_transaction(legit_revoke) == true);
    chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Authority");

    // Doctor tries to access again at t=1400 (well within [1000, 5000]) -> MUST FAIL because REVOKED
    std::vector<uint8_t> revoked_attempt;
    assert(chain.request_decryption(token_id, doc_addr, 1400, revoked_attempt) == false);

    chain.get_storage().close();
    std::remove(db_file.c_str());
    std::cout << "  ✓ Early revocation enforced; unauthorized revocation attempt blocked.\n";
}

// 5. Edge Case: Delegation Invariant Constraints
void test_delegation_invariants() {
    std::cout << "[EdgeCase 5] Testing hierarchical delegation constraints...\n";

    const std::string db_file = "test_edge_delegation.db";
    std::remove(db_file.c_str());

    Blockchain chain;
    assert(chain.init(db_file) == true);

    KeyPair auth_kp = Crypto::generate_ec_keypair();
    Address auth_addr = Crypto::derive_address_bytes(auth_kp.public_key_pem);
    chain.register_authority(auth_addr, "Authority", auth_kp.public_key_pem);
    chain.create_genesis_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Authority");

    KeyPair alice = Crypto::generate_ec_keypair();
    Address alice_addr = Crypto::derive_address_bytes(alice.public_key_pem);

    KeyPair hospital = Crypto::generate_ec_keypair();
    Address hosp_addr = Crypto::derive_address_bytes(hospital.public_key_pem);

    KeyPair doctor = Crypto::generate_ec_keypair();
    Address doc_addr = Crypto::derive_address_bytes(doctor.public_key_pem);

    // Root grant: Alice -> Hospital (valid until 2000)
    Hash256 parent_id{};
    parent_id.fill(0xAA);
    TemporalAccessToken parent;
    parent.token_id = parent_id;
    parent.target_blob_id.fill(0x01);
    parent.grantor_address = alice_addr;
    parent.recipient_address = hosp_addr;
    parent.valid_from = 1000;
    parent.valid_until = 2000;
    parent.status = 1;

    Transaction tx1;
    tx1.type = TxType::TOKEN_GRANT;
    tx1.timestamp = 1000;
    tx1.sender = alice_addr;
    tx1.payload = TokenGrantPayload{parent};
    assert(chain.add_transaction(tx1) == true);
    chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Authority");

    // Invariant Violation 1: Child token expiration exceeds parent expiration (e.g. 2500 > 2000)
    TemporalAccessToken bad_child;
    bad_child.token_id.fill(0xBB);
    bad_child.target_blob_id = parent.target_blob_id;
    bad_child.grantor_address = hosp_addr;
    bad_child.recipient_address = doc_addr;
    bad_child.valid_from = 1100;
    bad_child.valid_until = 2500; // EXCEEDS PARENT EXPIRY (2000)
    bad_child.parent_token_id = parent_id;

    Transaction bad_del_tx;
    bad_del_tx.type = TxType::TOKEN_DELEGATE;
    bad_del_tx.timestamp = 1100;
    bad_del_tx.sender = hosp_addr;
    bad_del_tx.payload = TokenDelegatePayload{parent_id, bad_child};
    assert(chain.add_transaction(bad_del_tx) == false); // Must be rejected

    // Invariant Violation 2: Delegation referencing nonexistent parent ID
    Hash256 fake_parent{};
    fake_parent.fill(0xFF);
    bad_child.valid_until = 1500;
    bad_child.parent_token_id = fake_parent;

    Transaction ghost_del_tx;
    ghost_del_tx.type = TxType::TOKEN_DELEGATE;
    ghost_del_tx.timestamp = 1100;
    ghost_del_tx.sender = hosp_addr;
    ghost_del_tx.payload = TokenDelegatePayload{fake_parent, bad_child};
    assert(chain.add_transaction(ghost_del_tx) == false); // Must be rejected

    chain.get_storage().close();
    std::remove(db_file.c_str());
    std::cout << "  ✓ Delegation invariants (expiration bounds, parent existence) verified.\n";
}

// 6. Edge Case: Binary Serialization Bounds & Corrupted Deserialization
void test_binary_deserialization_safety() {
    std::cout << "[EdgeCase 6] Testing binary deserializer bounds safety...\n";

    // Buffer underflow check
    std::vector<uint8_t> truncated = {0x01, 0x02}; // incomplete header
    BinaryReader r(truncated);
    try {
        r.read_uint32();
        assert(false && "Should throw on reading uint32 from 2-byte buffer!");
    } catch (const std::out_of_range&) {
        // Expected
    }

    // String length prefix claims 1000 bytes, but buffer only has 5 bytes
    BinaryWriter w;
    w.write_uint32(1000); // fake length
    w.write_bytes(reinterpret_cast<const uint8_t*>("hello"), 5);

    BinaryReader r_str(w.get_buffer());
    try {
        r_str.read_string();
        assert(false && "Should throw on string size mismatch!");
    } catch (const std::out_of_range&) {
        // Expected
    }

    std::cout << "  ✓ Binary buffer bounds checking strictly prevents buffer overflows.\n";
}

// 7. Edge Case: PoA Block Header Tampering
void test_poa_block_tampering() {
    std::cout << "[EdgeCase 7] Testing PoA block tampering rejection...\n";

    KeyPair auth_kp = Crypto::generate_ec_keypair();
    Address auth_addr = Crypto::derive_address_bytes(auth_kp.public_key_pem);

    PoAConsensus consensus;
    consensus.register_authority(auth_addr, "Ministry", auth_kp.public_key_pem);

    Block b0;
    b0.header.index = 0;
    b0.header.timestamp = 1000;
    consensus.sign_block(b0, auth_kp.private_key_pem, auth_addr, "Ministry");
    assert(consensus.validate_block(b0, nullptr) == true);

    // Tamper 1: Modify block index
    Block tampered_b0 = b0;
    tampered_b0.header.index = 5;
    assert(consensus.validate_block(tampered_b0, nullptr) == false);

    // Tamper 2: Modify timestamp
    Block tampered_time = b0;
    tampered_time.header.timestamp = 9999;
    assert(consensus.validate_block(tampered_time, nullptr) == false);

    // Tamper 3: Modify Merkle root
    Block tampered_merkle = b0;
    tampered_merkle.header.merkle_root.fill(0xEE);
    assert(consensus.validate_block(tampered_merkle, nullptr) == false);

    // Tamper 4: Unregistered authority signature
    KeyPair fake_auth = Crypto::generate_ec_keypair();
    Address fake_addr = Crypto::derive_address_bytes(fake_auth.public_key_pem);
    Block fake_b;
    fake_b.header.index = 1;
    fake_b.header.prev_hash = b0.calculate_hash();
    consensus.sign_block(fake_b, fake_auth.private_key_pem, fake_addr, "Fake Node");
    assert(consensus.validate_block(fake_b, &b0) == false);

    std::cout << "  ✓ PoA header tampering (index, timestamp, Merkle root, signer) strictly rejected.\n";
}

int main() {
    std::cout << "\n===============================================================\n";
    std::cout << "     Sanjeev Blockchain Comprehensive Edge Case Test Suite     \n";
    std::cout << "===============================================================\n\n";

    test_tampered_crypto();
    test_tampered_signatures();
    test_temporal_boundary_conditions();
    test_early_revocation();
    test_delegation_invariants();
    test_binary_deserialization_safety();
    test_poa_block_tampering();

    std::cout << "\n[PASS] All 7 rigorous edge case test suites passed with 100% assertions!\n\n";
    return 0;
}
