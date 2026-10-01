#include "storage.hpp"
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace Sanjeev {

Storage::Storage() = default;

Storage::~Storage() {
    close();
}

bool Storage::open(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_) close();

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }

    // Enable WAL mode for high concurrency & synchronous NORMAL
    char* err_msg = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); err_msg = nullptr; }
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); err_msg = nullptr; }

    return init_schema();
}

void Storage::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Storage::init_schema() {
    const char* schema_sql = R"(
        CREATE TABLE IF NOT EXISTS blocks (
            block_index INTEGER PRIMARY KEY,
            block_hash TEXT UNIQUE NOT NULL,
            prev_hash TEXT NOT NULL,
            merkle_root TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            authority_address TEXT NOT NULL,
            authority_name TEXT NOT NULL,
            tx_count INTEGER NOT NULL,
            raw_data BLOB NOT NULL
        );

        CREATE TABLE IF NOT EXISTS transactions (
            tx_id TEXT PRIMARY KEY,
            block_index INTEGER NOT NULL,
            tx_type INTEGER NOT NULL,
            sender_address TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            nonce INTEGER NOT NULL,
            raw_data BLOB NOT NULL
        );

        CREATE TABLE IF NOT EXISTS blobs (
            blob_id TEXT PRIMARY KEY,
            previous_blob_id TEXT NOT NULL,
            owner_address TEXT NOT NULL,
            updater_address TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            iv BLOB NOT NULL,
            tag BLOB NOT NULL,
            data BLOB NOT NULL,
            block_index INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS temporal_tokens (
            token_id TEXT PRIMARY KEY,
            target_blob_id TEXT NOT NULL,
            grantor_address TEXT NOT NULL,
            recipient_address TEXT NOT NULL,
            valid_from INTEGER NOT NULL,
            valid_until INTEGER NOT NULL,
            parent_token_id TEXT NOT NULL,
            encrypted_symkey BLOB NOT NULL,
            status INTEGER NOT NULL,
            block_index INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS decryption_audits (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            token_id TEXT NOT NULL,
            blob_id TEXT NOT NULL,
            accessor_address TEXT NOT NULL,
            access_timestamp INTEGER NOT NULL,
            block_index INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_blobs_owner ON blobs(owner_address);
        CREATE INDEX IF NOT EXISTS idx_blobs_prev ON blobs(previous_blob_id);
        CREATE INDEX IF NOT EXISTS idx_tokens_recipient ON temporal_tokens(recipient_address);
        CREATE INDEX IF NOT EXISTS idx_tokens_grantor ON temporal_tokens(grantor_address);
        CREATE INDEX IF NOT EXISTS idx_tokens_blob ON temporal_tokens(target_blob_id);
        CREATE INDEX IF NOT EXISTS idx_audits_blob ON decryption_audits(blob_id);
        CREATE INDEX IF NOT EXISTS idx_audits_accessor ON decryption_audits(accessor_address);
    )";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, schema_sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            std::cerr << "SQLite init_schema error: " << err_msg << std::endl;
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

bool Storage::save_block(const Block& block) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return false;

    char* err_msg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }

    // Serialize full block
    BinaryWriter bw;
    block.serialize(bw);
    const auto& raw_block = bw.get_buffer();

    std::string b_hash = block.get_hash_hex();
    std::string prev_h = hash_to_hex(block.header.prev_hash);
    std::string m_root = hash_to_hex(block.header.merkle_root);
    std::string auth_addr = address_to_hex(block.header.authority_address);

    const char* ins_block_sql = R"(
        INSERT OR REPLACE INTO blocks (
            block_index, block_hash, prev_hash, merkle_root,
            timestamp, authority_address, authority_name, tx_count, raw_data
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, ins_block_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_int64(stmt, 1, block.header.index);
    sqlite3_bind_text(stmt, 2, b_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, prev_h.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, m_root.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, block.header.timestamp);
    sqlite3_bind_text(stmt, 6, auth_addr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, block.header.authority_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, static_cast<int>(block.transactions.size()));
    sqlite3_bind_blob(stmt, 9, raw_block.data(), static_cast<int>(raw_block.size()), SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_finalize(stmt);

    // Index each transaction and its specific payload
    for (const auto& tx : block.transactions) {
        if (!index_transaction(tx, block.header.index)) {
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    }

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Storage::index_transaction(const Transaction& tx, uint64_t block_index) {
    BinaryWriter tw;
    tx.serialize(tw);
    const auto& raw_tx = tw.get_buffer();

    std::string tx_id = tx.get_id_hex();
    std::string sender = address_to_hex(tx.sender);

    const char* ins_tx_sql = R"(
        INSERT OR REPLACE INTO transactions (
            tx_id, block_index, tx_type, sender_address, timestamp, nonce, raw_data
        ) VALUES (?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, ins_tx_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, tx_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, block_index);
    sqlite3_bind_int(stmt, 3, static_cast<int>(tx.type));
    sqlite3_bind_text(stmt, 4, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, tx.timestamp);
    sqlite3_bind_int64(stmt, 6, tx.nonce);
    sqlite3_bind_blob(stmt, 7, raw_tx.data(), static_cast<int>(raw_tx.size()), SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    // Handle Payload-specific indexing
    if (std::holds_alternative<BlobStorePayload>(tx.payload)) {
        const auto& blob = std::get<BlobStorePayload>(tx.payload).blob;
        const char* sql = R"(
            INSERT OR REPLACE INTO blobs (
                blob_id, previous_blob_id, owner_address, updater_address,
                timestamp, iv, tag, data, block_index
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string b_id = hash_to_hex(blob.blob_id);
            std::string prev_id = hash_to_hex(blob.previous_blob_id);
            std::string owner = address_to_hex(blob.owner_address);
            std::string updater = address_to_hex(blob.updater_address);

            sqlite3_bind_text(stmt, 1, b_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, prev_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, owner.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, updater.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, blob.timestamp);
            sqlite3_bind_blob(stmt, 6, blob.iv.data(), static_cast<int>(blob.iv.size()), SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 7, blob.tag.data(), static_cast<int>(blob.tag.size()), SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 8, blob.data.data(), static_cast<int>(blob.data.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 9, block_index);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else if (std::holds_alternative<BlobUpdatePayload>(tx.payload)) {
        const auto& p = std::get<BlobUpdatePayload>(tx.payload);
        const char* sql = R"(
            INSERT OR REPLACE INTO blobs (
                blob_id, previous_blob_id, owner_address, updater_address,
                timestamp, iv, tag, data, block_index
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string b_id = hash_to_hex(p.new_blob.blob_id);
            std::string prev_id = hash_to_hex(p.previous_blob_id);
            std::string owner = address_to_hex(p.new_blob.owner_address);
            std::string updater = address_to_hex(p.new_blob.updater_address);

            sqlite3_bind_text(stmt, 1, b_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, prev_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, owner.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, updater.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, p.new_blob.timestamp);
            sqlite3_bind_blob(stmt, 6, p.new_blob.iv.data(), static_cast<int>(p.new_blob.iv.size()), SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 7, p.new_blob.tag.data(), static_cast<int>(p.new_blob.tag.size()), SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 8, p.new_blob.data.data(), static_cast<int>(p.new_blob.data.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 9, block_index);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else if (std::holds_alternative<TokenGrantPayload>(tx.payload)) {
        const auto& t = std::get<TokenGrantPayload>(tx.payload).token;
        const char* sql = R"(
            INSERT OR REPLACE INTO temporal_tokens (
                token_id, target_blob_id, grantor_address, recipient_address,
                valid_from, valid_until, parent_token_id, encrypted_symkey, status, block_index
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string tok_id = hash_to_hex(t.token_id);
            std::string tgt_id = hash_to_hex(t.target_blob_id);
            std::string grantor = address_to_hex(t.grantor_address);
            std::string recip = address_to_hex(t.recipient_address);
            std::string parent = hash_to_hex(t.parent_token_id);

            sqlite3_bind_text(stmt, 1, tok_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, tgt_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, grantor.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, recip.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, t.valid_from);
            sqlite3_bind_int64(stmt, 6, t.valid_until);
            sqlite3_bind_text(stmt, 7, parent.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 8, t.encrypted_symkey.data(), static_cast<int>(t.encrypted_symkey.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 9, t.status);
            sqlite3_bind_int64(stmt, 10, block_index);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else if (std::holds_alternative<TokenDelegatePayload>(tx.payload)) {
        const auto& t = std::get<TokenDelegatePayload>(tx.payload).child_token;
        const char* sql = R"(
            INSERT OR REPLACE INTO temporal_tokens (
                token_id, target_blob_id, grantor_address, recipient_address,
                valid_from, valid_until, parent_token_id, encrypted_symkey, status, block_index
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string tok_id = hash_to_hex(t.token_id);
            std::string tgt_id = hash_to_hex(t.target_blob_id);
            std::string grantor = address_to_hex(t.grantor_address);
            std::string recip = address_to_hex(t.recipient_address);
            std::string parent = hash_to_hex(t.parent_token_id);

            sqlite3_bind_text(stmt, 1, tok_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, tgt_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, grantor.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, recip.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, t.valid_from);
            sqlite3_bind_int64(stmt, 6, t.valid_until);
            sqlite3_bind_text(stmt, 7, parent.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_blob(stmt, 8, t.encrypted_symkey.data(), static_cast<int>(t.encrypted_symkey.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 9, t.status);
            sqlite3_bind_int64(stmt, 10, block_index);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else if (std::holds_alternative<TokenRevokePayload>(tx.payload)) {
        const auto& p = std::get<TokenRevokePayload>(tx.payload);
        const char* sql = "UPDATE temporal_tokens SET status = 2 WHERE token_id = ?;";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string tok_id = hash_to_hex(p.target_token_id);
            sqlite3_bind_text(stmt, 1, tok_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else if (std::holds_alternative<DecryptionAuditPayload>(tx.payload)) {
        const auto& a = std::get<DecryptionAuditPayload>(tx.payload);
        const char* sql = R"(
            INSERT INTO decryption_audits (
                token_id, blob_id, accessor_address, access_timestamp, block_index
            ) VALUES (?, ?, ?, ?, ?);
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string tok_id = hash_to_hex(a.token_id);
            std::string b_id = hash_to_hex(a.blob_id);
            std::string acc = address_to_hex(a.accessor_address);

            sqlite3_bind_text(stmt, 1, tok_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, b_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, acc.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 4, a.access_timestamp);
            sqlite3_bind_int64(stmt, 5, block_index);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return true;
}

std::optional<Block> Storage::get_block_by_index(uint64_t index) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return std::nullopt;

    const char* sql = "SELECT raw_data FROM blocks WHERE block_index = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int64(stmt, 1, index);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* blob_bytes = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int blob_size = sqlite3_column_bytes(stmt, 0);
        BinaryReader r(blob_bytes, blob_size);
        Block b = Block::deserialize(r);
        sqlite3_finalize(stmt);
        return b;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<Block> Storage::get_block_by_hash(const Hash256& hash) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return std::nullopt;

    const char* sql = "SELECT raw_data FROM blocks WHERE block_hash = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    std::string h_str = hash_to_hex(hash);
    sqlite3_bind_text(stmt, 1, h_str.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* blob_bytes = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int blob_size = sqlite3_column_bytes(stmt, 0);
        BinaryReader r(blob_bytes, blob_size);
        Block b = Block::deserialize(r);
        sqlite3_finalize(stmt);
        return b;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<Block> Storage::get_latest_block() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return std::nullopt;

    const char* sql = "SELECT raw_data FROM blocks ORDER BY block_index DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* blob_bytes = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int blob_size = sqlite3_column_bytes(stmt, 0);
        BinaryReader r(blob_bytes, blob_size);
        Block b = Block::deserialize(r);
        sqlite3_finalize(stmt);
        return b;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

uint64_t Storage::get_block_count() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return 0;

    const char* sql = "SELECT COUNT(*) FROM blocks;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    uint64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

std::optional<Transaction> Storage::get_transaction(const Hash256& tx_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return std::nullopt;

    const char* sql = "SELECT raw_data FROM transactions WHERE tx_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    std::string id_str = hash_to_hex(tx_id);
    sqlite3_bind_text(stmt, 1, id_str.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* blob_bytes = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0));
        int blob_size = sqlite3_column_bytes(stmt, 0);
        BinaryReader r(blob_bytes, blob_size);
        Transaction tx = Transaction::deserialize(r);
        sqlite3_finalize(stmt);
        return tx;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<EncryptedBlob> Storage::get_blob(const Hash256& blob_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return std::nullopt;

    const char* sql = R"(
        SELECT blob_id, previous_blob_id, owner_address, updater_address,
               timestamp, iv, tag, data
        FROM blobs WHERE blob_id = ?;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    std::string id_str = hash_to_hex(blob_id);
    sqlite3_bind_text(stmt, 1, id_str.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        EncryptedBlob b;
        b.blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        b.previous_blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        b.owner_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        b.updater_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        b.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));

        const uint8_t* iv_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 5));
        b.iv.assign(iv_ptr, iv_ptr + sqlite3_column_bytes(stmt, 5));

        const uint8_t* tag_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 6));
        b.tag.assign(tag_ptr, tag_ptr + sqlite3_column_bytes(stmt, 6));

        const uint8_t* data_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 7));
        b.data.assign(data_ptr, data_ptr + sqlite3_column_bytes(stmt, 7));

        sqlite3_finalize(stmt);
        return b;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<EncryptedBlob> Storage::get_blobs_by_owner(const Address& owner) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<EncryptedBlob> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT blob_id, previous_blob_id, owner_address, updater_address,
               timestamp, iv, tag, data
        FROM blobs WHERE owner_address = ? ORDER BY timestamp DESC;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string o_str = address_to_hex(owner);
    sqlite3_bind_text(stmt, 1, o_str.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EncryptedBlob b;
        b.blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        b.previous_blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        b.owner_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        b.updater_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        b.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));

        const uint8_t* iv_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 5));
        b.iv.assign(iv_ptr, iv_ptr + sqlite3_column_bytes(stmt, 5));

        const uint8_t* tag_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 6));
        b.tag.assign(tag_ptr, tag_ptr + sqlite3_column_bytes(stmt, 6));

        const uint8_t* data_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 7));
        b.data.assign(data_ptr, data_ptr + sqlite3_column_bytes(stmt, 7));

        results.push_back(std::move(b));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<EncryptedBlob> Storage::get_blob_version_history(const Hash256& head_blob_id) {
    std::vector<EncryptedBlob> history;
    Hash256 current_id = head_blob_id;

    while (!is_zero_hash(current_id)) {
        auto blob_opt = get_blob(current_id);
        if (!blob_opt) break;
        current_id = blob_opt->previous_blob_id;
        history.push_back(std::move(*blob_opt));
    }

    return history;
}

std::optional<TemporalAccessToken> Storage::get_token(const Hash256& token_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return std::nullopt;

    const char* sql = R"(
        SELECT token_id, target_blob_id, grantor_address, recipient_address,
               valid_from, valid_until, parent_token_id, encrypted_symkey, status
        FROM temporal_tokens WHERE token_id = ?;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    std::string id_str = hash_to_hex(token_id);
    sqlite3_bind_text(stmt, 1, id_str.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        TemporalAccessToken t;
        t.token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        t.target_blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        t.grantor_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        t.recipient_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        t.valid_from = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
        t.valid_until = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
        t.parent_token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));

        const uint8_t* key_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 7));
        t.encrypted_symkey.assign(key_ptr, key_ptr + sqlite3_column_bytes(stmt, 7));
        t.status = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));

        sqlite3_finalize(stmt);
        return t;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<TemporalAccessToken> Storage::get_active_tokens_for_recipient(const Address& recipient, uint64_t current_time) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TemporalAccessToken> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT token_id, target_blob_id, grantor_address, recipient_address,
               valid_from, valid_until, parent_token_id, encrypted_symkey, status
        FROM temporal_tokens
        WHERE recipient_address = ? AND status = 1 AND valid_from <= ? AND valid_until >= ?;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string r_str = address_to_hex(recipient);
    sqlite3_bind_text(stmt, 1, r_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, current_time);
    sqlite3_bind_int64(stmt, 3, current_time);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TemporalAccessToken t;
        t.token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        t.target_blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        t.grantor_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        t.recipient_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        t.valid_from = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
        t.valid_until = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
        t.parent_token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));

        const uint8_t* key_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 7));
        t.encrypted_symkey.assign(key_ptr, key_ptr + sqlite3_column_bytes(stmt, 7));
        t.status = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));

        results.push_back(std::move(t));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<TemporalAccessToken> Storage::get_tokens_for_grantor(const Address& grantor) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TemporalAccessToken> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT token_id, target_blob_id, grantor_address, recipient_address,
               valid_from, valid_until, parent_token_id, encrypted_symkey, status
        FROM temporal_tokens WHERE grantor_address = ?;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string g_str = address_to_hex(grantor);
    sqlite3_bind_text(stmt, 1, g_str.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TemporalAccessToken t;
        t.token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        t.target_blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        t.grantor_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        t.recipient_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        t.valid_from = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
        t.valid_until = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
        t.parent_token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));

        const uint8_t* key_ptr = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 7));
        t.encrypted_symkey.assign(key_ptr, key_ptr + sqlite3_column_bytes(stmt, 7));
        t.status = static_cast<uint8_t>(sqlite3_column_int(stmt, 8));

        results.push_back(std::move(t));
    }

    sqlite3_finalize(stmt);
    return results;
}

bool Storage::set_token_status(const Hash256& token_id, uint8_t status) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!db_) return false;

    const char* sql = "UPDATE temporal_tokens SET status = ? WHERE token_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string id_str = hash_to_hex(token_id);
    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_text(stmt, 2, id_str.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<DecryptionAuditPayload> Storage::get_audits_for_blob(const Hash256& blob_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<DecryptionAuditPayload> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT token_id, blob_id, accessor_address, access_timestamp
        FROM decryption_audits WHERE blob_id = ? ORDER BY access_timestamp DESC;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string b_str = hash_to_hex(blob_id);
    sqlite3_bind_text(stmt, 1, b_str.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DecryptionAuditPayload a;
        a.token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        a.blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        a.accessor_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        a.access_timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        results.push_back(std::move(a));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<DecryptionAuditPayload> Storage::get_audits_for_accessor(const Address& accessor) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<DecryptionAuditPayload> results;
    if (!db_) return results;

    const char* sql = R"(
        SELECT token_id, blob_id, accessor_address, access_timestamp
        FROM decryption_audits WHERE accessor_address = ? ORDER BY access_timestamp DESC;
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

    std::string a_str = address_to_hex(accessor);
    sqlite3_bind_text(stmt, 1, a_str.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DecryptionAuditPayload a;
        a.token_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        a.blob_id = hex_to_hash(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        a.accessor_address = hex_to_address(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        a.access_timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        results.push_back(std::move(a));
    }

    sqlite3_finalize(stmt);
    return results;
}

} // namespace Sanjeev
