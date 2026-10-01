#pragma once

#include "transaction.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace Sanjeev {

struct BlockHeader {
    uint32_t version = 1;
    uint64_t index = 0;
    uint64_t timestamp = 0;
    Hash256 prev_hash{};
    Hash256 merkle_root{};
    Address authority_address{};
    std::string authority_name;
    std::vector<uint8_t> authority_signature;

    void serialize_unsigned(BinaryWriter& w) const;
    void serialize(BinaryWriter& w) const;
    static BlockHeader deserialize(BinaryReader& r);

    Hash256 calculate_hash() const;
    std::string get_hash_hex() const { return hash_to_hex(calculate_hash()); }
};

struct Block {
    BlockHeader header;
    std::vector<Transaction> transactions;

    Hash256 calculate_hash() const { return header.calculate_hash(); }
    std::string get_hash_hex() const { return header.get_hash_hex(); }
    Hash256 compute_merkle_root() const;

    void serialize(BinaryWriter& w) const;
    static Block deserialize(BinaryReader& r);
};

} // namespace Sanjeev
