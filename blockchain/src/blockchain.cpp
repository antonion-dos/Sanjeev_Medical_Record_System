#include "blockchain.hpp"
#include "crypto.hpp"
#include "json_utils.hpp"
#include <iostream>

namespace Sanjeev {

Blockchain::Blockchain() {
}

void Blockchain::register_authority(const std::string& address, const std::string& name, const std::string& public_key_pem) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    consensus_.register_authority(address, name, public_key_pem);
}

Block Blockchain::create_genesis_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    if (!chain_.empty()) {
        return chain_[0];
    }

    std::string auth_addr = Crypto::derive_address(authority_pubkey);
    consensus_.register_authority(auth_addr, authority_name, authority_pubkey);

    Block genesis;
    genesis.index = 0;
    genesis.timestamp = Crypto::current_timestamp();
    genesis.prev_hash = "0000000000000000000000000000000000000000000000000000000000000000";

    // Genesis transaction: system authority registration
    Transaction tx;
    tx.type = TxType::AUTHORITY_REGISTER;
    tx.sender = auth_addr;
    tx.recipient = auth_addr;
    tx.timestamp = genesis.timestamp;
    tx.payload = "{\"genesis\":\"Sanjeev Decentralized Health Record Ledger Initialized\"}";
    tx.required_signatures = 1;
    tx.tx_id = tx.calculate_hash();
    tx.add_signature(authority_privkey, authority_pubkey);

    genesis.transactions.push_back(tx);
    genesis.sign_block(authority_privkey, authority_pubkey, authority_name);

    chain_.push_back(genesis);
    tx_index_[tx.tx_id] = tx;

    return genesis;
}

bool Blockchain::add_transaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(chain_mutex_);

    // 1. Verify signatures
    if (!tx.verify_signatures()) {
        std::cerr << "Transaction signature verification failed for tx: " << tx.tx_id << std::endl;
        return false;
    }

    // 2. Enforce temporal key delegation constraints
    if (tx.type == TxType::TEMPORAL_KEY_DELEGATE) {
        auto it = tx_index_.find(tx.parent_tx_id);
        if (it == tx_index_.end()) {
            std::cerr << "Delegation rejected: Parent key " << tx.parent_tx_id << " not found" << std::endl;
            return false;
        }

        const Transaction& parent = it->second;
        if (revoked_keys_.find(parent.tx_id) != revoked_keys_.end()) {
            std::cerr << "Delegation rejected: Parent key " << parent.tx_id << " is revoked" << std::endl;
            return false;
        }

        // Sub-delegated key cannot exceed the parent key's validity expiration
        if (tx.valid_until > parent.valid_until) {
            std::cerr << "Delegation rejected: Validity expiration exceeds parent key expiration" << std::endl;
            return false;
        }

        // The delegator must be the authorized recipient of the parent key
        if (tx.sender != parent.recipient) {
            std::cerr << "Delegation rejected: Sender " << tx.sender << " is not authorized recipient of parent key" << std::endl;
            return false;
        }
    }

    // 3. Handle revocation
    if (tx.type == TxType::TEMPORAL_KEY_REVOKE) {
        revoked_keys_[tx.parent_tx_id] = true;
    }

    mempool_.push_back(tx);
    return true;
}

Block Blockchain::mine_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name) {
    std::lock_guard<std::mutex> lock(chain_mutex_);

    if (chain_.empty()) {
        throw std::runtime_error("Cannot mine block: Genesis block does not exist");
    }

    std::string auth_addr = Crypto::derive_address(authority_pubkey);
    if (!consensus_.is_authorized(auth_addr)) {
        throw std::runtime_error("Mining rejected: Signer is not an authorized central institution: " + auth_addr);
    }

    const Block& prev_block = chain_.back();

    Block new_block;
    new_block.index = prev_block.index + 1;
    new_block.timestamp = Crypto::current_timestamp();
    new_block.prev_hash = prev_block.hash;
    new_block.transactions = mempool_;

    new_block.sign_block(authority_privkey, authority_pubkey, authority_name);

    if (!consensus_.validate_block_header(new_block, &prev_block)) {
        throw std::runtime_error("Mined block failed PoA validation");
    }

    for (const auto& tx : new_block.transactions) {
        tx_index_[tx.tx_id] = tx;
    }

    chain_.push_back(new_block);
    mempool_.clear();

    return new_block;
}

bool Blockchain::is_chain_valid() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    if (chain_.empty()) return false;

    for (size_t i = 0; i < chain_.size(); ++i) {
        const Block& current = chain_[i];
        const Block* prev = (i == 0) ? nullptr : &chain_[i - 1];

        if (!consensus_.validate_block_header(current, prev)) {
            return false;
        }

        // Verify all transactions in the block
        for (const auto& tx : current.transactions) {
            if (!tx.verify_signatures()) {
                return false;
            }
        }
    }
    return true;
}

size_t Blockchain::get_chain_length() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return chain_.size();
}

const std::vector<Block>& Blockchain::get_blocks() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return chain_;
}

const std::vector<Transaction>& Blockchain::get_mempool() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return mempool_;
}

std::vector<Transaction> Blockchain::get_all_records() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    std::vector<Transaction> list;
    for (const auto& pair : tx_index_) {
        if (pair.second.type == TxType::RECORD_STORE) {
            list.push_back(pair.second);
        }
    }
    return list;
}

std::vector<Transaction> Blockchain::get_records_for_patient(const std::string& patient_address) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    std::vector<Transaction> list;
    for (const auto& pair : tx_index_) {
        if (pair.second.type == TxType::RECORD_STORE && pair.second.sender == patient_address) {
            list.push_back(pair.second);
        }
    }
    return list;
}

std::vector<Transaction> Blockchain::get_active_temporal_keys(uint64_t current_time) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    if (current_time == 0) current_time = Crypto::current_timestamp();

    std::vector<Transaction> active;
    for (const auto& pair : tx_index_) {
        const auto& tx = pair.second;
        if (tx.type == TxType::TEMPORAL_KEY_GRANT || tx.type == TxType::TEMPORAL_KEY_DELEGATE) {
            if (revoked_keys_.find(tx.tx_id) == revoked_keys_.end() && tx.is_temporally_valid(current_time)) {
                active.push_back(tx);
            }
        }
    }
    return active;
}

std::vector<Transaction> Blockchain::get_temporal_keys_for_holder(const std::string& holder_address) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    std::vector<Transaction> list;
    for (const auto& pair : tx_index_) {
        const auto& tx = pair.second;
        if ((tx.type == TxType::TEMPORAL_KEY_GRANT || tx.type == TxType::TEMPORAL_KEY_DELEGATE) &&
            (tx.recipient == holder_address || tx.sender == holder_address)) {
            list.push_back(tx);
        }
    }
    return list;
}

bool Blockchain::is_access_authorized(const std::string& record_hash, const std::string& accessor_address, uint64_t current_time) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    if (current_time == 0) current_time = Crypto::current_timestamp();

    // 1. Direct record owner check
    for (const auto& pair : tx_index_) {
        const auto& tx = pair.second;
        if (tx.type == TxType::RECORD_STORE && tx.record_hash == record_hash) {
            if (tx.sender == accessor_address) {
                return true;
            }
        }
    }

    // 2. Active temporal key or delegation check
    for (const auto& pair : tx_index_) {
        const auto& tx = pair.second;
        if (tx.type == TxType::TEMPORAL_KEY_GRANT || tx.type == TxType::TEMPORAL_KEY_DELEGATE) {
            if (tx.record_hash == record_hash && tx.recipient == accessor_address) {
                if (revoked_keys_.find(tx.tx_id) == revoked_keys_.end() && tx.is_temporally_valid(current_time)) {
                    return true;
                }
            }
        }
    }

    return false;
}

LineageNode Blockchain::trace_lineage(const std::string& key_or_record_id) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    uint64_t now = Crypto::current_timestamp();

    LineageNode node;
    auto it = tx_index_.find(key_or_record_id);
    if (it != tx_index_.end()) {
        const auto& tx = it->second;
        node.tx_id = tx.tx_id;
        node.type = tx_type_to_string(tx.type);
        node.sender = tx.sender;
        node.recipient = tx.recipient;
        node.record_hash = tx.record_hash;
        node.valid_from = tx.valid_from;
        node.valid_until = tx.valid_until;
        node.parent_tx_id = tx.parent_tx_id;
        node.is_active = (revoked_keys_.find(tx.tx_id) == revoked_keys_.end()) && tx.is_temporally_valid(now);

        // Find child delegations
        for (const auto& pair : tx_index_) {
            if (pair.second.parent_tx_id == tx.tx_id) {
                node.child_delegations.push_back(pair.second.tx_id);
            }
        }
    }
    return node;
}

std::string Blockchain::get_birds_eye_view_json(uint64_t current_time) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    if (current_time == 0) current_time = Crypto::current_timestamp();

    JsonValue root = JsonValue::object();
    root["chain_height"] = static_cast<int64_t>(chain_.size());
    root["total_transactions"] = static_cast<int64_t>(tx_index_.size());
    root["mempool_size"] = static_cast<int64_t>(mempool_.size());
    root["current_time"] = current_time;

    // Authorities
    JsonValue auths = JsonValue::array();
    for (const auto& a : consensus_.get_all_authorities()) {
        JsonValue av = JsonValue::object();
        av["address"] = a.address;
        av["name"] = a.name;
        auths.push_back(av);
    }
    root["authorities"] = auths;

    // Blocks summary
    JsonValue blocks_arr = JsonValue::array();
    for (const auto& b : chain_) {
        JsonValue bv = JsonValue::object();
        bv["index"] = static_cast<int64_t>(b.index);
        bv["hash"] = b.hash;
        bv["prev_hash"] = b.prev_hash;
        bv["timestamp"] = b.timestamp;
        bv["authority_address"] = b.authority_address;
        bv["authority_name"] = b.authority_name;
        bv["tx_count"] = static_cast<int64_t>(b.transactions.size());
        blocks_arr.push_back(bv);
    }
    root["blocks"] = blocks_arr;

    // Lineage Graph: Nodes and Directed Edges
    // Graph visualizes: Patient Address -> Temporal Key -> Hospital -> Doctor
    std::map<std::string, bool> distinct_addresses;
    JsonValue links = JsonValue::array();
    JsonValue keys_arr = JsonValue::array();

    for (const auto& pair : tx_index_) {
        const auto& tx = pair.second;
        if (tx.type == TxType::TEMPORAL_KEY_GRANT || tx.type == TxType::TEMPORAL_KEY_DELEGATE) {
            distinct_addresses[tx.sender] = true;
            distinct_addresses[tx.recipient] = true;

            bool is_revoked = (revoked_keys_.find(tx.tx_id) != revoked_keys_.end());
            bool is_expired = (tx.valid_until > 0 && current_time > tx.valid_until);
            bool is_active = !is_revoked && !is_expired;

            int64_t time_remaining = 0;
            if (tx.valid_until > current_time) {
                time_remaining = static_cast<int64_t>(tx.valid_until - current_time);
            }

            JsonValue kv = JsonValue::object();
            kv["tx_id"] = tx.tx_id;
            kv["type"] = tx_type_to_string(tx.type);
            kv["sender"] = tx.sender;
            kv["recipient"] = tx.recipient;
            kv["record_hash"] = tx.record_hash;
            kv["parent_tx_id"] = tx.parent_tx_id;
            kv["valid_from"] = tx.valid_from;
            kv["valid_until"] = tx.valid_until;
            kv["is_active"] = is_active;
            kv["is_expired"] = is_expired;
            kv["is_revoked"] = is_revoked;
            kv["time_remaining_seconds"] = time_remaining;
            keys_arr.push_back(kv);

            // Directed Edge in Lineage Graph
            JsonValue edge = JsonValue::object();
            edge["from"] = tx.sender;
            edge["to"] = tx.recipient;
            edge["key_id"] = tx.tx_id;
            edge["record_hash"] = tx.record_hash;
            edge["parent_tx_id"] = tx.parent_tx_id;
            edge["is_active"] = is_active;
            edge["type"] = tx_type_to_string(tx.type);
            links.push_back(edge);
        } else if (tx.type == TxType::RECORD_STORE) {
            distinct_addresses[tx.sender] = true;
        }
    }

    JsonValue nodes = JsonValue::array();
    for (const auto& addr_pair : distinct_addresses) {
        JsonValue nv = JsonValue::object();
        nv["address"] = addr_pair.first;
        nodes.push_back(nv);
    }

    JsonValue graph = JsonValue::object();
    graph["nodes"] = nodes;
    graph["links"] = links;

    root["temporal_keys"] = keys_arr;
    root["lineage_graph"] = graph;

    return root.dump();
}

} // namespace Sanjeev
