#include "blockchain.hpp"
#include "crypto.hpp"
#include <iostream>

namespace Sanjeev {

Blockchain::Blockchain() = default;

bool Blockchain::init(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return storage_.open(db_path);
}

void Blockchain::register_authority(const Address& address, const std::string& name, const std::string& public_key_pem) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    consensus_.register_authority(address, name, public_key_pem);
}

Block Blockchain::create_genesis_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    if (storage_.get_block_count() > 0) {
        auto b0 = storage_.get_block_by_index(0);
        if (b0) return *b0;
    }

    Address auth_addr = Crypto::derive_address_bytes(authority_pubkey);
    consensus_.register_authority(auth_addr, authority_name, authority_pubkey);

    Block genesis;
    genesis.header.version = 1;
    genesis.header.index = 0;
    genesis.header.timestamp = Crypto::current_timestamp();
    genesis.header.prev_hash.fill(0);
    genesis.header.authority_address = auth_addr;
    genesis.header.authority_name = authority_name;

    consensus_.sign_block(genesis, authority_privkey, auth_addr, authority_name);
    storage_.save_block(genesis);
    return genesis;
}

bool Blockchain::add_transaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(chain_mutex_);

    // 1. Validate transaction signature(s) if provided
    Hash256 tx_id = tx.calculate_id();
    for (const auto& sig : tx.signatures) {
        if (!sig.public_key.empty() && !sig.signature.empty()) {
            std::string pubkey_pem(reinterpret_cast<const char*>(sig.public_key.data()), sig.public_key.size());
            if (!Crypto::verify_bytes(pubkey_pem, tx_id.data(), tx_id.size(), sig.signature)) {
                std::cerr << "Transaction signature verification failed for " << address_to_hex(sig.signer_address) << std::endl;
                return false;
            }
        }
    }

    // 2. Validate token rules
    if (std::holds_alternative<TokenGrantPayload>(tx.payload)) {
        const auto& t = std::get<TokenGrantPayload>(tx.payload).token;
        if (t.valid_until < t.valid_from) {
            std::cerr << "Invalid token grant: valid_until is before valid_from" << std::endl;
            return false;
        }
    } else if (std::holds_alternative<TokenDelegatePayload>(tx.payload)) {
        const auto& p = std::get<TokenDelegatePayload>(tx.payload);
        auto parent_opt = storage_.get_token(p.parent_token_id);
        if (!parent_opt || parent_opt->status != 1) {
            std::cerr << "Invalid token delegation: parent token does not exist or is inactive" << std::endl;
            return false;
        }
        if (p.child_token.valid_until > parent_opt->valid_until) {
            std::cerr << "Delegated token cannot exceed parent expiration" << std::endl;
            return false;
        }
    } else if (std::holds_alternative<TokenRevokePayload>(tx.payload)) {
        const auto& p = std::get<TokenRevokePayload>(tx.payload);
        auto target = storage_.get_token(p.target_token_id);
        if (!target) {
            std::cerr << "Cannot revoke nonexistent token" << std::endl;
            return false;
        }
        if (target->grantor_address != tx.sender) {
            std::cerr << "Only token grantor can revoke token" << std::endl;
            return false;
        }
    }

    mempool_.push_back(tx);
    return true;
}

Block Blockchain::mine_block(const std::string& authority_privkey, const std::string& authority_pubkey, const std::string& authority_name) {
    std::lock_guard<std::mutex> lock(chain_mutex_);

    auto latest = storage_.get_latest_block();
    Address auth_addr = Crypto::derive_address_bytes(authority_pubkey);

    Block block;
    block.header.version = 1;
    block.header.index = latest ? (latest->header.index + 1) : 0;
    block.header.timestamp = Crypto::current_timestamp();
    block.header.prev_hash = latest ? latest->calculate_hash() : Hash256{};
    block.transactions = std::move(mempool_);
    mempool_.clear();

    if (!consensus_.sign_block(block, authority_privkey, auth_addr, authority_name)) {
        throw std::runtime_error("Failed to sign block with authority credentials");
    }

    if (!consensus_.validate_block(block, latest ? &(*latest) : nullptr)) {
        throw std::runtime_error("Mined block failed PoA validation");
    }

    if (!storage_.save_block(block)) {
        throw std::runtime_error("Failed to persist mined block to SQLite storage");
    }

    return block;
}

uint64_t Blockchain::get_chain_height() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_block_count();
}

std::optional<Block> Blockchain::get_block_by_index(uint64_t index) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_block_by_index(index);
}

std::optional<Block> Blockchain::get_block_by_hash(const Hash256& hash) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_block_by_hash(hash);
}

std::optional<Block> Blockchain::get_latest_block() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_latest_block();
}

std::vector<Transaction> Blockchain::get_mempool() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return mempool_;
}

std::optional<EncryptedBlob> Blockchain::get_blob(const Hash256& blob_id) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_blob(blob_id);
}

std::vector<EncryptedBlob> Blockchain::get_blobs_by_owner(const Address& owner) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_blobs_by_owner(owner);
}

std::vector<EncryptedBlob> Blockchain::get_blob_version_history(const Hash256& head_blob_id) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_blob_version_history(head_blob_id);
}

std::optional<TemporalAccessToken> Blockchain::get_token(const Hash256& token_id) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_token(token_id);
}

std::vector<TemporalAccessToken> Blockchain::get_active_tokens_for_recipient(const Address& recipient, uint64_t current_time) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_active_tokens_for_recipient(recipient, current_time);
}

bool Blockchain::request_decryption(const Hash256& token_id, const Address& accessor_address, uint64_t current_time, std::vector<uint8_t>& out_encrypted_symkey) {
    std::lock_guard<std::mutex> lock(chain_mutex_);

    auto token_opt = storage_.get_token(token_id);
    if (!token_opt || token_opt->status != 1) {
        return false;
    }

    if (token_opt->recipient_address != accessor_address) {
        return false;
    }

    if (!token_opt->is_temporally_valid(current_time)) {
        return false;
    }

    // Access permitted: record on-chain audit receipt in mempool
    Transaction audit_tx;
    audit_tx.version = 1;
    audit_tx.type = TxType::DECRYPTION_AUDIT;
    audit_tx.timestamp = current_time;
    audit_tx.sender = accessor_address;
    audit_tx.payload = DecryptionAuditPayload{token_id, token_opt->target_blob_id, accessor_address, current_time};
    mempool_.push_back(audit_tx);

    out_encrypted_symkey = token_opt->encrypted_symkey;
    return true;
}

std::vector<DecryptionAuditPayload> Blockchain::get_audits_for_blob(const Hash256& blob_id) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_audits_for_blob(blob_id);
}

std::vector<DecryptionAuditPayload> Blockchain::get_audits_for_accessor(const Address& accessor) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    return const_cast<Storage&>(storage_).get_audits_for_accessor(accessor);
}

LineageTree Blockchain::get_lineage_for_blob(const Hash256& head_blob_id) const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    LineageTree tree;
    tree.head_blob_id = head_blob_id;

    auto history = const_cast<Storage&>(storage_).get_blob_version_history(head_blob_id);
    for (const auto& blob : history) {
        LineageBlobNode node;
        node.blob = blob;
        node.audits = const_cast<Storage&>(storage_).get_audits_for_blob(blob.blob_id);
        tree.versions.push_back(std::move(node));
    }

    return tree;
}

} // namespace Sanjeev
