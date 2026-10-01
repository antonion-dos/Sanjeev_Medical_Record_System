#include "crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <stdexcept>
#include <cstring>

namespace Sanjeev {

std::string Crypto::to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string Crypto::to_hex(const std::string& input) {
    return to_hex(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

std::vector<uint8_t> Crypto::from_hex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string Crypto::to_base64(const uint8_t* data, size_t len) {
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* bio = BIO_push(b64, bmem);

    BIO_write(bio, data, static_cast<int>(len));
    BIO_flush(bio);

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(bio, &bptr);

    std::string res(bptr->data, bptr->length);
    BIO_free_all(bio);
    return res;
}

std::string Crypto::to_base64(const std::string& input) {
    return to_base64(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

std::vector<uint8_t> Crypto::from_base64(const std::string& b64) {
    BIO* b64_bio = BIO_new(BIO_f_base64());
    BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);
    BIO* bmem = BIO_new_mem_buf(b64.data(), static_cast<int>(b64.size()));
    BIO* bio = BIO_push(b64_bio, bmem);

    std::vector<uint8_t> buffer(b64.size());
    int decoded_len = BIO_read(bio, buffer.data(), static_cast<int>(buffer.size()));
    BIO_free_all(bio);

    if (decoded_len < 0) {
        throw std::runtime_error("Base64 decoding failed");
    }
    buffer.resize(static_cast<size_t>(decoded_len));
    return buffer;
}

std::string Crypto::sha256(const std::string& input) {
    return sha256_bytes(std::vector<uint8_t>(input.begin(), input.end()));
}

Hash256 Crypto::sha256_digest(const uint8_t* data, size_t len) {
    Hash256 out{};
    unsigned int hash_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to allocate EVP_MD_CTX");

    if (1 != EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) ||
        1 != EVP_DigestUpdate(ctx, data, len) ||
        1 != EVP_DigestFinal_ex(ctx, out.data(), &hash_len)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 digest calculation failed");
    }

    EVP_MD_CTX_free(ctx);
    return out;
}

Hash256 Crypto::sha256_digest(const std::vector<uint8_t>& data) {
    return sha256_digest(data.data(), data.size());
}

std::string Crypto::sha256_bytes(const std::vector<uint8_t>& input) {
    uint8_t hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to allocate EVP_MD_CTX");

    if (1 != EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) ||
        1 != EVP_DigestUpdate(ctx, input.data(), input.size()) ||
        1 != EVP_DigestFinal_ex(ctx, hash, &hash_len)) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 calculation failed");
    }

    EVP_MD_CTX_free(ctx);
    return to_hex(hash, hash_len);
}

std::string Crypto::derive_address(const std::string& public_key_pem) {
    std::string h = sha256(public_key_pem);
    return "0x" + h.substr(0, 40);
}

KeyPair Crypto::generate_ec_keypair() {
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!pctx) throw std::runtime_error("Failed to allocate EC PKEY_CTX");

    if (1 != EVP_PKEY_keygen_init(pctx) ||
        1 != EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) ||
        1 != EVP_PKEY_keygen(pctx, &pkey)) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to generate EC KeyPair");
    }
    EVP_PKEY_CTX_free(pctx);

    // Write Private Key to PEM
    BIO* priv_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(priv_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BUF_MEM* priv_ptr = nullptr;
    BIO_get_mem_ptr(priv_bio, &priv_ptr);
    std::string priv_pem(priv_ptr->data, priv_ptr->length);
    BIO_free(priv_bio);

    // Write Public Key to PEM
    BIO* pub_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(pub_bio, pkey);
    BUF_MEM* pub_ptr = nullptr;
    BIO_get_mem_ptr(pub_bio, &pub_ptr);
    std::string pub_pem(pub_ptr->data, pub_ptr->length);
    BIO_free(pub_bio);

    EVP_PKEY_free(pkey);

    KeyPair kp;
    kp.private_key_pem = priv_pem;
    kp.public_key_pem = pub_pem;
    kp.address = derive_address(pub_pem);
    return kp;
}

Address Crypto::derive_address_bytes(const std::string& public_key_pem) {
    Hash256 h = sha256_digest(reinterpret_cast<const uint8_t*>(public_key_pem.data()), public_key_pem.size());
    Address addr{};
    std::copy_n(h.begin(), 20, addr.begin());
    return addr;
}

std::vector<uint8_t> Crypto::sign_bytes(const std::string& private_key_pem, const uint8_t* data, size_t len) {
    BIO* bio = BIO_new_mem_buf(private_key_pem.data(), static_cast<int>(private_key_pem.size()));
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) throw std::runtime_error("Failed to load private key PEM for signing");

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (1 != EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey)) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSignInit failed");
    }

    if (1 != EVP_DigestSignUpdate(ctx, data, len)) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSignUpdate failed");
    }

    size_t sig_len = 0;
    if (1 != EVP_DigestSignFinal(ctx, nullptr, &sig_len)) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSignFinal (size query) failed");
    }

    std::vector<uint8_t> sig(sig_len);
    if (1 != EVP_DigestSignFinal(ctx, sig.data(), &sig_len)) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSignFinal failed");
    }
    sig.resize(sig_len);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    return sig;
}

bool Crypto::verify_bytes(const std::string& public_key_pem, const uint8_t* data, size_t len, const std::vector<uint8_t>& signature) {
    BIO* bio = BIO_new_mem_buf(public_key_pem.data(), static_cast<int>(public_key_pem.size()));
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (1 != EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey)) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    if (1 != EVP_DigestVerifyUpdate(ctx, data, len)) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return false;
    }

    int rc = EVP_DigestVerifyFinal(ctx, signature.data(), signature.size());
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    return (rc == 1);
}

std::string Crypto::sign(const std::string& private_key_pem, const std::string& message) {
    auto sig = sign_bytes(private_key_pem, reinterpret_cast<const uint8_t*>(message.data()), message.size());
    return to_hex(sig.data(), sig.size());
}

bool Crypto::verify(const std::string& public_key_pem, const std::string& message, const std::string& signature_hex) {
    try {
        auto sig = from_hex(signature_hex);
        return verify_bytes(public_key_pem, reinterpret_cast<const uint8_t*>(message.data()), message.size(), sig);
    } catch (...) {
        return false;
    }
}

std::vector<uint8_t> Crypto::generate_random_bytes(size_t len) {
    std::vector<uint8_t> buf(len);
    if (1 != RAND_bytes(buf.data(), static_cast<int>(len))) {
        throw std::runtime_error("RAND_bytes failed to generate secure entropy");
    }
    return buf;
}

EncryptedData Crypto::encrypt_aes_gcm(const std::string& plaintext, const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        throw std::invalid_argument("AES-256 requires a 32-byte key");
    }

    const size_t iv_len = 12; // Standard 96-bit IV for GCM
    const size_t tag_len = 16; // Standard 128-bit Tag
    std::vector<uint8_t> iv = generate_random_bytes(iv_len);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to allocate cipher context");

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) ||
        1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv_len), nullptr) ||
        1 != EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }

    std::vector<uint8_t> ciphertext(plaintext.size() + 16);
    int len = 0;
    if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                              reinterpret_cast<const uint8_t*>(plaintext.data()), static_cast<int>(plaintext.size()))) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }
    int ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }
    ciphertext_len += len;
    ciphertext.resize(static_cast<size_t>(ciphertext_len));

    std::vector<uint8_t> tag(tag_len);
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag_len), tag.data())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CIPHER_CTX_ctrl GET_TAG failed");
    }
    EVP_CIPHER_CTX_free(ctx);

    EncryptedData res;
    res.ciphertext_b64 = to_base64(ciphertext.data(), ciphertext.size());
    res.iv_b64 = to_base64(iv.data(), iv.size());
    res.tag_b64 = to_base64(tag.data(), tag.size());
    res.algorithm = "AES-256-GCM";
    return res;
}

std::string Crypto::decrypt_aes_gcm(const EncryptedData& data, const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        throw std::invalid_argument("AES-256 requires a 32-byte key");
    }

    std::vector<uint8_t> ciphertext = from_base64(data.ciphertext_b64);
    std::vector<uint8_t> iv = from_base64(data.iv_b64);
    std::vector<uint8_t> tag = from_base64(data.tag_b64);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to allocate cipher context");

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) ||
        1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) ||
        1 != EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptInit_ex failed");
    }

    std::vector<uint8_t> plaintext(ciphertext.size());
    int len = 0;
    if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size()))) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptUpdate failed");
    }
    int plaintext_len = len;

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), tag.data())) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CIPHER_CTX_ctrl SET_TAG failed");
    }

    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) {
        throw std::runtime_error("AES-GCM decryption failed: authentication tag verification failed");
    }
    plaintext_len += len;
    plaintext.resize(static_cast<size_t>(plaintext_len));

    return std::string(plaintext.begin(), plaintext.end());
}

uint64_t Crypto::current_timestamp() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace Sanjeev
