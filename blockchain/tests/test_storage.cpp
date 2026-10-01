#include "storage.hpp"
#include "crypto.hpp"
#include <iostream>
#include <cassert>
#include <cstdio>

using namespace Sanjeev;

void test_storage_lifecycle() {
    const std::string db_file = "test_sanjeev.db";
    std::remove(db_file.c_str());

    Storage storage;
    assert(storage.open(db_file) == true);
    assert(storage.get_block_count() == 0);

    // Build Block 0 (Genesis)
    Block b0;
    b0.header.index = 0;
    b0.header.timestamp = 1000;
    b0.header.authority_address.fill(0xAA);
    b0.header.authority_name = "Genesis Authority";
    b0.header.merkle_root = b0.compute_merkle_root();

    assert(storage.save_block(b0) == true);
    assert(storage.get_block_count() == 1);

    // Build Block 1 with BlobStore & TokenGrant
    Block b1;
    b1.header.index = 1;
    b1.header.timestamp = 2000;
    b1.header.prev_hash = b0.calculate_hash();
    b1.header.authority_address.fill(0xAA);
    b1.header.authority_name = "Genesis Authority";

    Address patient{};
    patient.fill(0x11);
    Address hospital{};
    hospital.fill(0x22);
    Address doctor{};
    doctor.fill(0x33);

    // Tx 1: Store Blob
    Transaction tx1;
    tx1.version = 1;
    tx1.type = TxType::BLOB_STORE;
    tx1.timestamp = 2000;
    tx1.nonce = 1;
    tx1.sender = patient;

    EncryptedBlob blob1;
    blob1.blob_id.fill(0x01);
    blob1.previous_blob_id.fill(0x00);
    blob1.owner_address = patient;
    blob1.updater_address = patient;
    blob1.timestamp = 2000;
    blob1.iv = {1, 2, 3};
    blob1.tag = {4, 5, 6};
    blob1.data = {'S', 'E', 'C', 'R', 'E', 'T'};
    tx1.payload = BlobStorePayload{blob1};
    b1.transactions.push_back(tx1);

    // Tx 2: Grant Temporal Key to Hospital
    Transaction tx2;
    tx2.version = 1;
    tx2.type = TxType::TOKEN_GRANT;
    tx2.timestamp = 2005;
    tx2.nonce = 2;
    tx2.sender = patient;

    TemporalAccessToken token1;
    token1.token_id.fill(0x10);
    token1.target_blob_id = blob1.blob_id;
    token1.grantor_address = patient;
    token1.recipient_address = hospital;
    token1.valid_from = 2000;
    token1.valid_until = 5000;
    token1.encrypted_symkey = {0xDE, 0xAD};
    token1.status = 1; // Active
    tx2.payload = TokenGrantPayload{token1};
    b1.transactions.push_back(tx2);

    b1.header.merkle_root = b1.compute_merkle_root();
    assert(storage.save_block(b1) == true);
    assert(storage.get_block_count() == 2);

    // Block 2: Delegate Key to Doctor & Record Decryption Audit
    Block b2;
    b2.header.index = 2;
    b2.header.timestamp = 3000;
    b2.header.prev_hash = b1.calculate_hash();
    b2.header.authority_address.fill(0xAA);
    b2.header.authority_name = "Genesis Authority";

    // Tx 3: Hospital delegates to Doctor
    Transaction tx3;
    tx3.version = 1;
    tx3.type = TxType::TOKEN_DELEGATE;
    tx3.timestamp = 3000;
    tx3.nonce = 1;
    tx3.sender = hospital;

    TemporalAccessToken child_token;
    child_token.token_id.fill(0x20);
    child_token.target_blob_id = blob1.blob_id;
    child_token.grantor_address = hospital;
    child_token.recipient_address = doctor;
    child_token.valid_from = 3000;
    child_token.valid_until = 4000;
    child_token.parent_token_id = token1.token_id;
    child_token.encrypted_symkey = {0xBE, 0xEF};
    child_token.status = 1;
    tx3.payload = TokenDelegatePayload{token1.token_id, child_token};
    b2.transactions.push_back(tx3);

    // Tx 4: Doctor records Decryption Audit
    Transaction tx4;
    tx4.version = 1;
    tx4.type = TxType::DECRYPTION_AUDIT;
    tx4.timestamp = 3100;
    tx4.nonce = 1;
    tx4.sender = doctor;
    tx4.payload = DecryptionAuditPayload{child_token.token_id, blob1.blob_id, doctor, 3100};
    b2.transactions.push_back(tx4);

    b2.header.merkle_root = b2.compute_merkle_root();
    assert(storage.save_block(b2) == true);
    assert(storage.get_block_count() == 3);

    // Test Point Queries
    auto b1_rec = storage.get_block_by_index(1);
    assert(b1_rec.has_value());
    assert(b1_rec->header.index == 1);
    assert(b1_rec->transactions.size() == 2);

    auto latest = storage.get_latest_block();
    assert(latest.has_value());
    assert(latest->header.index == 2);

    auto blob_rec = storage.get_blob(blob1.blob_id);
    assert(blob_rec.has_value());
    assert(blob_rec->data == blob1.data);
    assert(blob_rec->owner_address == patient);

    auto token_rec = storage.get_token(child_token.token_id);
    assert(token_rec.has_value());
    assert(token_rec->recipient_address == doctor);
    assert(token_rec->parent_token_id == token1.token_id);

    // Active tokens check at timestamp 3500 (within [3000, 4000])
    auto active_tokens = storage.get_active_tokens_for_recipient(doctor, 3500);
    assert(active_tokens.size() == 1);
    assert(active_tokens[0].token_id == child_token.token_id);

    // Expired check at timestamp 4500 (outside [3000, 4000])
    auto expired_check = storage.get_active_tokens_for_recipient(doctor, 4500);
    assert(expired_check.empty());

    // Audits check
    auto audits = storage.get_audits_for_blob(blob1.blob_id);
    assert(audits.size() == 1);
    assert(audits[0].accessor_address == doctor);
    assert(audits[0].access_timestamp == 3100);

    // Test persistence across restart
    storage.close();

    Storage storage2;
    assert(storage2.open(db_file) == true);
    assert(storage2.get_block_count() == 3);
    auto b2_rec = storage2.get_block_by_index(2);
    assert(b2_rec.has_value());
    assert(b2_rec->header.prev_hash == b1.calculate_hash());

    storage2.close();
    std::remove(db_file.c_str());

    std::cout << "[PASS] test_storage_lifecycle" << std::endl;
}

int main() {
    std::cout << "Running SQLite Persistence unit tests..." << std::endl;
    test_storage_lifecycle();
    std::cout << "All SQLite persistence tests passed successfully!" << std::endl;
    return 0;
}
