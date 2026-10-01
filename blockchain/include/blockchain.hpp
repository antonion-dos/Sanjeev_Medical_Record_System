#pragma once

#include "block.hpp"
#include "consensus.hpp"
#include "storage.hpp"
#include <vector>
#include <mutex>
#include <memory>
#include <optional>

namespace Sanjeev {

struct LineageBlobNode {
    EncryptedBlob blob;
    std::vector<TemporalAccessToken> tokens;
    std::vector<DecryptionAuditPayload> audits;
};

struct LineageTree {
    Hash256 head_blob_id{};
    std::vector<LineageBlobNode> versions;
};

class Blockchain {
public:
    Blockchain();
    ~Blockchain() = default;

    // Database & state initialization
    bool init(const std::string& db_path = "sanjeev.db");

    // Authority setup & Genesis
    void register_authority(const Address& address, const std::string& name, const std::string& public_key_pem);
    Block create_genesis_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name);

    // Transaction & Block lifecycle
    bool add_transaction(const Transaction& tx);
    Block mine_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name);

    // Status queries
    uint64_t get_chain_height() const;
    std::optional<Block> get_block_by_index(uint64_t index) const;
    std::optional<Block> get_block_by_hash(const Hash256& hash) const;
    std::optional<Block> get_latest_block() const;
    std::vector<Transaction> get_mempool() const;

    // Record & Token queries
    std::optional<EncryptedBlob> get_blob(const Hash256& blob_id) const;
    std::vector<EncryptedBlob> get_blobs_by_owner(const Address& owner) const;
    std::vector<EncryptedBlob> get_blob_version_history(const Hash256& head_blob_id) const;
    std::optional<TemporalAccessToken> get_token(const Hash256& token_id) const;
    std::vector<TemporalAccessToken> get_active_tokens_for_recipient(const Address& recipient, uint64_t current_time) const;

    // Decryption Access Verification & On-Chain Audit Logging
    bool request_decryption(const Hash256& token_id, const Address& accessor_address, uint64_t current_time, std::vector<uint8_t>& out_encrypted_symkey);

    // Audit logs
    std::vector<DecryptionAuditPayload> get_audits_for_blob(const Hash256& blob_id) const;
    std::vector<DecryptionAuditPayload> get_audits_for_accessor(const Address& accessor) const;

    // Lineage Tree
    LineageTree get_lineage_for_blob(const Hash256& head_blob_id) const;

    // PoA Consensus access
    const PoAConsensus& get_consensus() const { return consensus_; }
    Storage& get_storage() { return storage_; }

private:
    Storage storage_;
    PoAConsensus consensus_;
    std::vector<Transaction> mempool_;
    mutable std::mutex chain_mutex_;
};

} // namespace Sanjeev
