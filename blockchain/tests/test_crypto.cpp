#include "crypto.hpp"
#include <iostream>
#include <cassert>

using namespace Sanjeev;

void test_sha256() {
    std::string text = "Hello Sanjeev Blockchain";
    std::string h1 = Crypto::sha256(text);
    std::string h2 = Crypto::sha256(text);
    assert(h1 == h2);
    assert(h1.length() == 64);
    std::cout << "[PASS] SHA-256 deterministic hashing verified\n";
}

void test_aes_gcm() {
    std::string medical_record = "CONFIDENTIAL: Patient Blood Panel 2026. Normal glucose, elevated LDL.";
    std::vector<uint8_t> key = Crypto::generate_random_bytes(32);

    EncryptedData enc = Crypto::encrypt_aes_gcm(medical_record, key);
    assert(!enc.ciphertext_b64.empty());
    assert(!enc.iv_b64.empty());
    assert(!enc.tag_b64.empty());

    std::string decrypted = Crypto::decrypt_aes_gcm(enc, key);
    assert(decrypted == medical_record);

    // Test with wrong key
    std::vector<uint8_t> wrong_key = Crypto::generate_random_bytes(32);
    bool failed = false;
    try {
        Crypto::decrypt_aes_gcm(enc, wrong_key);
    } catch (...) {
        failed = true;
    }
    assert(failed);

    std::cout << "[PASS] AES-256-GCM encryption, decryption, and authentication tag verification passed\n";
}

void test_ecdsa_signatures() {
    KeyPair kp = Crypto::generate_ec_keypair();
    assert(kp.address.substr(0, 2) == "0x");
    assert(kp.address.length() == 42);

    std::string message = "Grant Temporal Key to Hospital Apollo";
    std::string sig = Crypto::sign(kp.private_key_pem, message);
    assert(!sig.empty());

    bool valid = Crypto::verify(kp.public_key_pem, message, sig);
    assert(valid);

    bool invalid = Crypto::verify(kp.public_key_pem, "Tampered Message", sig);
    assert(!invalid);

    KeyPair other_kp = Crypto::generate_ec_keypair();
    bool wrong_key = Crypto::verify(other_kp.public_key_pem, message, sig);
    assert(!wrong_key);

    std::cout << "[PASS] ECDSA keypair generation, address derivation, signing and verification passed\n";
}

int main() {
    std::cout << "--- Running Crypto Tests ---\n";
    test_sha256();
    test_aes_gcm();
    test_ecdsa_signatures();
    std::cout << "All Crypto Tests Succeeded!\n";
    return 0;
}
