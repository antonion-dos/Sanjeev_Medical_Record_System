#include "transaction.hpp"
#include "crypto.hpp"
#include "json_utils.hpp"

#include <sstream>

namespace Sanjeev {

std::string tx_type_to_string(TxType type) {
    switch (type) {
        case TxType::RECORD_STORE: return "RECORD_STORE";
        case TxType::TEMPORAL_KEY_GRANT: return "TEMPORAL_KEY_GRANT";
        case TxType::TEMPORAL_KEY_DELEGATE: return "TEMPORAL_KEY_DELEGATE";
        case TxType::TEMPORAL_KEY_REVOKE: return "TEMPORAL_KEY_REVOKE";
        case TxType::AUTHORITY_REGISTER: return "AUTHORITY_REGISTER";
        default: return "UNKNOWN";
    }
}

TxType string_to_tx_type(const std::string& str) {
    if (str == "RECORD_STORE") return TxType::RECORD_STORE;
    if (str == "TEMPORAL_KEY_GRANT") return TxType::TEMPORAL_KEY_GRANT;
    if (str == "TEMPORAL_KEY_DELEGATE") return TxType::TEMPORAL_KEY_DELEGATE;
    if (str == "TEMPORAL_KEY_REVOKE") return TxType::TEMPORAL_KEY_REVOKE;
    if (str == "AUTHORITY_REGISTER") return TxType::AUTHORITY_REGISTER;
    return TxType::RECORD_STORE;
}

std::string Transaction::get_signing_data() const {
    std::ostringstream ss;
    ss << tx_type_to_string(type) << ":"
       << sender << ":"
       << recipient << ":"
       << timestamp << ":"
       << valid_from << ":"
       << valid_until << ":"
       << parent_tx_id << ":"
       << record_hash << ":"
       << payload << ":"
       << required_signatures;
    return ss.str();
}

std::string Transaction::calculate_hash() const {
    return Crypto::sha256(get_signing_data());
}

bool Transaction::add_signature(const std::string& private_key_pem, const std::string& public_key_pem) {
    std::string h = calculate_hash();
    std::string sig = Crypto::sign(private_key_pem, h);
    std::string addr = Crypto::derive_address(public_key_pem);

    SignatureEntry entry;
    entry.signer_address = addr;
    entry.public_key_pem = public_key_pem;
    entry.signature_hex = sig;

    // Avoid duplicate signature by same address
    for (auto& s : signatures) {
        if (s.signer_address == addr) {
            s = entry;
            return true;
        }
    }
    signatures.push_back(entry);
    return true;
}

bool Transaction::verify_signatures() const {
    if (signatures.size() < required_signatures) {
        return false;
    }

    std::string h = calculate_hash();
    bool sender_signed = false;

    for (const auto& entry : signatures) {
        std::string derived = Crypto::derive_address(entry.public_key_pem);
        if (derived != entry.signer_address) {
            return false;
        }
        if (!Crypto::verify(entry.public_key_pem, h, entry.signature_hex)) {
            return false;
        }
        if (entry.signer_address == sender) {
            sender_signed = true;
        }
    }

    // For single-origin transactions, primary sender must sign
    if (type == TxType::RECORD_STORE || type == TxType::TEMPORAL_KEY_GRANT || type == TxType::TEMPORAL_KEY_DELEGATE) {
        if (!sender_signed) return false;
    }

    return true;
}

bool Transaction::is_temporally_valid(uint64_t current_time) const {
    if (valid_from > 0 && current_time < valid_from) {
        return false;
    }
    if (valid_until > 0 && current_time > valid_until) {
        return false;
    }
    return true;
}

std::string Transaction::to_json() const {
    JsonValue v = JsonValue::object();
    v["tx_id"] = tx_id.empty() ? calculate_hash() : tx_id;
    v["type"] = tx_type_to_string(type);
    v["sender"] = sender;
    v["recipient"] = recipient;
    v["timestamp"] = timestamp;
    v["valid_from"] = valid_from;
    v["valid_until"] = valid_until;
    v["parent_tx_id"] = parent_tx_id;
    v["record_hash"] = record_hash;
    v["payload"] = payload;
    v["required_signatures"] = static_cast<int64_t>(required_signatures);

    JsonValue sigs = JsonValue::array();
    for (const auto& s : signatures) {
        JsonValue sv = JsonValue::object();
        sv["signer_address"] = s.signer_address;
        sv["public_key_pem"] = s.public_key_pem;
        sv["signature_hex"] = s.signature_hex;
        sigs.push_back(sv);
    }
    v["signatures"] = sigs;

    return v.dump();
}

Transaction Transaction::from_json(const std::string& json_str) {
    JsonValue v = JsonValue::parse(json_str);
    Transaction tx;
    tx.tx_id = v["tx_id"].as_string();
    tx.type = string_to_tx_type(v["type"].as_string("RECORD_STORE"));
    tx.sender = v["sender"].as_string();
    tx.recipient = v["recipient"].as_string();
    tx.timestamp = v["timestamp"].as_uint64();
    tx.valid_from = v["valid_from"].as_uint64();
    tx.valid_until = v["valid_until"].as_uint64();
    tx.parent_tx_id = v["parent_tx_id"].as_string();
    tx.record_hash = v["record_hash"].as_string();
    tx.payload = v["payload"].as_string();
    tx.required_signatures = static_cast<uint32_t>(v["required_signatures"].as_int(1));

    const JsonValue& sigs = v["signatures"];
    for (const auto& sv : sigs.arr_val) {
        SignatureEntry entry;
        entry.signer_address = sv["signer_address"].as_string();
        entry.public_key_pem = sv["public_key_pem"].as_string();
        entry.signature_hex = sv["signature_hex"].as_string();
        tx.signatures.push_back(entry);
    }

    if (tx.tx_id.empty()) {
        tx.tx_id = tx.calculate_hash();
    }

    return tx;
}

} // namespace Sanjeev
