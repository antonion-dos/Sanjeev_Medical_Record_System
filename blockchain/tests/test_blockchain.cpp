#include "blockchain.hpp"
#include "crypto.hpp"
#include <iostream>
#include <cassert>
#include <cstdio>

using namespace Sanjeev;

void test_full_blockchain_flow() {
    const std::string db_file = "test_chain.db";
    std::remove(db_file.c_str());

    Blockchain chain;
    assert(chain.init(db_file) == true);
    assert(chain.get_chain_height() == 0);

    // 1. Setup Authority
    KeyPair auth_kp = Crypto::generate_ec_keypair();
    Address auth_addr = Crypto::derive_address_bytes(auth_kp.public_key_pem);
    chain.register_authority(auth_addr, "Ministry of Health", auth_kp.public_key_pem);

    // 2. Genesis
    Block genesis = chain.create_genesis_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health");
    assert(genesis.header.index == 0);
    assert(chain.get_chain_height() == 1);

    // 3. Patient & Doctor Setup
    KeyPair patient_kp = Crypto::generate_ec_keypair();
    Address patient_addr = Crypto::derive_address_bytes(patient_kp.public_key_pem);

    KeyPair doctor_kp = Crypto::generate_ec_keypair();
    Address doctor_addr = Crypto::derive_address_bytes(doctor_kp.public_key_pem);

    // 4. Patient stores encrypted blob
    EncryptedBlob blob;
    blob.blob_id.fill(0x33);
    blob.previous_blob_id.fill(0x00);
    blob.owner_address = patient_addr;
    blob.updater_address = patient_addr;
    blob.timestamp = 1000;
    blob.iv = {1, 2, 3};
    blob.tag = {4, 5, 6};
    blob.data = {'E', 'H', 'R', '_', 'R', 'E', 'C', 'O', 'R', 'D'};

    Transaction tx_blob;
    tx_blob.version = 1;
    tx_blob.type = TxType::BLOB_STORE;
    tx_blob.timestamp = 1000;
    tx_blob.nonce = 1;
    tx_blob.sender = patient_addr;
    tx_blob.payload = BlobStorePayload{blob};
    assert(chain.add_transaction(tx_blob) == true);

    // 5. Patient issues temporal access token to Doctor (valid from 1000 to 2000)
    TemporalAccessToken token;
    token.token_id.fill(0x44);
    token.target_blob_id = blob.blob_id;
    token.grantor_address = patient_addr;
    token.recipient_address = doctor_addr;
    token.valid_from = 1000;
    token.valid_until = 2000;
    token.encrypted_symkey = {0xAA, 0xBB, 0xCC};
    token.status = 1;

    Transaction tx_token;
    tx_token.version = 1;
    tx_token.type = TxType::TOKEN_GRANT;
    tx_token.timestamp = 1000;
    tx_token.nonce = 2;
    tx_token.sender = patient_addr;
    tx_token.payload = TokenGrantPayload{token};
    assert(chain.add_transaction(tx_token) == true);

    // 6. Mine Block 1
    Block b1 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health");
    assert(b1.header.index == 1);
    assert(b1.transactions.size() == 2);
    assert(chain.get_chain_height() == 2);

    // 7. Doctor requests decryption at timestamp 1500 (within [1000, 2000])
    std::vector<uint8_t> decrypted_key;
    assert(chain.request_decryption(token.token_id, doctor_addr, 1500, decrypted_key) == true);
    assert(decrypted_key == token.encrypted_symkey);

    // Request from unauthorized party should fail
    Address imposter{};
    imposter.fill(0xFF);
    std::vector<uint8_t> imposter_key;
    assert(chain.request_decryption(token.token_id, imposter, 1500, imposter_key) == false);

    // Request expired (timestamp 2500) should fail
    std::vector<uint8_t> late_key;
    assert(chain.request_decryption(token.token_id, doctor_addr, 2500, late_key) == false);

    // 8. Mine Block 2 containing the on-chain decryption audit transaction
    assert(chain.get_mempool().size() == 1);
    Block b2 = chain.mine_block(auth_kp.private_key_pem, auth_kp.public_key_pem, "Ministry of Health");
    assert(b2.header.index == 2);
    assert(b2.transactions.size() == 1);
    assert(b2.transactions[0].type == TxType::DECRYPTION_AUDIT);
    assert(chain.get_chain_height() == 3);

    // 9. Verify audits query
    auto audits = chain.get_audits_for_blob(blob.blob_id);
    assert(audits.size() == 1);
    assert(audits[0].accessor_address == doctor_addr);
    assert(audits[0].access_timestamp == 1500);

    std::remove(db_file.c_str());
    std::cout << "[PASS] test_full_blockchain_flow" << std::endl;
}

int main() {
    std::cout << "Running Full Blockchain State & Consensus tests..." << std::endl;
    test_full_blockchain_flow();
    std::cout << "All Blockchain integration tests passed successfully!" << std::endl;
    return 0;
}
