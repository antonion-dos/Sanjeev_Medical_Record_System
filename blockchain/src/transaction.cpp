#include "transaction.hpp"
#include "crypto.hpp"
#include <stdexcept>

namespace Sanjeev {

void Transaction::serialize_unsigned(BinaryWriter& w) const {
    w.write_uint32(version);
    w.write_uint8(static_cast<uint8_t>(type));
    w.write_uint64(timestamp);
    w.write_uint64(nonce);
    w.write_fixed_array(sender);
    w.write_uint32(required_signatures);

    std::visit([&w](const auto& p) {
        p.serialize(w);
    }, payload);
}

void Transaction::serialize(BinaryWriter& w) const {
    serialize_unsigned(w);
    w.write_uint32(static_cast<uint32_t>(signatures.size()));
    for (const auto& sig : signatures) {
        sig.serialize(w);
    }
}

Transaction Transaction::deserialize(BinaryReader& r) {
    Transaction tx;
    tx.version = r.read_uint32();
    uint8_t type_byte = r.read_uint8();
    tx.type = static_cast<TxType>(type_byte);
    tx.timestamp = r.read_uint64();
    tx.nonce = r.read_uint64();
    tx.sender = r.read_fixed_array<20>();
    tx.required_signatures = r.read_uint32();

    switch (tx.type) {
        case TxType::BLOB_STORE:
            tx.payload = BlobStorePayload::deserialize(r);
            break;
        case TxType::BLOB_UPDATE:
            tx.payload = BlobUpdatePayload::deserialize(r);
            break;
        case TxType::TOKEN_GRANT:
            tx.payload = TokenGrantPayload::deserialize(r);
            break;
        case TxType::TOKEN_DELEGATE:
            tx.payload = TokenDelegatePayload::deserialize(r);
            break;
        case TxType::TOKEN_REVOKE:
            tx.payload = TokenRevokePayload::deserialize(r);
            break;
        case TxType::DECRYPTION_AUDIT:
            tx.payload = DecryptionAuditPayload::deserialize(r);
            break;
        case TxType::AUTHORITY_SET:
            tx.payload = AuthoritySetPayload::deserialize(r);
            break;
        default:
            throw std::runtime_error("Unknown transaction type during deserialization: " +
                                     std::to_string(type_byte));
    }

    uint32_t sig_count = r.read_uint32();
    tx.signatures.reserve(sig_count);
    for (uint32_t i = 0; i < sig_count; ++i) {
        tx.signatures.push_back(SignatureEntry::deserialize(r));
    }

    return tx;
}

Hash256 Transaction::calculate_id() const {
    BinaryWriter w;
    serialize_unsigned(w);
    return Crypto::sha256_digest(w.get_buffer());
}

bool Transaction::is_temporally_valid(uint64_t current_time) const {
    if (std::holds_alternative<TokenGrantPayload>(payload)) {
        return std::get<TokenGrantPayload>(payload).token.is_temporally_valid(current_time);
    }
    if (std::holds_alternative<TokenDelegatePayload>(payload)) {
        return std::get<TokenDelegatePayload>(payload).child_token.is_temporally_valid(current_time);
    }
    return true;
}

} // namespace Sanjeev
