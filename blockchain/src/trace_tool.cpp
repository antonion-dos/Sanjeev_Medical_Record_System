#include "blockchain.hpp"
#include "crypto.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <set>

using namespace Sanjeev;

namespace {
    std::atomic<bool> g_tracer_running{true};
    void tracer_sig_handler(int) {
        g_tracer_running = false;
    }
}

void print_banner() {
    std::cout << "===============================================================\n";
    std::cout << "   Sanjeev (संजीव) - Blockchain Transaction & Lineage Tracer   \n";
    std::cout << "===============================================================\n";
}

void print_usage(const char* prog) {
    std::cout << "Usage:\n";
    std::cout << "  " << prog << " --chain [--db <path>] [--follow]\n";
    std::cout << "  " << prog << " --blob <blob_id_hex> [--db <path>] [--follow]\n";
    std::cout << "  " << prog << " --token <token_id_hex> [--db <path>]\n";
    std::cout << "  " << prog << " --tx <tx_id_hex> [--db <path>]\n";
    std::cout << "Options:\n";
    std::cout << "  --follow, -f    Stay open and stream new blocks and events in real time\n";
    std::cout << "  --db <path>     Path to SQLite database (default: sanjeev_node.db)\n";
}

void print_transaction_details(const Transaction& tx, const std::string& prefix = "   └─ ") {
    std::cout << prefix << "[Tx " << tx_type_to_string(tx.type) << "] ID: "
              << tx.get_id_hex().substr(0, 16) << "..."
              << " | Sender: " << address_to_hex(tx.sender).substr(0, 14) << "...\n";

    if (std::holds_alternative<BlobStorePayload>(tx.payload)) {
        const auto& p = std::get<BlobStorePayload>(tx.payload);
        std::cout << "      • Target Blob: " << hash_to_hex(p.blob.blob_id).substr(0, 20) << "...\n"
                  << "      • Owner: " << address_to_hex(p.blob.owner_address).substr(0, 14) << "..."
                  << " | Size: " << p.blob.data.size() << " bytes\n";
    } else if (std::holds_alternative<BlobUpdatePayload>(tx.payload)) {
        const auto& p = std::get<BlobUpdatePayload>(tx.payload);
        std::cout << "      • New Blob: " << hash_to_hex(p.new_blob.blob_id).substr(0, 20) << "...\n"
                  << "      • Previous Version: " << hash_to_hex(p.previous_blob_id).substr(0, 20) << "...\n"
                  << "      • Updater: " << address_to_hex(p.new_blob.updater_address).substr(0, 14) << "...\n";
    } else if (std::holds_alternative<TokenGrantPayload>(tx.payload)) {
        const auto& p = std::get<TokenGrantPayload>(tx.payload);
        std::cout << "      • Token ID: " << hash_to_hex(p.token.token_id).substr(0, 20) << "...\n"
                  << "      • Recipient: " << address_to_hex(p.token.recipient_address).substr(0, 14) << "..."
                  << " | Window: [" << p.token.valid_from << " -> " << p.token.valid_until << "]\n";
    } else if (std::holds_alternative<TokenRevokePayload>(tx.payload)) {
        const auto& p = std::get<TokenRevokePayload>(tx.payload);
        std::cout << "      • Revoked Token ID: " << hash_to_hex(p.target_token_id).substr(0, 20) << "...\n"
                  << "      • Reason: " << p.reason << "\n";
    } else if (std::holds_alternative<DecryptionAuditPayload>(tx.payload)) {
        const auto& p = std::get<DecryptionAuditPayload>(tx.payload);
        std::cout << "      • [DECRYPTION AUDIT] Accessor: " << address_to_hex(p.accessor_address).substr(0, 14) << "...\n"
                  << "      • Accessed Blob: " << hash_to_hex(p.blob_id).substr(0, 20) << "...\n"
                  << "      • Token Used: " << hash_to_hex(p.token_id).substr(0, 20) << "...\n";
    }
}

void trace_chain(Blockchain& chain) {
    uint64_t height = chain.get_chain_height();
    std::cout << "\n[Ledger Summary]\n";
    std::cout << "  Total Blocks: " << height << "\n";
    std::cout << "---------------------------------------------------------------\n";

    for (uint64_t i = 0; i < height; ++i) {
        auto b = chain.get_block_by_index(i);
        if (!b) continue;

        std::cout << "Block #" << std::setw(3) << b->header.index
                  << " | Hash: " << b->get_hash_hex().substr(0, 16) << "..."
                  << " | TxCount: " << std::setw(2) << b->transactions.size()
                  << " | Authority: " << b->header.authority_name
                  << " | Time: " << b->header.timestamp << "\n";

        for (const auto& tx : b->transactions) {
            print_transaction_details(tx);
        }
    }
    std::cout << "---------------------------------------------------------------\n\n";
}

void watch_chain(Blockchain& chain) {
    trace_chain(chain);
    uint64_t last_height = chain.get_chain_height();

    std::cout << "[*] Watching ledger in REAL-TIME for new blocks, transactions, and audit receipts...\n";
    std::cout << "    (Press Ctrl+C to stop realtime tracer)\n\n";

    while (g_tracer_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        uint64_t cur_height = chain.get_chain_height();
        if (cur_height > last_height) {
            for (uint64_t i = last_height; i < cur_height; ++i) {
                auto b = chain.get_block_by_index(i);
                if (!b) continue;

                std::cout << "\n>>> [REAL-TIME LEDGER EVENT] New Block Sealed! <<<\n";
                std::cout << "  Block #" << b->header.index
                          << " | Hash: " << b->get_hash_hex() << "\n"
                          << "  PrevHash : " << hash_to_hex(b->header.prev_hash).substr(0, 20) << "...\n"
                          << "  Merkle   : " << hash_to_hex(b->header.merkle_root).substr(0, 20) << "...\n"
                          << "  Authority: " << b->header.authority_name << "\n"
                          << "  Timestamp: " << b->header.timestamp << "\n"
                          << "  Transactions (" << b->transactions.size() << "):\n";

                for (const auto& tx : b->transactions) {
                    print_transaction_details(tx, "    └─ ");
                }
                std::cout << "---------------------------------------------------------------\n" << std::flush;
            }
            last_height = cur_height;
        }
    }
    std::cout << "\n[Tracer] Real-time session terminated cleanly.\n";
}

void trace_blob(Blockchain& chain, const std::string& blob_hex, bool follow = false) {
    Hash256 blob_id = hex_to_hash(blob_hex);
    auto blob_opt = chain.get_blob(blob_id);

    if (!blob_opt) {
        std::cerr << "[-] Error: Encrypted blob " << blob_hex << " not found on ledger.\n";
        return;
    }

    std::cout << "\n[Target Encrypted Blob Details]\n";
    std::cout << "  Blob ID        : " << hash_to_hex(blob_opt->blob_id) << "\n";
    std::cout << "  Previous Version: " << (is_zero_hash(blob_opt->previous_blob_id) ? "None (Genesis Version)" : hash_to_hex(blob_opt->previous_blob_id)) << "\n";
    std::cout << "  Owner Address  : " << address_to_hex(blob_opt->owner_address) << "\n";
    std::cout << "  Updater Address: " << address_to_hex(blob_opt->updater_address) << "\n";
    std::cout << "  Ciphertext Size: " << blob_opt->data.size() << " bytes\n";
    std::cout << "  IV (Hex)       : " << bytes_to_hex(blob_opt->iv.data(), blob_opt->iv.size(), false) << "\n";
    std::cout << "  Auth Tag (Hex) : " << bytes_to_hex(blob_opt->tag.data(), blob_opt->tag.size(), false) << "\n";
    std::cout << "  Timestamp      : " << blob_opt->timestamp << "\n";

    // Version history
    auto history = chain.get_blob_version_history(blob_id);
    std::cout << "\n--- Version Provenance Chain (" << history.size() << " revisions) ---\n";
    for (size_t i = 0; i < history.size(); ++i) {
        std::cout << "  v" << (history.size() - i) << ": " << hash_to_hex(history[i].blob_id)
                  << " (Updated by: " << address_to_hex(history[i].updater_address)
                  << " at t=" << history[i].timestamp << ")\n";
    }

    // Decryption Audits
    auto audits = chain.get_audits_for_blob(blob_id);
    std::cout << "\n--- On-Chain Decryption Audit Receipts (" << audits.size() << " access events) ---\n";
    if (audits.empty()) {
        std::cout << "  (No access requests recorded for this document)\n";
    } else {
        for (const auto& a : audits) {
            std::cout << "  [ACCESS EVENT] Accessor: " << address_to_hex(a.accessor_address)
                      << " | Token: " << hash_to_hex(a.token_id).substr(0, 16) << "..."
                      << " | Time: " << a.access_timestamp << "\n";
        }
    }
    std::cout << "\n";

    if (follow) {
        size_t last_audit_count = audits.size();
        std::cout << "[*] Watching document in REAL-TIME for access audits and revisions (Ctrl+C to stop)...\n";
        while (g_tracer_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto cur_audits = chain.get_audits_for_blob(blob_id);
            if (cur_audits.size() > last_audit_count) {
                for (size_t i = last_audit_count; i < cur_audits.size(); ++i) {
                    const auto& a = cur_audits[i];
                    std::cout << ">>> [REAL-TIME ACCESS AUDIT] Accessor: " << address_to_hex(a.accessor_address)
                              << " | Token: " << hash_to_hex(a.token_id).substr(0, 16) << "..."
                              << " | Time: " << a.access_timestamp << "\n" << std::flush;
                }
                last_audit_count = cur_audits.size();
            }
        }
        std::cout << "\n[Tracer] Stopped.\n";
    }
}

void trace_token(Blockchain& chain, const std::string& token_hex) {
    Hash256 token_id = hex_to_hash(token_hex);
    auto token_opt = chain.get_token(token_id);

    if (!token_opt) {
        std::cerr << "[-] Error: Temporal Access Token " << token_hex << " not found.\n";
        return;
    }

    std::cout << "\n[Temporal Access Token Information]\n";
    std::cout << "  Token ID       : " << hash_to_hex(token_opt->token_id) << "\n";
    std::cout << "  Target Blob ID : " << hash_to_hex(token_opt->target_blob_id) << "\n";
    std::cout << "  Grantor (Owner): " << address_to_hex(token_opt->grantor_address) << "\n";
    std::cout << "  Recipient      : " << address_to_hex(token_opt->recipient_address) << "\n";
    std::cout << "  Valid Window   : " << token_opt->valid_from << " -> " << token_opt->valid_until << "\n";
    std::cout << "  Delegation Parent: " << (is_zero_hash(token_opt->parent_token_id) ? "Root Grant (Direct from Patient)" : hash_to_hex(token_opt->parent_token_id)) << "\n";
    std::cout << "  Status Code    : " << (token_opt->status == 1 ? "1 (ACTIVE)" : (token_opt->status == 2 ? "2 (REVOKED)" : "3 (EXPIRED)")) << "\n\n";
}

void trace_tx(Blockchain& chain, const std::string& tx_hex) {
    Hash256 tx_id = hex_to_hash(tx_hex);
    auto tx_opt = chain.get_storage().get_transaction(tx_id);

    if (!tx_opt) {
        std::cerr << "[-] Error: Transaction " << tx_hex << " not found on chain.\n";
        return;
    }

    std::cout << "\n[Transaction Audit Details]\n";
    std::cout << "  Tx ID          : " << tx_opt->get_id_hex() << "\n";
    std::cout << "  Type           : " << tx_type_to_string(tx_opt->type) << "\n";
    std::cout << "  Sender         : " << address_to_hex(tx_opt->sender) << "\n";
    std::cout << "  Nonce          : " << tx_opt->nonce << "\n";
    std::cout << "  Timestamp      : " << tx_opt->timestamp << "\n";
    std::cout << "  Signatures     : " << tx_opt->signatures.size() << "\n\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, tracer_sig_handler);
    std::signal(SIGTERM, tracer_sig_handler);

    print_banner();

    std::string db_path = "sanjeev_node.db";
    std::string action;
    std::string target_id;
    bool follow = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--chain") {
            action = "chain";
        } else if (arg == "--blob" && i + 1 < argc) {
            action = "blob";
            target_id = argv[++i];
        } else if (arg == "--token" && i + 1 < argc) {
            action = "token";
            target_id = argv[++i];
        } else if (arg == "--tx" && i + 1 < argc) {
            action = "tx";
            target_id = argv[++i];
        } else if (arg == "--follow" || arg == "-f" || arg == "--watch") {
            follow = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (action.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    Blockchain chain;
    if (!chain.init(db_path)) {
        std::cerr << "[-] Failed to open SQLite database: " << db_path << "\n";
        return 1;
    }

    if (action == "chain") {
        if (follow) {
            watch_chain(chain);
        } else {
            trace_chain(chain);
        }
    } else if (action == "blob") {
        trace_blob(chain, target_id, follow);
    } else if (action == "token") {
        trace_token(chain, target_id);
    } else if (action == "tx") {
        trace_tx(chain, target_id);
    }

    return 0;
}
