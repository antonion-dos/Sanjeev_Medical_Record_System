#include "block.hpp"
#include "crypto.hpp"
#include "json_utils.hpp"

#include <sstream>

namespace Sanjeev {

std::string Block::get_header_data() const {
    std::ostringstream ss;
    ss << index << ":"
       << timestamp << ":"
       << prev_hash << ":"
       << merkle_root << ":"
       << authority_address;
    return ss.str();
}

std::string Block::calculate_hash() const {
    return Crypto::sha256(get_header_data());
}

std::string Block::compute_merkle_root() const {
    if (transactions.empty()) {
        return Crypto::sha256("EMPTY_TREE");
    }

    std::vector<std::string> current_level;
    for (const auto& tx : transactions) {
        current_level.push_back(tx.tx_id.empty() ? tx.calculate_hash() : tx.tx_id);
    }

    while (current_level.size() > 1) {
        if (current_level.size() % 2 != 0) {
            current_level.push_back(current_level.back());
        }

        std::vector<std::string> next_level;
        for (size_t i = 0; i < current_level.size(); i += 2) {
            std::string combined = current_level[i] + current_level[i + 1];
            next_level.push_back(Crypto::sha256(combined));
        }
        current_level = next_level;
    }

    return current_level[0];
}

bool Block::sign_block(const std::string& private_key_pem, const std::string& public_key_pem, const std::string& name) {
    merkle_root = compute_merkle_root();
    authority_address = Crypto::derive_address(public_key_pem);
    authority_name = name;
    hash = calculate_hash();
    authority_signature = Crypto::sign(private_key_pem, hash);
    return !authority_signature.empty();
}

bool Block::verify_authority_signature(const std::string& public_key_pem) const {
    std::string derived = Crypto::derive_address(public_key_pem);
    if (derived != authority_address) return false;
    std::string computed_hash = calculate_hash();
    if (computed_hash != hash) return false;
    return Crypto::verify(public_key_pem, computed_hash, authority_signature);
}

std::string Block::to_json() const {
    JsonValue v = JsonValue::object();
    v["index"] = static_cast<int64_t>(index);
    v["timestamp"] = timestamp;
    v["prev_hash"] = prev_hash;
    v["merkle_root"] = merkle_root;
    v["authority_address"] = authority_address;
    v["authority_name"] = authority_name;
    v["authority_signature"] = authority_signature;
    v["hash"] = hash;

    JsonValue txs = JsonValue::array();
    for (const auto& tx : transactions) {
        txs.push_back(JsonValue::parse(tx.to_json()));
    }
    v["transactions"] = txs;

    return v.dump();
}

Block Block::from_json(const std::string& json_str) {
    JsonValue v = JsonValue::parse(json_str);
    Block b;
    b.index = v["index"].as_uint64();
    b.timestamp = v["timestamp"].as_uint64();
    b.prev_hash = v["prev_hash"].as_string();
    b.merkle_root = v["merkle_root"].as_string();
    b.authority_address = v["authority_address"].as_string();
    b.authority_name = v["authority_name"].as_string();
    b.authority_signature = v["authority_signature"].as_string();
    b.hash = v["hash"].as_string();

    const JsonValue& txs = v["transactions"];
    for (const auto& tv : txs.arr_val) {
        b.transactions.push_back(Transaction::from_json(tv.dump()));
    }

    return b;
}

} // namespace Sanjeev
