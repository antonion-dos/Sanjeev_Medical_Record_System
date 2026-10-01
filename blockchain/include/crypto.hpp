#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace Sanjeev {

struct KeyPair {
    std::string private_key_pem;
    std::string public_key_pem;
    std::string address;
};

struct EncryptedData {
    std::string ciphertext_b64;
    std::string iv_b64;
    std::string tag_b64;
    std::string algorithm; // e.g. "AES-256-GCM"
};

class Crypto {
public:
    // Hashing
    static std::string sha256(const std::string& input);
    static std::string sha256_bytes(const std::vector<uint8_t>& input);

    // Encoding helpers
    static std::string to_hex(const uint8_t* data, size_t len);
    static std::string to_hex(const std::string& input);
    static std::vector<uint8_t> from_hex(const std::string& hex);
    static std::string to_base64(const uint8_t* data, size_t len);
    static std::string to_base64(const std::string& input);
    static std::vector<uint8_t> from_base64(const std::string& b64);

    // Key generation & address calculation
    static KeyPair generate_ec_keypair();
    static std::string derive_address(const std::string& public_key_pem);

    // Digital Signatures (ECDSA secp256k1 / prime256v1)
    static std::string sign(const std::string& private_key_pem, const std::string& message);
    static bool verify(const std::string& public_key_pem, const std::string& message, const std::string& signature_hex);

    // Symmetric Encryption (AES-256-GCM for medical records)
    static std::vector<uint8_t> generate_random_bytes(size_t len);
    static EncryptedData encrypt_aes_gcm(const std::string& plaintext, const std::vector<uint8_t>& key);
    static std::string decrypt_aes_gcm(const EncryptedData& data, const std::vector<uint8_t>& key);

    // Timestamp helper
    static uint64_t current_timestamp();
};

} // namespace Sanjeev
