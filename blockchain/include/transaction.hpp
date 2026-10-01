#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Sanjeev {

enum class TxType {
    RECORD_STORE = 1,       // Store encrypted medical record / case file
    TEMPORAL_KEY_GRANT = 2, // Grant time-limited decryption access key
    TEMPORAL_KEY_DELEGATE = 3, // Hospital delegates temporal key to doctor
    TEMPORAL_KEY_REVOKE = 4,   // Patient revokes a temporal key
    AUTHORITY_REGISTER = 5     // Register authorized institutional mining authority
};

std::string tx_type_to_string(TxType type);
TxType string_to_tx_type(const std::string& str);

struct SignatureEntry {
    std::string signer_address;
    std::string public_key_pem;
    std::string signature_hex;
};

struct Transaction {
    std::string tx_id;              // SHA-256 hash of canonical content
    TxType type;
    std::string sender;             // e.g. Patient, Hospital, Authority
    std::string recipient;          // e.g. Hospital, Doctor, or "0x0000..." for public/self
    uint64_t timestamp;             // Creation time (unix epoch)
    uint64_t valid_from;            // Temporal validity start (0 if N/A)
    uint64_t valid_until;           // Temporal validity expiration (0 if N/A)
    
    // For lineage tracking:
    std::string parent_tx_id;       // For delegation or updates: points to parent grant/record
    std::string record_hash;        // Hash of the target document

    // Payload (JSON string containing encrypted doc details, key envelope, metadata)
    std::string payload;

    // Multi-Signatory support (threshold signatures)
    std::vector<SignatureEntry> signatures;
    uint32_t required_signatures = 1; // M-of-N threshold

    // Canonical representation for signing & hashing
    std::string get_signing_data() const;
    std::string calculate_hash() const;

    // Signature verification
    bool add_signature(const std::string& private_key_pem, const std::string& public_key_pem);
    bool verify_signatures() const;

    // Temporal validity check
    bool is_temporally_valid(uint64_t current_time) const;

    // JSON serialization
    std::string to_json() const;
    static Transaction from_json(const std::string& json_str);
};

} // namespace Sanjeev
