#pragma once

#include "block.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>

struct sqlite3;

namespace Sanjeev {

class Storage {
public:
    Storage();
    ~Storage();

    // Database lifecycle
    bool open(const std::string& db_path = "sanjeev.db");
    void close();
    bool is_open() const { return db_ != nullptr; }

    // Block operations (ACID transaction)
    bool save_block(const Block& block);
    std::optional<Block> get_block_by_index(uint64_t index);
    std::optional<Block> get_block_by_hash(const Hash256& hash);
    std::optional<Block> get_latest_block();
    uint64_t get_block_count();

    // Transaction lookups
    std::optional<Transaction> get_transaction(const Hash256& tx_id);

    // Encrypted Blob storage & lineage
    std::optional<EncryptedBlob> get_blob(const Hash256& blob_id);
    std::vector<EncryptedBlob> get_blobs_by_owner(const Address& owner);
    std::vector<EncryptedBlob> get_blob_version_history(const Hash256& head_blob_id);

    // Temporal Access Token management
    std::optional<TemporalAccessToken> get_token(const Hash256& token_id);
    std::vector<TemporalAccessToken> get_active_tokens_for_recipient(const Address& recipient, uint64_t current_time);
    std::vector<TemporalAccessToken> get_tokens_for_grantor(const Address& grantor);
    bool set_token_status(const Hash256& token_id, uint8_t status);

    // Decryption Audits
    std::vector<DecryptionAuditPayload> get_audits_for_blob(const Hash256& blob_id);
    std::vector<DecryptionAuditPayload> get_audits_for_accessor(const Address& accessor);

private:
    sqlite3* db_ = nullptr;
    mutable std::mutex db_mutex_;

    bool init_schema();
    bool index_transaction(const Transaction& tx, uint64_t block_index);
};

} // namespace Sanjeev
