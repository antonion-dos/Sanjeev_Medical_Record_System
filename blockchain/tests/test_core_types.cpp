#include "binary_buffer.hpp"
#include "types.hpp"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace Sanjeev;

void test_binary_buffer() {
    BinaryWriter writer;
    writer.write_uint8(0x42);
    writer.write_uint16(0x1234);
    writer.write_uint32(0xDEADBEEF);
    writer.write_uint64(0x0123456789ABCDEFULL);
    writer.write_string("Sanjeev Decentralized Health");

    std::vector<uint8_t> raw = {0xAA, 0xBB, 0xCC, 0xDD};
    writer.write_var_bytes(raw);

    Hash256 test_hash{};
    test_hash.fill(0x7F);
    writer.write_fixed_array(test_hash);

    BinaryReader reader(writer.get_buffer());
    assert(reader.read_uint8() == 0x42);
    assert(reader.read_uint16() == 0x1234);
    assert(reader.read_uint32() == 0xDEADBEEF);
    assert(reader.read_uint64() == 0x0123456789ABCDEFULL);
    assert(reader.read_string() == "Sanjeev Decentralized Health");
    assert(reader.read_var_bytes() == raw);
    assert(reader.read_fixed_array<32>() == test_hash);
    assert(reader.remaining() == 0);

    std::cout << "[PASS] test_binary_buffer" << std::endl;
}

void test_encrypted_blob() {
    EncryptedBlob blob;
    blob.blob_id.fill(0x01);
    blob.previous_blob_id.fill(0x00);
    blob.owner_address.fill(0x11);
    blob.updater_address.fill(0x22);
    blob.timestamp = 1743510000;
    blob.iv = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    blob.tag = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    blob.data = {'E', 'N', 'C', 'R', 'Y', 'P', 'T', 'E', 'D', '_', 'D', 'A', 'T', 'A'};

    BinaryWriter w;
    blob.serialize(w);

    BinaryReader r(w.get_buffer());
    EncryptedBlob recovered = EncryptedBlob::deserialize(r);

    assert(recovered.blob_id == blob.blob_id);
    assert(recovered.previous_blob_id == blob.previous_blob_id);
    assert(recovered.owner_address == blob.owner_address);
    assert(recovered.updater_address == blob.updater_address);
    assert(recovered.timestamp == blob.timestamp);
    assert(recovered.iv == blob.iv);
    assert(recovered.tag == blob.tag);
    assert(recovered.data == blob.data);
    assert(r.remaining() == 0);

    std::cout << "[PASS] test_encrypted_blob" << std::endl;
}

void test_temporal_access_token() {
    TemporalAccessToken token;
    token.token_id.fill(0xAA);
    token.target_blob_id.fill(0xBB);
    token.grantor_address.fill(0x11);
    token.recipient_address.fill(0x22);
    token.valid_from = 1000;
    token.valid_until = 2000;
    token.parent_token_id.fill(0x00);
    token.encrypted_symkey = {0x01, 0x02, 0x03, 0x04};
    token.status = 1; // Active

    assert(token.is_temporally_valid(1500) == true);
    assert(token.is_temporally_valid(999) == false);
    assert(token.is_temporally_valid(2001) == false);

    BinaryWriter w;
    token.serialize(w);

    BinaryReader r(w.get_buffer());
    TemporalAccessToken recovered = TemporalAccessToken::deserialize(r);

    assert(recovered.token_id == token.token_id);
    assert(recovered.target_blob_id == token.target_blob_id);
    assert(recovered.grantor_address == token.grantor_address);
    assert(recovered.recipient_address == token.recipient_address);
    assert(recovered.valid_from == token.valid_from);
    assert(recovered.valid_until == token.valid_until);
    assert(recovered.encrypted_symkey == token.encrypted_symkey);
    assert(recovered.status == token.status);
    assert(r.remaining() == 0);

    std::cout << "[PASS] test_temporal_access_token" << std::endl;
}

void test_decryption_audit_payload() {
    DecryptionAuditPayload audit;
    audit.token_id.fill(0x12);
    audit.blob_id.fill(0x34);
    audit.accessor_address.fill(0x56);
    audit.access_timestamp = 1743519999;

    BinaryWriter w;
    audit.serialize(w);

    BinaryReader r(w.get_buffer());
    DecryptionAuditPayload rec = DecryptionAuditPayload::deserialize(r);

    assert(rec.token_id == audit.token_id);
    assert(rec.blob_id == audit.blob_id);
    assert(rec.accessor_address == audit.accessor_address);
    assert(rec.access_timestamp == audit.access_timestamp);
    assert(r.remaining() == 0);

    std::cout << "[PASS] test_decryption_audit_payload" << std::endl;
}

int main() {
    std::cout << "Running Core Types & Binary Buffer unit tests..." << std::endl;
    test_binary_buffer();
    test_encrypted_blob();
    test_temporal_access_token();
    test_decryption_audit_payload();
    std::cout << "All core data structure tests passed successfully!" << std::endl;
    return 0;
}
