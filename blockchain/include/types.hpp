#pragma once

#include "binary_buffer.hpp"
#include <array>
#include <vector>
#include <string>
#include <variant>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Sanjeev {

// 32-byte cryptographic digest (SHA-256)
using Hash256 = std::array<uint8_t, 32>;

// 20-byte address derived from public key
using Address = std::array<uint8_t, 20>;

inline std::string bytes_to_hex(const uint8_t* data, size_t len, bool prefix = true) {
    std::ostringstream oss;
    if (prefix) oss << "0x";
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
}

inline std::string hash_to_hex(const Hash256& hash, bool prefix = true) {
    return bytes_to_hex(hash.data(), hash.size(), prefix);
}

inline std::string address_to_hex(const Address& addr, bool prefix = true) {
    return bytes_to_hex(addr.data(), addr.size(), prefix);
}

inline std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::string clean = hex;
    if (clean.rfind("0x", 0) == 0 || clean.rfind("0X", 0) == 0) {
        clean = clean.substr(2);
    }
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < clean.length(); i += 2) {
        std::string byteStr = clean.substr(i, 2);
        bytes.push_back(static_cast<uint8_t>(std::strtoul(byteStr.c_str(), nullptr, 16)));
    }
    return bytes;
}

inline Hash256 hex_to_hash(const std::string& hex) {
    Hash256 h{};
    auto b = hex_to_bytes(hex);
    size_t copy_len = std::min(h.size(), b.size());
    std::copy_n(b.begin(), copy_len, h.begin());
    return h;
}

inline Address hex_to_address(const std::string& hex) {
    Address a{};
    auto b = hex_to_bytes(hex);
    size_t copy_len = std::min(a.size(), b.size());
    std::copy_n(b.begin(), copy_len, a.begin());
    return a;
}

inline bool is_zero_hash(const Hash256& hash) {
    for (uint8_t b : hash) {
        if (b != 0) return false;
    }
    return true;
}

// Opaque Encrypted Medical Record Blob (schema-agnostic)
struct EncryptedBlob {
    Hash256 blob_id{};            // SHA-256 hash of the ciphertext
    Hash256 previous_blob_id{};   // All-zero if initial; points to parent blob if updated
    Address owner_address{};      // Patient address
    Address updater_address{};    // Address committing this revision
    uint64_t timestamp = 0;       // Epoch timestamp
    std::vector<uint8_t> iv;      // AES-GCM IV (typically 12 bytes)
    std::vector<uint8_t> tag;     // AES-GCM Authentication Tag (typically 16 bytes)
    std::vector<uint8_t> data;    // Ciphertext payload

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(blob_id);
        w.write_fixed_array(previous_blob_id);
        w.write_fixed_array(owner_address);
        w.write_fixed_array(updater_address);
        w.write_uint64(timestamp);
        w.write_var_bytes(iv);
        w.write_var_bytes(tag);
        w.write_var_bytes(data);
    }

    static EncryptedBlob deserialize(BinaryReader& r) {
        EncryptedBlob blob;
        blob.blob_id = r.read_fixed_array<32>();
        blob.previous_blob_id = r.read_fixed_array<32>();
        blob.owner_address = r.read_fixed_array<20>();
        blob.updater_address = r.read_fixed_array<20>();
        blob.timestamp = r.read_uint64();
        blob.iv = r.read_var_bytes();
        blob.tag = r.read_var_bytes();
        blob.data = r.read_var_bytes();
        return blob;
    }
};

// Temporal Access Token for cryptographic capability delegation
struct TemporalAccessToken {
    Hash256 token_id{};                      // Unique token identifier
    Hash256 target_blob_id{};                // Target encrypted blob
    Address grantor_address{};               // Patient or delegating hospital
    Address recipient_address{};             // Hospital or physician
    uint64_t valid_from = 0;                 // Epoch start timestamp
    uint64_t valid_until = 0;                // Epoch expiration timestamp
    Hash256 parent_token_id{};               // All-zero if root grant; non-zero if delegated
    std::vector<uint8_t> encrypted_symkey;   // AES document key encrypted under recipient's public key
    uint8_t status = 1;                      // 1 = Active, 2 = Revoked, 3 = Expired

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(token_id);
        w.write_fixed_array(target_blob_id);
        w.write_fixed_array(grantor_address);
        w.write_fixed_array(recipient_address);
        w.write_uint64(valid_from);
        w.write_uint64(valid_until);
        w.write_fixed_array(parent_token_id);
        w.write_var_bytes(encrypted_symkey);
        w.write_uint8(status);
    }

    static TemporalAccessToken deserialize(BinaryReader& r) {
        TemporalAccessToken t;
        t.token_id = r.read_fixed_array<32>();
        t.target_blob_id = r.read_fixed_array<32>();
        t.grantor_address = r.read_fixed_array<20>();
        t.recipient_address = r.read_fixed_array<20>();
        t.valid_from = r.read_uint64();
        t.valid_until = r.read_uint64();
        t.parent_token_id = r.read_fixed_array<32>();
        t.encrypted_symkey = r.read_var_bytes();
        t.status = r.read_uint8();
        return t;
    }

    bool is_temporally_valid(uint64_t current_time) const {
        return (status == 1) && (current_time >= valid_from) && (current_time <= valid_until);
    }
};

enum class TxType : uint8_t {
    BLOB_STORE       = 0x01,
    BLOB_UPDATE      = 0x02,
    TOKEN_GRANT      = 0x03,
    TOKEN_DELEGATE   = 0x04,
    TOKEN_REVOKE     = 0x05,
    DECRYPTION_AUDIT = 0x06,
    AUTHORITY_SET    = 0x07
};

inline std::string tx_type_to_string(TxType type) {
    switch (type) {
        case TxType::BLOB_STORE:       return "BLOB_STORE";
        case TxType::BLOB_UPDATE:      return "BLOB_UPDATE";
        case TxType::TOKEN_GRANT:      return "TOKEN_GRANT";
        case TxType::TOKEN_DELEGATE:   return "TOKEN_DELEGATE";
        case TxType::TOKEN_REVOKE:     return "TOKEN_REVOKE";
        case TxType::DECRYPTION_AUDIT: return "DECRYPTION_AUDIT";
        case TxType::AUTHORITY_SET:    return "AUTHORITY_SET";
        default:                       return "UNKNOWN";
    }
}

// Payload structs
struct BlobStorePayload {
    EncryptedBlob blob;

    void serialize(BinaryWriter& w) const { blob.serialize(w); }
    static BlobStorePayload deserialize(BinaryReader& r) { return { EncryptedBlob::deserialize(r) }; }
};

struct BlobUpdatePayload {
    Hash256 previous_blob_id{};
    EncryptedBlob new_blob;

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(previous_blob_id);
        new_blob.serialize(w);
    }
    static BlobUpdatePayload deserialize(BinaryReader& r) {
        BlobUpdatePayload p;
        p.previous_blob_id = r.read_fixed_array<32>();
        p.new_blob = EncryptedBlob::deserialize(r);
        return p;
    }
};

struct TokenGrantPayload {
    TemporalAccessToken token;

    void serialize(BinaryWriter& w) const { token.serialize(w); }
    static TokenGrantPayload deserialize(BinaryReader& r) { return { TemporalAccessToken::deserialize(r) }; }
};

struct TokenDelegatePayload {
    Hash256 parent_token_id{};
    TemporalAccessToken child_token;

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(parent_token_id);
        child_token.serialize(w);
    }
    static TokenDelegatePayload deserialize(BinaryReader& r) {
        TokenDelegatePayload p;
        p.parent_token_id = r.read_fixed_array<32>();
        p.child_token = TemporalAccessToken::deserialize(r);
        return p;
    }
};

struct TokenRevokePayload {
    Hash256 target_token_id{};
    std::string reason;

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(target_token_id);
        w.write_string(reason);
    }
    static TokenRevokePayload deserialize(BinaryReader& r) {
        TokenRevokePayload p;
        p.target_token_id = r.read_fixed_array<32>();
        p.reason = r.read_string();
        return p;
    }
};

struct DecryptionAuditPayload {
    Hash256 token_id{};
    Hash256 blob_id{};
    Address accessor_address{};
    uint64_t access_timestamp = 0;

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(token_id);
        w.write_fixed_array(blob_id);
        w.write_fixed_array(accessor_address);
        w.write_uint64(access_timestamp);
    }
    static DecryptionAuditPayload deserialize(BinaryReader& r) {
        DecryptionAuditPayload p;
        p.token_id = r.read_fixed_array<32>();
        p.blob_id = r.read_fixed_array<32>();
        p.accessor_address = r.read_fixed_array<20>();
        p.access_timestamp = r.read_uint64();
        return p;
    }
};

struct AuthoritySetPayload {
    Address authority_address{};
    std::string authority_name;
    std::vector<uint8_t> public_key;

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(authority_address);
        w.write_string(authority_name);
        w.write_var_bytes(public_key);
    }
    static AuthoritySetPayload deserialize(BinaryReader& r) {
        AuthoritySetPayload p;
        p.authority_address = r.read_fixed_array<20>();
        p.authority_name = r.read_string();
        p.public_key = r.read_var_bytes();
        return p;
    }
};

using TxPayload = std::variant<
    BlobStorePayload,
    BlobUpdatePayload,
    TokenGrantPayload,
    TokenDelegatePayload,
    TokenRevokePayload,
    DecryptionAuditPayload,
    AuthoritySetPayload
>;

struct SignatureEntry {
    Address signer_address{};
    std::vector<uint8_t> public_key;
    std::vector<uint8_t> signature; // ECDSA DER encoded bytes

    void serialize(BinaryWriter& w) const {
        w.write_fixed_array(signer_address);
        w.write_var_bytes(public_key);
        w.write_var_bytes(signature);
    }

    static SignatureEntry deserialize(BinaryReader& r) {
        SignatureEntry s;
        s.signer_address = r.read_fixed_array<20>();
        s.public_key = r.read_var_bytes();
        s.signature = r.read_var_bytes();
        return s;
    }
};

} // namespace Sanjeev
