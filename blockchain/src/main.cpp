#include "blockchain.hpp"
#include "server.hpp"
#include "crypto.hpp"
#include "json_utils.hpp"

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
    std::string authority_name = "Government Ministry of Health - Authority Validator #1";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--name" && i + 1 < argc) {
            authority_name = argv[++i];
        }
    }

    std::cout << "====================================================\n";
    std::cout << "  Sanjeev (संजीव) - Decentralized Health Ledger\n";
    std::cout << "====================================================\n";

    Sanjeev::Blockchain blockchain;

    // 1. Generate Institutional Government Authority Validator Keypair
    std::cout << "[Consensus] Initializing Proof-of-Authority validator credentials...\n";
    Sanjeev::KeyPair auth_kp = Sanjeev::Crypto::generate_ec_keypair();
    std::cout << "  Authority Name   : " << authority_name << "\n";
    std::cout << "  Authority Address: " << auth_kp.address << "\n";

    // 2. Create Genesis Block sealed by Government Authority
    std::cout << "[Blockchain] Generating Genesis Block...\n";
    Sanjeev::Block genesis = blockchain.create_genesis_block(auth_kp.private_key_pem, auth_kp.public_key_pem, authority_name);
    std::cout << "  Genesis Hash     : " << genesis.hash << "\n";

    // 3. Pre-seed demo entities to make the system instantly testable
    std::cout << "[Seed] Generating initial demo participants...\n";

    // Patient Alice
    Sanjeev::KeyPair alice_kp = Sanjeev::Crypto::generate_ec_keypair();
    // Apollo City Hospital
    Sanjeev::KeyPair hospital_kp = Sanjeev::Crypto::generate_ec_keypair();
    // Dr. Rajesh Sharma (Cardiologist under Apollo Hospital)
    Sanjeev::KeyPair doctor_kp = Sanjeev::Crypto::generate_ec_keypair();

    std::cout << "  Patient (Alice)   : " << alice_kp.address << "\n";
    std::cout << "  Hospital (Apollo) : " << hospital_kp.address << "\n";
    std::cout << "  Doctor (Dr. Rajesh): " << doctor_kp.address << "\n";

    // 4. Create encrypted medical document for Alice
    std::string sample_medical_record = 
        "PATIENT: Alice Sharma | AGE: 34 | BLOOD GROUP: B+\n"
        "DIAGNOSIS: Mild Cardiac Arrhythmia & Hypercholesterolemia\n"
        "PRESCRIPTION: Atorvastatin 10mg OD, Metoprolol 25mg BD\n"
        "NOTES: Patient exhibits episodic tachycardia after exertion. ECG shows normal sinus rhythm with occasional PACs.\n"
        "FOLLOW-UP: 4 weeks with lipid profile and ambulatory Holter monitor.";

    std::vector<uint8_t> document_sym_key = Sanjeev::Crypto::generate_random_bytes(32);
    Sanjeev::EncryptedData enc_doc = Sanjeev::Crypto::encrypt_aes_gcm(sample_medical_record, document_sym_key);
    std::string record_hash = Sanjeev::Crypto::sha256(sample_medical_record);

    Sanjeev::JsonValue doc_payload = Sanjeev::JsonValue::object();
    doc_payload["title"] = "Cardiology Evaluation & Lab Panel";
    doc_payload["patient_name"] = "Alice Sharma";
    doc_payload["department"] = "Cardiology";
    doc_payload["hospital_issuer"] = "Apollo City Hospital";
    doc_payload["ciphertext"] = enc_doc.ciphertext_b64;
    doc_payload["iv"] = enc_doc.iv_b64;
    doc_payload["tag"] = enc_doc.tag_b64;
    doc_payload["algorithm"] = enc_doc.algorithm;
    doc_payload["document_sym_key_hex"] = Sanjeev::Crypto::to_hex(document_sym_key.data(), document_sym_key.size());

    Sanjeev::Transaction rec_tx;
    rec_tx.type = Sanjeev::TxType::RECORD_STORE;
    rec_tx.sender = alice_kp.address;
    rec_tx.recipient = "0x0000000000000000000000000000000000000000";
    rec_tx.timestamp = Sanjeev::Crypto::current_timestamp();
    rec_tx.record_hash = record_hash;
    rec_tx.payload = doc_payload.dump();
    rec_tx.required_signatures = 1;
    rec_tx.tx_id = rec_tx.calculate_hash();
    rec_tx.add_signature(alice_kp.private_key_pem, alice_kp.public_key_pem);

    blockchain.add_transaction(rec_tx);

    // 5. Patient Alice grants a 24-Hour Temporal Key to Apollo Hospital
    uint64_t now = Sanjeev::Crypto::current_timestamp();
    uint64_t valid_until_24h = now + 86400; // 24 hours

    Sanjeev::JsonValue key_payload = Sanjeev::JsonValue::object();
    key_payload["grant_purpose"] = "Outpatient Cardiology Consultation";
    key_payload["authorized_document_sym_key_hex"] = Sanjeev::Crypto::to_hex(document_sym_key.data(), document_sym_key.size());

    Sanjeev::Transaction grant_tx;
    grant_tx.type = Sanjeev::TxType::TEMPORAL_KEY_GRANT;
    grant_tx.sender = alice_kp.address;
    grant_tx.recipient = hospital_kp.address;
    grant_tx.timestamp = now;
    grant_tx.valid_from = now;
    grant_tx.valid_until = valid_until_24h;
    grant_tx.record_hash = record_hash;
    grant_tx.payload = key_payload.dump();
    grant_tx.required_signatures = 1;
    grant_tx.tx_id = grant_tx.calculate_hash();
    grant_tx.add_signature(alice_kp.private_key_pem, alice_kp.public_key_pem);

    blockchain.add_transaction(grant_tx);

    // 6. Apollo Hospital delegates the Temporal Key to Dr. Rajesh Sharma (valid for 12 hours)
    uint64_t valid_until_12h = now + 43200; // 12 hours <= 24 hours
    Sanjeev::JsonValue del_payload = Sanjeev::JsonValue::object();
    del_payload["doctor_name"] = "Dr. Rajesh Sharma, MD";
    del_payload["specialty"] = "Cardiology";
    del_payload["authorized_document_sym_key_hex"] = Sanjeev::Crypto::to_hex(document_sym_key.data(), document_sym_key.size());

    Sanjeev::Transaction del_tx;
    del_tx.type = Sanjeev::TxType::TEMPORAL_KEY_DELEGATE;
    del_tx.sender = hospital_kp.address;
    del_tx.recipient = doctor_kp.address;
    del_tx.parent_tx_id = grant_tx.tx_id;
    del_tx.timestamp = now;
    del_tx.valid_from = now;
    del_tx.valid_until = valid_until_12h;
    del_tx.record_hash = record_hash;
    del_tx.payload = del_payload.dump();
    del_tx.required_signatures = 1;
    del_tx.tx_id = del_tx.calculate_hash();
    del_tx.add_signature(hospital_kp.private_key_pem, hospital_kp.public_key_pem);

    blockchain.add_transaction(del_tx);

    // Mine seed block by Government Authority
    std::cout << "[Consensus] Mining initial block with records and temporal keys...\n";
    blockchain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, authority_name);
    std::cout << "  Block #1 sealed successfully! Height: " << blockchain.get_chain_length() << "\n";

    // 7. Start HTTP REST Server
    Sanjeev::HttpServer server(blockchain, port);
    server.set_authority_credentials(auth_kp.private_key_pem, auth_kp.public_key_pem, authority_name);
    server.start();

    std::cout << "\n[Ready] Sanjeev Blockchain Node is online at http://127.0.0.1:" << port << "\n";
    std::cout << "        Press Ctrl+C to stop node.\n\n";

    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[Shutdown] Stopping node cleanly...\n";
    server.stop();
    std::cout << "[Shutdown] Completed.\n";

    return 0;
}
