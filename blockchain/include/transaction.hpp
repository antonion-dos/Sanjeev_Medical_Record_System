#pragma once

#include "types.hpp"
#include <string>
#include <vector>

namespace Sanjeev {

struct Transaction {
    uint32_t version = 1;
    TxType type = TxType::BLOB_STORE;
    uint64_t timestamp = 0;
    uint64_t nonce = 0;
    Address sender{};
    uint32_t required_signatures = 1;
    TxPayload payload;
    std::vector<SignatureEntry> signatures;

    // Canonical binary serialization
    void serialize_unsigned(BinaryWriter& w) const;
    void serialize(BinaryWriter& w) const;
    static Transaction deserialize(BinaryReader& r);

    // Cryptographic identifier
    Hash256 calculate_id() const;
    std::string get_id_hex() const { return hash_to_hex(calculate_id()); }

    // Temporal validity check
    bool is_temporally_valid(uint64_t current_time) const;
};

} // namespace Sanjeev
