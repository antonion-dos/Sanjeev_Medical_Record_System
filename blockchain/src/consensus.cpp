#include "consensus.hpp"
#include <iostream>

namespace Sanjeev {

void PoAConsensus::register_authority(const std::string& address, const std::string& name, const std::string& public_key_pem) {
    AuthorityNode node;
    node.address = address;
    node.name = name;
    node.public_key_pem = public_key_pem;
    node.is_active = true;
    authorities_[address] = node;
}

void PoAConsensus::revoke_authority(const std::string& address) {
    auto it = authorities_.find(address);
    if (it != authorities_.end()) {
        it->second.is_active = false;
    }
}

bool PoAConsensus::is_authorized(const std::string& address) const {
    auto it = authorities_.find(address);
    if (it != authorities_.end()) {
        return it->second.is_active;
    }
    return false;
}

const AuthorityNode* PoAConsensus::get_authority(const std::string& address) const {
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

bool PoAConsensus::validate_block_header(const Block& block, const Block* prev_block) const {
    // 1. Check authority credentials
    const AuthorityNode* auth = get_authority(block.authority_address);
    if (!auth || !auth->is_active) {
        std::cerr << "PoA Error: Authority address " << block.authority_address << " is not authorized!" << std::endl;
        return false;
    }

    // 2. Verify digital signature
    if (!block.verify_authority_signature(auth->public_key_pem)) {
        std::cerr << "PoA Error: Block signature verification failed for authority " << auth->name << std::endl;
        return false;
    }

    // 3. Verify Merkle root matches transactions
    if (block.merkle_root != block.compute_merkle_root()) {
        std::cerr << "PoA Error: Merkle root mismatch in block #" << block.index << std::endl;
        return false;
    }

    // 4. If chained, verify linkage
    if (prev_block != nullptr) {
        if (block.index != prev_block->index + 1) {
            std::cerr << "PoA Error: Non-sequential block index: expected " << (prev_block->index + 1) << ", got " << block.index << std::endl;
            return false;
        }
        if (block.prev_hash != prev_block->hash) {
            std::cerr << "PoA Error: Prev hash mismatch: expected " << prev_block->hash << ", got " << block.prev_hash << std::endl;
            return false;
        }
    }

    return true;
}

} // namespace Sanjeev
