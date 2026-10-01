#include "block.hpp"
#include "crypto.hpp"
#include <vector>

namespace Sanjeev {

void BlockHeader::serialize_unsigned(BinaryWriter& w) const {
    w.write_uint32(version);
    w.write_uint64(index);
    w.write_uint64(timestamp);
    w.write_fixed_array(prev_hash);
    w.write_fixed_array(merkle_root);
    w.write_fixed_array(authority_address);
    w.write_string(authority_name);
}

void BlockHeader::serialize(BinaryWriter& w) const {
    serialize_unsigned(w);
    w.write_var_bytes(authority_signature);
}

BlockHeader BlockHeader::deserialize(BinaryReader& r) {
    BlockHeader h;
    h.version = r.read_uint32();
    h.index = r.read_uint64();
    h.timestamp = r.read_uint64();
    h.prev_hash = r.read_fixed_array<32>();
    h.merkle_root = r.read_fixed_array<32>();
    h.authority_address = r.read_fixed_array<20>();
    h.authority_name = r.read_string();
    h.authority_signature = r.read_var_bytes();
    return h;
}

Hash256 BlockHeader::calculate_hash() const {
    BinaryWriter w;
    serialize_unsigned(w);
    return Crypto::sha256_digest(w.get_buffer());
}

Hash256 Block::compute_merkle_root() const {
    if (transactions.empty()) {
        return Hash256{};
    }

    std::vector<Hash256> current_level;
    current_level.reserve(transactions.size());
    for (const auto& tx : transactions) {
        current_level.push_back(tx.calculate_id());
    }

    while (current_level.size() > 1) {
        if (current_level.size() % 2 != 0) {
            current_level.push_back(current_level.back());
        }

        std::vector<Hash256> next_level;
        next_level.reserve(current_level.size() / 2);

        for (size_t i = 0; i < current_level.size(); i += 2) {
            uint8_t combined[64];
            std::memcpy(combined, current_level[i].data(), 32);
            std::memcpy(combined + 32, current_level[i + 1].data(), 32);
            next_level.push_back(Crypto::sha256_digest(combined, 64));
        }

        current_level = std::move(next_level);
    }

    return current_level[0];
}

void Block::serialize(BinaryWriter& w) const {
    header.serialize(w);
    w.write_uint32(static_cast<uint32_t>(transactions.size()));
    for (const auto& tx : transactions) {
        tx.serialize(w);
    }
}

Block Block::deserialize(BinaryReader& r) {
    Block b;
    b.header = BlockHeader::deserialize(r);
    uint32_t tx_count = r.read_uint32();
    b.transactions.reserve(tx_count);
    for (uint32_t i = 0; i < tx_count; ++i) {
        b.transactions.push_back(Transaction::deserialize(r));
    }
    return b;
}

} // namespace Sanjeev
