#pragma once

#include "transaction.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace Sanjeev {

struct Block {
    uint64_t index = 0;
    uint64_t timestamp = 0;
    std::string prev_hash;
    std::string merkle_root;
    std::vector<Transaction> transactions;

    // Proof-of-Authority (PoA) validation fields
    std::string authority_address;
    std::string authority_name;
    std::string authority_signature;
    std::string hash;

    std::string get_header_data() const;
    std::string calculate_hash() const;
    std::string compute_merkle_root() const;

    bool sign_block(const std::string& private_key_pem, const std::string& public_key_pem, const std::string& name);
    bool verify_authority_signature(const std::string& public_key_pem) const;

    std::string to_json() const;
    static Block from_json(const std::string& json_str);
};

} // namespace Sanjeev
