#include "blockchain.hpp"
#include "server.hpp"
#include "crypto.hpp"

#include <iostream>
#include <csignal>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

namespace {
    std::atomic<bool> g_shutdown{false};
    void signal_handler(int) {
        g_shutdown = true;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int port = 8080;
    std::string db_file = "sanjeev_node.db";
    std::string authority_name = "Government Ministry of Health - Authority Validator #1";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--db" && i + 1 < argc) {
            db_file = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            authority_name = argv[++i];
        }
    }

    std::cout << "====================================================\n";
    std::cout << "  Sanjeev (संजीव) - Decentralized Health Ledger\n";
    std::cout << "====================================================\n";

    Sanjeev::Blockchain blockchain;
    if (!blockchain.init(db_file)) {
        std::cerr << "Failed to initialize SQLite storage engine: " << db_file << "\n";
        return 1;
    }
    std::cout << "[Storage] Connected to SQLite database: " << db_file << "\n";

    // 1. Generate / Configure Institutional Authority Keypair
    std::cout << "[Consensus] Initializing Proof-of-Authority validator credentials...\n";
    Sanjeev::KeyPair auth_kp = Sanjeev::Crypto::generate_ec_keypair();
    Sanjeev::Address auth_addr = Sanjeev::Crypto::derive_address_bytes(auth_kp.public_key_pem);
    std::cout << "  Authority Name   : " << authority_name << "\n";
    std::cout << "  Authority Address: " << Sanjeev::address_to_hex(auth_addr) << "\n";

    // 2. Create Genesis Block if chain is empty
    if (blockchain.get_chain_height() == 0) {
        std::cout << "[Blockchain] Generating Genesis Block...\n";
        Sanjeev::Block genesis = blockchain.create_genesis_block(auth_kp.private_key_pem, auth_kp.public_key_pem, authority_name);
        std::cout << "  Genesis Hash     : " << genesis.get_hash_hex() << "\n";

        // Pre-seed demo entities
        std::cout << "[Seed] Generating initial demo participants...\n";
        Sanjeev::KeyPair alice_kp = Sanjeev::Crypto::generate_ec_keypair();
        Sanjeev::Address alice_addr = Sanjeev::Crypto::derive_address_bytes(alice_kp.public_key_pem);

        Sanjeev::KeyPair hospital_kp = Sanjeev::Crypto::generate_ec_keypair();
        Sanjeev::Address hospital_addr = Sanjeev::Crypto::derive_address_bytes(hospital_kp.public_key_pem);

        Sanjeev::KeyPair doctor_kp = Sanjeev::Crypto::generate_ec_keypair();
        Sanjeev::Address doctor_addr = Sanjeev::Crypto::derive_address_bytes(doctor_kp.public_key_pem);

        std::cout << "  Patient (Alice)   : " << Sanjeev::address_to_hex(alice_addr) << "\n";
        std::cout << "  Hospital (Apollo) : " << Sanjeev::address_to_hex(hospital_addr) << "\n";
        std::cout << "  Doctor (Dr. Rajesh): " << Sanjeev::address_to_hex(doctor_addr) << "\n";

        // Store initial Encrypted Medical Record Blob
        std::string sample_medical_record = 
            "PATIENT: Alice Sharma | AGE: 34 | BLOOD GROUP: B+\n"
            "DIAGNOSIS: Mild Cardiac Arrhythmia & Hypercholesterolemia\n"
            "PRESCRIPTION: Atorvastatin 10mg OD, Metoprolol 25mg BD\n"
            "NOTES: Normal sinus rhythm with occasional PACs.\n";

        std::vector<uint8_t> doc_sym_key = Sanjeev::Crypto::generate_random_bytes(32);
        Sanjeev::EncryptedData enc = Sanjeev::Crypto::encrypt_aes_gcm(sample_medical_record, doc_sym_key);

        Sanjeev::EncryptedBlob blob;
        blob.blob_id = Sanjeev::Crypto::sha256_digest(reinterpret_cast<const uint8_t*>(sample_medical_record.data()), sample_medical_record.size());
        blob.previous_blob_id.fill(0);
        blob.owner_address = alice_addr;
        blob.updater_address = alice_addr;
        blob.timestamp = Sanjeev::Crypto::current_timestamp();
        blob.iv = Sanjeev::Crypto::from_base64(enc.iv_b64);
        blob.tag = Sanjeev::Crypto::from_base64(enc.tag_b64);
        blob.data = Sanjeev::Crypto::from_base64(enc.ciphertext_b64);

        Sanjeev::Transaction tx_blob;
        tx_blob.version = 1;
        tx_blob.type = Sanjeev::TxType::BLOB_STORE;
        tx_blob.timestamp = blob.timestamp;
        tx_blob.nonce = 1;
        tx_blob.sender = alice_addr;
        tx_blob.payload = Sanjeev::BlobStorePayload{blob};
        blockchain.add_transaction(tx_blob);

        // Patient Alice grants 24-hour temporal key to Apollo Hospital
        uint64_t now = Sanjeev::Crypto::current_timestamp();
        Sanjeev::TemporalAccessToken token;
        token.token_id.fill(0x7A);
        token.target_blob_id = blob.blob_id;
        token.grantor_address = alice_addr;
        token.recipient_address = hospital_addr;
        token.valid_from = now;
        token.valid_until = now + 86400; // 24 hours
        token.encrypted_symkey = doc_sym_key;
        token.status = 1;

        Sanjeev::Transaction tx_token;
        tx_token.version = 1;
        tx_token.type = Sanjeev::TxType::TOKEN_GRANT;
        tx_token.timestamp = now;
        tx_token.nonce = 2;
        tx_token.sender = alice_addr;
        tx_token.payload = Sanjeev::TokenGrantPayload{token};
        blockchain.add_transaction(tx_token);

        // Mine Block 1
        std::cout << "[Consensus] Mining initial block with demo records & keys...\n";
        Sanjeev::Block b1 = blockchain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, authority_name);
        std::cout << "  Block #1 sealed! Current height: " << blockchain.get_chain_height() << "\n";
    } else {
        std::cout << "[Blockchain] Resumed existing chain. Height: " << blockchain.get_chain_height() << "\n";
    }

    // 3. Start HTTP REST Server
    Sanjeev::HttpServer server(blockchain, port);
    server.set_authority_credentials(auth_kp.private_key_pem, auth_kp.public_key_pem, authority_name);
    server.start();

    std::cout << "\n[Ready] Sanjeev Blockchain Node is online at http://127.0.0.1:" << port << "\n";
    std::cout << "        API documentation: blockchain/API.md\n";
    std::cout << "        Press Ctrl+C to terminate node.\n\n";

    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[Shutdown] Stopping node cleanly...\n";
    server.stop();
    std::cout << "[Shutdown] Completed.\n";

    return 0;
}
