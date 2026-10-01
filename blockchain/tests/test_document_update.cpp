#include "blockchain.hpp"
#include "crypto.hpp"
#include "binary_buffer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstdio>

using namespace Sanjeev;

// Simple CRC32 for standard ZIP creation
uint32_t compute_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// In-memory standard uncompressed ZIP generator
std::vector<uint8_t> create_test_zip(const std::vector<uint8_t>& image_bytes, const std::string& text_content) {
    struct FileEntry {
        std::string name;
        std::vector<uint8_t> content;
        uint32_t crc;
        uint32_t offset;
    };

    std::vector<FileEntry> files = {
        {"medical_scan.png", image_bytes, compute_crc32(image_bytes.data(), image_bytes.size()), 0},
        {"clinical_report.txt", std::vector<uint8_t>(text_content.begin(), text_content.end()),
         compute_crc32(reinterpret_cast<const uint8_t*>(text_content.data()), text_content.size()), 0}
    };

    BinaryWriter zip;

    // Write Local File Headers & Data
    for (auto& f : files) {
        f.offset = static_cast<uint32_t>(zip.size());
        zip.write_uint32(0x04034b50); // Signature
        zip.write_uint16(20);         // Version
        zip.write_uint16(0);          // Flags
        zip.write_uint16(0);          // Method (0 = Stored)
        zip.write_uint16(0);          // Time
        zip.write_uint16(0);          // Date
        zip.write_uint32(f.crc);
        zip.write_uint32(static_cast<uint32_t>(f.content.size()));
        zip.write_uint32(static_cast<uint32_t>(f.content.size()));
        zip.write_uint16(static_cast<uint16_t>(f.name.size()));
        zip.write_uint16(0);
        zip.write_bytes(reinterpret_cast<const uint8_t*>(f.name.data()), f.name.size());
        zip.write_bytes(f.content.data(), f.content.size());
    }

    uint32_t cd_start_offset = static_cast<uint32_t>(zip.size());

    // Central Directory Headers
    for (const auto& f : files) {
        zip.write_uint32(0x02014b50);
        zip.write_uint16(20);
        zip.write_uint16(20);
        zip.write_uint16(0);
        zip.write_uint16(0);
        zip.write_uint16(0);
        zip.write_uint16(0);
        zip.write_uint32(f.crc);
        zip.write_uint32(static_cast<uint32_t>(f.content.size()));
        zip.write_uint32(static_cast<uint32_t>(f.content.size()));
        zip.write_uint16(static_cast<uint16_t>(f.name.size()));
        zip.write_uint16(0);
        zip.write_uint16(0);
        zip.write_uint16(0);
        zip.write_uint16(0);
        zip.write_uint32(0);
        zip.write_uint32(f.offset);
        zip.write_bytes(reinterpret_cast<const uint8_t*>(f.name.data()), f.name.size());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_start_offset;

    // End of Central Directory
    zip.write_uint32(0x06054b50);
    zip.write_uint16(0);
    zip.write_uint16(0);
    zip.write_uint16(static_cast<uint16_t>(files.size()));
    zip.write_uint16(static_cast<uint16_t>(files.size()));
    zip.write_uint32(cd_size);
    zip.write_uint32(cd_start_offset);
    zip.write_uint16(0);

    return zip.release();
}

void test_document_update_workflow() {
    const std::string db_file = "test_doc_update.db";
    std::remove(db_file.c_str());

    std::cout << "\n==================================================================\n";
    std::cout << "  Sanjeev Test: Temporal Key Document Update & Provenance Audit\n";
    std::cout << "==================================================================\n";

    // 1. Initialize Blockchain
    Blockchain chain;
    assert(chain.init(db_file) == true);

    KeyPair auth_kp = Crypto::generate_ec_keypair();
    Address auth_addr = Crypto::derive_address_bytes(auth_kp.public_key_pem);
    chain.register_authority(auth_addr, "Ministry of Health Validator", auth_kp.public_key_pem);
    chain.create_genesis_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");

    // 2. Setup Participant 1 (Alice - Patient), Participant 2 (Dr. Bob - Doctor), and Mallory (Unauthorized)
    KeyPair alice_kp = Crypto::generate_ec_keypair();
    Address addr1 = Crypto::derive_address_bytes(alice_kp.public_key_pem);

    KeyPair bob_kp = Crypto::generate_ec_keypair();
    Address addr2 = Crypto::derive_address_bytes(bob_kp.public_key_pem);

    KeyPair mallory_kp = Crypto::generate_ec_keypair();
    Address addr3 = Crypto::derive_address_bytes(mallory_kp.public_key_pem);

    std::cout << "[Entities] Patient (addr1) : " << address_to_hex(addr1) << "\n";
    std::cout << "[Entities] Doctor  (addr2) : " << address_to_hex(addr2) << "\n";
    std::cout << "[Entities] Imposter(addr3) : " << address_to_hex(addr3) << "\n";

    // 3. Alice creates Version 1 of Document (ZIP with mock scan image + initial text)
    std::vector<uint8_t> mock_image_bytes = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG Header
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, // 128x128
        0x08, 0x06, 0x00, 0x00, 0x00, 0xC3, 0x3E, 0x61,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88  // Raw scan payload
    };

    std::string text_v1 = 
        "PATIENT RECORD #SANJ-2026-1188\n"
        "Patient: Alice Sharma | Age: 34\n"
        "Status : Initial consultation pending with Dr. Bob.\n";

    std::vector<uint8_t> zip_v1 = create_test_zip(mock_image_bytes, text_v1);
    std::vector<uint8_t> primary_key = Crypto::generate_random_bytes(32); // Patient's primary encryption key

    std::string raw_zip_v1_str(reinterpret_cast<const char*>(zip_v1.data()), zip_v1.size());
    EncryptedData enc_v1 = Crypto::encrypt_aes_gcm(raw_zip_v1_str, primary_key);

    EncryptedBlob blob_v1;
    blob_v1.blob_id = Crypto::sha256_digest(reinterpret_cast<const uint8_t*>(enc_v1.ciphertext_b64.data()), enc_v1.ciphertext_b64.size());
    blob_v1.previous_blob_id.fill(0); // Root version
    blob_v1.owner_address = addr1;
    blob_v1.updater_address = addr1;
    blob_v1.timestamp = 1000;
    blob_v1.iv = Crypto::from_base64(enc_v1.iv_b64);
    blob_v1.tag = Crypto::from_base64(enc_v1.tag_b64);
    blob_v1.data = Crypto::from_base64(enc_v1.ciphertext_b64);

    Transaction tx_store;
    tx_store.version = 1;
    tx_store.type = TxType::BLOB_STORE;
    tx_store.timestamp = 1000;
    tx_store.nonce = 1;
    tx_store.sender = addr1;
    tx_store.payload = BlobStorePayload{blob_v1};
    assert(chain.add_transaction(tx_store) == true);

    // Mine Block 1
    Block b1 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");
    assert(b1.header.index == 1);
    std::cout << "[Step 1] Alice stored Version 1 of record (Blob ID: " << hash_to_hex(blob_v1.blob_id) << ")\n";

    // 4. Alice issues Temporal Access Token to Dr. Bob (addr2) valid for [1000, 2000]
    Hash256 token_id = Crypto::sha256_digest(reinterpret_cast<const uint8_t*>("TOKEN-ALICE-BOB-CONSULT"), 23);
    TemporalAccessToken token;
    token.token_id = token_id;
    token.target_blob_id = blob_v1.blob_id;
    token.grantor_address = addr1;
    token.recipient_address = addr2;
    token.valid_from = 1000;
    token.valid_until = 2000;
    token.encrypted_symkey = primary_key;
    token.status = 1;

    Transaction tx_token;
    tx_token.version = 1;
    tx_token.type = TxType::TOKEN_GRANT;
    tx_token.timestamp = 1010;
    tx_token.nonce = 2;
    tx_token.sender = addr1;
    tx_token.payload = TokenGrantPayload{token};
    assert(chain.add_transaction(tx_token) == true);

    // Mine Block 2
    Block b2 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");
    assert(b2.header.index == 2);
    std::cout << "[Step 2] Alice granted Temporal Key to Dr. Bob (Token ID: " << hash_to_hex(token_id) << ")\n";

    // 5. Dr. Bob (addr2) requests decryption using his temporal token at t=1200
    std::vector<uint8_t> bob_retrieved_key;
    bool auth_res = chain.request_decryption(token_id, addr2, 1200, bob_retrieved_key);
    assert(auth_res == true);
    assert(bob_retrieved_key == primary_key);
    std::cout << "[Step 3] Dr. Bob accessed record using Temporal Key during consultation\n";

    // 6. Dr. Bob modifies the clinical report inside the ZIP
    EncryptedData dec_data_v1;
    dec_data_v1.ciphertext_b64 = Crypto::to_base64(blob_v1.data.data(), blob_v1.data.size());
    dec_data_v1.iv_b64 = Crypto::to_base64(blob_v1.iv.data(), blob_v1.iv.size());
    dec_data_v1.tag_b64 = Crypto::to_base64(blob_v1.tag.data(), blob_v1.tag.size());

    std::string decrypted_zip_v1 = Crypto::decrypt_aes_gcm(dec_data_v1, bob_retrieved_key);
    assert(decrypted_zip_v1.find("Initial consultation pending") != std::string::npos);

    // Dr. Bob appends his official diagnosis and prescription
    std::string text_v2 = text_v1 +
        "\n[PHYSICIAN CLINICAL EVALUATION - Dr. Bob, MD (addr2)]\n"
        "Date/Time   : 2026-10-01 12:00:00 UTC\n"
        "Findings    : Cardiac auscultation normal. Chest imaging clear.\n"
        "Prescription: Metoprolol 25mg BD, Lifestyle & diet counseling.\n"
        "Status      : REVIEW COMPLETED - DISCHARGED.\n";

    // Dr. Bob packages updated text and original scan into new ZIP (Version 2)
    std::vector<uint8_t> zip_v2 = create_test_zip(mock_image_bytes, text_v2);

    // Dr. Bob encrypts Version 2 using the primary symmetric key
    std::string raw_zip_v2_str(reinterpret_cast<const char*>(zip_v2.data()), zip_v2.size());
    EncryptedData enc_v2 = Crypto::encrypt_aes_gcm(raw_zip_v2_str, bob_retrieved_key);

    EncryptedBlob blob_v2;
    blob_v2.blob_id = Crypto::sha256_digest(reinterpret_cast<const uint8_t*>(enc_v2.ciphertext_b64.data()), enc_v2.ciphertext_b64.size());
    blob_v2.previous_blob_id = blob_v1.blob_id; // Explicit link to previous version
    blob_v2.owner_address = addr1;              // Owner remains Alice
    blob_v2.updater_address = addr2;            // Updater is Dr. Bob!
    blob_v2.timestamp = 1250;
    blob_v2.iv = Crypto::from_base64(enc_v2.iv_b64);
    blob_v2.tag = Crypto::from_base64(enc_v2.tag_b64);
    blob_v2.data = Crypto::from_base64(enc_v2.ciphertext_b64);

    // 7. Security Invariant Check: Unauthorized Mallory (addr3) tries to update Alice's record -> MUST FAIL
    Transaction tx_imposter_update;
    tx_imposter_update.version = 1;
    tx_imposter_update.type = TxType::BLOB_UPDATE;
    tx_imposter_update.timestamp = 1240;
    tx_imposter_update.nonce = 1;
    tx_imposter_update.sender = addr3; // Mallory
    tx_imposter_update.payload = BlobUpdatePayload{blob_v1.blob_id, blob_v2};
    assert(chain.add_transaction(tx_imposter_update) == false);
    std::cout << "[Security] Verified: Imposter addr3 without temporal key cannot update document (Rejected)\n";

    // 8. Authorized Dr. Bob submits BLOB_UPDATE transaction
    Transaction tx_doctor_update;
    tx_doctor_update.version = 1;
    tx_doctor_update.type = TxType::BLOB_UPDATE;
    tx_doctor_update.timestamp = 1250;
    tx_doctor_update.nonce = 1;
    tx_doctor_update.sender = addr2; // Dr. Bob
    tx_doctor_update.payload = BlobUpdatePayload{blob_v1.blob_id, blob_v2};
    assert(chain.add_transaction(tx_doctor_update) == true);

    // Mine Block 3
    Block b3 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");
    assert(b3.header.index == 3);
    std::cout << "[Step 4] Dr. Bob successfully committed Version 2 to Blockchain (Blob ID: " << hash_to_hex(blob_v2.blob_id) << ")\n";

    // 9. Alice (addr1) uses her primary key to get back the updated data from the blockchain
    auto alice_blobs = chain.get_blobs_by_owner(addr1);
    assert(alice_blobs.size() == 2); // Both v1 and v2 belong to Alice
    assert(alice_blobs[0].blob_id == blob_v2.blob_id); // Most recent version is v2
    assert(alice_blobs[0].updater_address == addr2);   // Updated by Dr. Bob!

    // Alice decrypts Version 2 using her primary key
    EncryptedData dec_data_v2;
    dec_data_v2.ciphertext_b64 = Crypto::to_base64(alice_blobs[0].data.data(), alice_blobs[0].data.size());
    dec_data_v2.iv_b64 = Crypto::to_base64(alice_blobs[0].iv.data(), alice_blobs[0].iv.size());
    dec_data_v2.tag_b64 = Crypto::to_base64(alice_blobs[0].tag.data(), alice_blobs[0].tag.size());

    std::string alice_decrypted_zip_v2 = Crypto::decrypt_aes_gcm(dec_data_v2, primary_key);

    // 10. Verify Alice sees the physician's updates & the original scan image
    // Verification A: Image bytes are identical
    std::string mock_image_str(reinterpret_cast<const char*>(mock_image_bytes.data()), mock_image_bytes.size());
    assert(alice_decrypted_zip_v2.find(mock_image_str) != std::string::npos);
    std::cout << "[Verification] Patient Alice verified scan image matches byte-for-byte in Version 2\n";

    // Verification B: Updated prescription & notes from Dr. Bob are present
    assert(alice_decrypted_zip_v2.find("PHYSICIAN CLINICAL EVALUATION - Dr. Bob") != std::string::npos);
    assert(alice_decrypted_zip_v2.find("Prescription: Metoprolol 25mg BD") != std::string::npos);
    assert(alice_decrypted_zip_v2.find("REVIEW COMPLETED - DISCHARGED") != std::string::npos);
    std::cout << "[Verification] Patient Alice verified Dr. Bob's clinical updates & prescription\n";

    // 11. Verify Version Provenance Lineage on-chain
    auto history = chain.get_blob_version_history(blob_v2.blob_id);
    assert(history.size() == 2);
    assert(history[0].blob_id == blob_v2.blob_id);
    assert(history[0].updater_address == addr2); // v2 updated by Bob
    assert(history[1].blob_id == blob_v1.blob_id);
    assert(history[1].updater_address == addr1); // v1 created by Alice
    std::cout << "[Provenance] On-chain version lineage verified: v2 (Bob) -> v1 (Alice)\n";

    chain.get_storage().close();
    std::remove(db_file.c_str());

    std::cout << "\n[PASS] test_document_update_workflow successfully completed!\n";
}

int main() {
    test_document_update_workflow();
    return 0;
}
