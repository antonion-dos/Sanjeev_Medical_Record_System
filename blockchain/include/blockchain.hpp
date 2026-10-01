#pragma once

#include "block.hpp"
#include "consensus.hpp"
#include <vector>
#include <mutex>
#include <memory>
#include <map>

namespace Sanjeev {

struct LineageNode {
    std::string tx_id;
    std::string type;
    std::string sender;
    std::string recipient;
    std::string record_hash;
    uint64_t valid_from = 0;
    uint64_t valid_until = 0;
    bool is_active = false;
    std::string parent_tx_id;
    std::vector<std::string> child_delegations;
};

class Blockchain {
public:
    Blockchain();

    // Authority setup & Genesis
    void register_authority(const std::string& address, const std::string& name, const std::string& public_key_pem);
    Block create_genesis_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name);

    // Transaction & Block lifecycle
    bool add_transaction(const Transaction& tx);
    Block mine_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name);

    // Chain validation
    bool is_chain_valid() const;
    size_t get_chain_length() const;
    const std::vector<Block>& get_blocks() const;
    const std::vector<Transaction>& get_mempool() const;

    // Queries
    std::vector<Transaction> get_all_records() const;
    std::vector<Transaction> get_records_for_patient(const std::string& patient_address) const;
    std::vector<Transaction> get_active_temporal_keys(uint64_t current_time = 0) const;
    std::vector<Transaction> get_temporal_keys_for_holder(const std::string& holder_address) const;
    
    // Temporal key authorization check
    bool is_access_authorized(const std::string& record_hash, const std::string& accessor_address, uint64_t current_time = 0) const;

    // Traceability & Lineage
    LineageNode trace_lineage(const std::string& key_or_record_id) const;
    std::string get_birds_eye_view_json(uint64_t current_time = 0) const;

    const PoAConsensus& get_consensus() const { return consensus_; }

private:
    std::vector<Block> chain_;
    std::vector<Transaction> mempool_;
    PoAConsensus consensus_;
    mutable std::mutex chain_mutex_;

    // Fast lookup caches
    std::map<std::string, Transaction> tx_index_;
    std::map<std::string, bool> revoked_keys_;
};

} // namespace Sanjeev
