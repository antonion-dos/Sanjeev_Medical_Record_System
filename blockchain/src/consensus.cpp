#include "consensus.hpp"
#include "crypto.hpp"
#include <iostream>

namespace Sanjeev {

void PoAConsensus::register_authority(const Address& address, const std::string& name, const std::string& public_key_pem) {
    AuthorityNode node;
    node.address = address;
    node.name = name;
    node.public_key_pem = public_key_pem;
    node.is_active = true;
    authorities_[address] = node;
}

void PoAConsensus::revoke_authority(const Address& address) {
    auto it = authorities_.find(address);
    if (it != authorities_.end()) {
        it->second.is_active = false;
    }
}

bool PoAConsensus::is_authorized(const Address& address) const {
    auto it = authorities_.find(address);
    if (it != authorities_.end()) {
        return it->second.is_active;
    }
    return false;
}

const AuthorityNode* PoAConsensus::get_authority(const Address& address) const {
    auto it = authorities_.find(address);
    if (it != authorities_.end()) {
        return &(it->second);
    }
    return nullptr;
}

std::vector<AuthorityNode> PoAConsensus::get_all_authorities() const {
    std::vector<AuthorityNode> list;
    for (const auto& pair : authorities_) {
        if (pair.second.is_active) {
            list.push_back(pair.second);
        }
    }
    return list;
}

bool PoAConsensus::sign_block(Block& block, const std::string& private_key_pem, const Address& authority_address, const std::string& authority_name) const {
    block.header.authority_address = authority_address;
    block.header.authority_name = authority_name;
    block.header.merkle_root = block.compute_merkle_root();

    Hash256 header_hash = block.header.calculate_hash();
    try {
        block.header.authority_signature = Crypto::sign_bytes(private_key_pem, header_hash.data(), header_hash.size());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "PoA Error: failed to sign block: " << e.what() << std::endl;
        return false;
    }
}

bool PoAConsensus::validate_block(const Block& block, const Block* prev_block) const {
    // 1. Check authority credentials
    const AuthorityNode* auth = get_authority(block.header.authority_address);
    if (!auth || !auth->is_active) {
        std::cerr << "PoA Error: Authority address " << address_to_hex(block.header.authority_address) << " is not authorized!" << std::endl;
        return false;
    }

    // 2. Verify digital signature over the block header
    Hash256 header_hash = block.header.calculate_hash();
    if (!Crypto::verify_bytes(auth->public_key_pem, header_hash.data(), header_hash.size(), block.header.authority_signature)) {
        std::cerr << "PoA Error: Block signature verification failed for authority " << auth->name << std::endl;
        return false;
    }

    // 3. Verify Merkle root matches transactions
    if (block.header.merkle_root != block.compute_merkle_root()) {
        std::cerr << "PoA Error: Merkle root mismatch in block #" << block.header.index << std::endl;
        return false;
    }

    // 4. Verify blockchain continuity
    if (prev_block != nullptr) {
        if (block.header.index != prev_block->header.index + 1) {
            std::cerr << "PoA Error: Non-sequential block index: expected "
                      << (prev_block->header.index + 1) << ", got " << block.header.index << std::endl;
            return false;
        }
        if (block.header.prev_hash != prev_block->calculate_hash()) {
            std::cerr << "PoA Error: Prev hash mismatch: expected "
                      << prev_block->get_hash_hex() << ", got " << hash_to_hex(block.header.prev_hash) << std::endl;
            return false;
        }
    }

    return true;
}

} // namespace Sanjeev
