#include "blockchain.hpp"
#include "crypto.hpp"
#include <iostream>
#include <iomanip>
#include <string>

using namespace Sanjeev;

void print_banner() {
    std::cout << "===============================================================\n";
    std::cout << "   Sanjeev (संजीव) - Blockchain Transaction & Lineage Tracer   \n";
    std::cout << "===============================================================\n";
}

void print_usage(const char* prog) {
    std::cout << "Usage:\n";
    std::cout << "  " << prog << " --chain [--db <path>]\n";
    std::cout << "  " << prog << " --blob <blob_id_hex> [--db <path>]\n";
    std::cout << "  " << prog << " --token <token_id_hex> [--db <path>]\n";
    std::cout << "  " << prog << " --tx <tx_id_hex> [--db <path>]\n";
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
            std::cout << "   └─ [Tx " << tx_type_to_string(tx.type) << "] ID: "
                      << tx.get_id_hex().substr(0, 16) << "..."
                      << " Sender: " << address_to_hex(tx.sender).substr(0, 12) << "...\n";
        }
    }
    std::cout << "---------------------------------------------------------------\n\n";
}

void trace_blob(Blockchain& chain, const std::string& blob_hex) {
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
    print_banner();

    std::string db_path = "sanjeev_node.db";
    std::string action;
    std::string target_id;

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
        trace_chain(chain);
    } else if (action == "blob") {
        trace_blob(chain, target_id);
    } else if (action == "token") {
        trace_token(chain, target_id);
    } else if (action == "tx") {
        trace_tx(chain, target_id);
    }

    return 0;
}
