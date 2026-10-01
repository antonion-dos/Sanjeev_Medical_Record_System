#include "blockchain.hpp"
#include "crypto.hpp"
#include "binary_buffer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace Sanjeev;

// Simple CRC32 computation for ZIP file compliance
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

// In-memory standard uncompressed ZIP file generator
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

    // Write Local File Headers & Payload
    for (auto& f : files) {
        f.offset = static_cast<uint32_t>(zip.size());
        zip.write_uint32(0x04034b50); // Local file header signature (PK\x03\x04)
        zip.write_uint16(20);         // Version needed to extract (2.0)
        zip.write_uint16(0);          // General purpose bit flag
        zip.write_uint16(0);          // Compression method (0 = Stored/Uncompressed)
        zip.write_uint16(0);          // File last modification time
        zip.write_uint16(0);          // File last modification date
        zip.write_uint32(f.crc);      // CRC-32
        zip.write_uint32(static_cast<uint32_t>(f.content.size())); // Compressed size
        zip.write_uint32(static_cast<uint32_t>(f.content.size())); // Uncompressed size
        zip.write_uint16(static_cast<uint16_t>(f.name.size()));    // File name length
        zip.write_uint16(0);          // Extra field length
        zip.write_bytes(reinterpret_cast<const uint8_t*>(f.name.data()), f.name.size());
        zip.write_bytes(f.content.data(), f.content.size());
    }

    uint32_t cd_start_offset = static_cast<uint32_t>(zip.size());

    // Write Central Directory Headers
    for (const auto& f : files) {
        zip.write_uint32(0x02014b50); // Central directory file header (PK\x01\x02)
        zip.write_uint16(20);         // Version made by
        zip.write_uint16(20);         // Version needed to extract
        zip.write_uint16(0);          // General purpose bit flag
        zip.write_uint16(0);          // Compression method
        zip.write_uint16(0);          // Mod time
        zip.write_uint16(0);          // Mod date
        zip.write_uint32(f.crc);      // CRC-32
        zip.write_uint32(static_cast<uint32_t>(f.content.size()));
        zip.write_uint32(static_cast<uint32_t>(f.content.size()));
        zip.write_uint16(static_cast<uint16_t>(f.name.size()));
        zip.write_uint16(0);          // Extra field length
        zip.write_uint16(0);          // File comment length
        zip.write_uint16(0);          // Disk number start
        zip.write_uint16(0);          // Internal file attributes
        zip.write_uint32(0);          // External file attributes
        zip.write_uint32(f.offset);   // Relative offset of local header
        zip.write_bytes(reinterpret_cast<const uint8_t*>(f.name.data()), f.name.size());
    }

    uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_start_offset;

    // Write End of Central Directory Record (EOCD)
    zip.write_uint32(0x06054b50); // EOCD signature (PK\x05\x06)
    zip.write_uint16(0);          // Number of this disk
    zip.write_uint16(0);          // Disk where central directory starts
    zip.write_uint16(static_cast<uint16_t>(files.size())); // Number of central directory records on this disk
    zip.write_uint16(static_cast<uint16_t>(files.size())); // Total number of central directory records
    zip.write_uint32(cd_size);    // Size of central directory
    zip.write_uint32(cd_start_offset); // Offset of start of central directory
    zip.write_uint16(0);          // Comment length

    return zip.release();
}

void test_document_encryption_and_temporal_delegation() {
    const std::string db_file = "test_doc_enc.db";
    std::remove(db_file.c_str());

    std::cout << "--- Starting Document Encryption & Temporal Delegation Test ---\n";

    // 1. Initialize Blockchain
    Blockchain chain;
    assert(chain.init(db_file) == true);

    KeyPair auth_kp = Crypto::generate_ec_keypair();
    Address auth_addr = Crypto::derive_address_bytes(auth_kp.public_key_pem);
    chain.register_authority(auth_addr, "Ministry of Health Validator", auth_kp.public_key_pem);
    chain.create_genesis_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");

    // 2. Setup Participant 1 (Patient Alice) & Participant 2 (Doctor Bob)
    KeyPair alice_kp = Crypto::generate_ec_keypair();
    Address addr1 = Crypto::derive_address_bytes(alice_kp.public_key_pem);

    KeyPair bob_kp = Crypto::generate_ec_keypair();
    Address addr2 = Crypto::derive_address_bytes(bob_kp.public_key_pem);

    std::cout << "[Entities] Patient (addr1): " << address_to_hex(addr1) << "\n";
    std::cout << "[Entities] Doctor  (addr2): " << address_to_hex(addr2) << "\n";

    // 3. Build test ZIP file containing an image file (mock PNG bytes) and clinical notes txt
    std::vector<uint8_t> mock_image_bytes = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // Standard PNG Header
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR Chunk header
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, // 256x256 dimensions
        0x08, 0x06, 0x00, 0x00, 0x00, 0x5C, 0x72, 0xA8,
        0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED, 0xFA, 0xCE  // Mock image pixel payloads
    };

    std::string report_text = 
        "PATIENT RECORD #SANJ-2026-9041\n"
        "Patient: Alice Sharma | Age: 34\n"
        "Clinical Evaluation: Chest X-Ray and Ultrasound clear.\n"
        "Biomarkers: Troponin T normal (<0.01 ng/mL), Lipid panel nominal.\n"
        "Physician Sign-off: Dr. S. K. Mehta\n";

    std::vector<uint8_t> zip_archive = create_test_zip(mock_image_bytes, report_text);
    std::cout << "[Zip] Created test zip archive in memory (size: " << zip_archive.size() << " bytes)\n";
    assert(zip_archive.size() > 100);

    // 4. Patient Alice generates symmetric key and encrypts the ZIP archive with AES-256-GCM
    std::vector<uint8_t> symmetric_key = Crypto::generate_random_bytes(32); // 256-bit key
    std::string raw_zip_str(reinterpret_cast<const char*>(zip_archive.data()), zip_archive.size());
    EncryptedData enc = Crypto::encrypt_aes_gcm(raw_zip_str, symmetric_key);

    std::vector<uint8_t> ciphertext = Crypto::from_base64(enc.ciphertext_b64);
    std::vector<uint8_t> iv = Crypto::from_base64(enc.iv_b64);
    std::vector<uint8_t> tag = Crypto::from_base64(enc.tag_b64);

    Hash256 zip_hash = Crypto::sha256_digest(zip_archive);
    Hash256 blob_id = Crypto::sha256_digest(ciphertext);

    std::cout << "[Crypto] Encrypted zip archive using AES-256-GCM\n";
    std::cout << "  Original Zip Hash: " << hash_to_hex(zip_hash) << "\n";
    std::cout << "  Encrypted Blob ID: " << hash_to_hex(blob_id) << "\n";

    // 5. Embed encrypted blob into the blockchain via addr1
    EncryptedBlob blob;
    blob.blob_id = blob_id;
    blob.previous_blob_id.fill(0);
    blob.owner_address = addr1;
    blob.updater_address = addr1;
    blob.timestamp = 1000;
    blob.iv = iv;
    blob.tag = tag;
    blob.data = ciphertext;

    Transaction tx_store;
    tx_store.version = 1;
    tx_store.type = TxType::BLOB_STORE;
    tx_store.timestamp = 1000;
    tx_store.nonce = 1;
    tx_store.sender = addr1;
    tx_store.payload = BlobStorePayload{blob};
    assert(chain.add_transaction(tx_store) == true);

    // Mine Block 1
    Block b1 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");
    assert(b1.header.index == 1);
    std::cout << "[Blockchain] Block #1 mined containing EncryptedBlob\n";

    // 6. addr2 attempts to access the document WITHOUT a temporal key -> MUST FAIL
    Hash256 dummy_token_id{};
    dummy_token_id.fill(0xEE);
    std::vector<uint8_t> unauthorized_key;
    bool unauthorized_attempt = chain.request_decryption(dummy_token_id, addr2, 1050, unauthorized_key);
    assert(unauthorized_attempt == false);
    std::cout << "[Security] Verified: addr2 cannot access document without valid temporal key (Access Denied)\n";

    // 7. addr1 issues a Temporal Access Key to addr2 (valid from timestamp 1000 to 2000)
    Hash256 token_id = Crypto::sha256_digest(reinterpret_cast<const uint8_t*>("TOKEN-ALICE-TO-BOB-ZIP"), 22);
    TemporalAccessToken token;
    token.token_id = token_id;
    token.target_blob_id = blob_id;
    token.grantor_address = addr1;
    token.recipient_address = addr2;
    token.valid_from = 1000;
    token.valid_until = 2000;
    token.encrypted_symkey = symmetric_key; // In production encrypted under Bob's pubkey
    token.status = 1; // Active

    Transaction tx_token;
    tx_token.version = 1;
    tx_token.type = TxType::TOKEN_GRANT;
    tx_token.timestamp = 1005;
    tx_token.nonce = 2;
    tx_token.sender = addr1;
    tx_token.payload = TokenGrantPayload{token};
    assert(chain.add_transaction(tx_token) == true);

    // Mine Block 2 containing the Temporal Access Token
    Block b2 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");
    assert(b2.header.index == 2);
    std::cout << "[Blockchain] Block #2 mined containing Temporal Access Token\n";

    // 8. addr2 accesses the document using the Temporal Key at timestamp 1500 (within [1000, 2000])
    std::vector<uint8_t> retrieved_symkey;
    bool authorized_attempt = chain.request_decryption(token_id, addr2, 1500, retrieved_symkey);
    assert(authorized_attempt == true);
    assert(retrieved_symkey == symmetric_key);
    std::cout << "[Security] Verified: addr2 successfully verified on-chain and retrieved decryption key\n";

    // 9. addr2 decrypts the EncryptedBlob with the retrieved key and verifies contents byte-for-byte!
    auto retrieved_blob = chain.get_blob(blob_id);
    assert(retrieved_blob.has_value());

    EncryptedData dec_data;
    dec_data.ciphertext_b64 = Crypto::to_base64(retrieved_blob->data.data(), retrieved_blob->data.size());
    dec_data.iv_b64 = Crypto::to_base64(retrieved_blob->iv.data(), retrieved_blob->iv.size());
    dec_data.tag_b64 = Crypto::to_base64(retrieved_blob->tag.data(), retrieved_blob->tag.size());

    std::string decrypted_raw_zip = Crypto::decrypt_aes_gcm(dec_data, retrieved_symkey);
    std::vector<uint8_t> decrypted_zip(decrypted_raw_zip.begin(), decrypted_raw_zip.end());

    // Verify hash of decrypted archive matches original
    assert(Crypto::sha256_digest(decrypted_zip) == zip_hash);
    std::cout << "[Verification] Decrypted ZIP archive hash matches original digest perfectly!\n";

    // Byte-level verification of contents inside the ZIP
    // 1. Verify PNG image bytes exist in the decrypted stream
    std::string mock_image_str(reinterpret_cast<const char*>(mock_image_bytes.data()), mock_image_bytes.size());
    assert(decrypted_raw_zip.find(mock_image_str) != std::string::npos);
    std::cout << "[Verification] Image byte payload verified byte-for-byte inside decrypted archive!\n";

    // 2. Verify text report exists in the decrypted stream
    assert(decrypted_raw_zip.find(report_text) != std::string::npos);
    std::cout << "[Verification] Clinical text report verified character-for-character!\n";

    // 10. Mine Block 3 containing the on-chain decryption audit trail
    Block b3 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health Validator");
    assert(b3.header.index == 3);

    // Verify audit log exists on-chain
    auto audits = chain.get_audits_for_blob(blob_id);
    assert(audits.size() == 1);
    assert(audits[0].accessor_address == addr2);
    assert(audits[0].access_timestamp == 1500);
    assert(audits[0].token_id == token_id);
    std::cout << "[Audit Trail] Verified: on-chain decryption audit logged for addr2 at timestamp 1500\n";

    // 11. Test Temporal Expiration: addr2 tries to decrypt again at timestamp 2500 (past valid_until = 2000)
    std::vector<uint8_t> expired_key_attempt;
    bool expired_attempt = chain.request_decryption(token_id, addr2, 2500, expired_key_attempt);
    assert(expired_attempt == false);
    std::cout << "[Security] Verified: addr2 access denied after temporal expiration (t=2500 > valid_until=2000)\n";

    chain.get_storage().close();
    std::remove(db_file.c_str());

    std::cout << "[PASS] test_document_encryption_and_temporal_delegation successfully completed!\n";
}

int main() {
    test_document_encryption_and_temporal_delegation();
    return 0;
}
