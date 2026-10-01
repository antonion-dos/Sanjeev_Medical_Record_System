#include "server.hpp"
#include "crypto.hpp"
#include "json_utils.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <thread>

namespace Sanjeev {

HttpServer::HttpServer(Blockchain& blockchain, int port)
    : blockchain_(blockchain), port_(port) {
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::set_authority_credentials(const std::string& privkey, const std::string& pubkey, const std::string& name) {
    authority_privkey_ = privkey;
    authority_pubkey_ = pubkey;
    authority_name_ = name;
}

void HttpServer::start() {
    if (running_) return;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (IS_INVALID_SOCKET(server_fd_)) {
        throw std::runtime_error("Failed to create server socket");
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
        throw std::runtime_error("Failed to bind socket to port " + std::to_string(port_));
    }

    if (listen(server_fd_, 128) < 0) {
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
        throw std::runtime_error("Failed to listen on socket");
    }

    running_ = true;
    server_thread_ = std::thread(&HttpServer::run, this);
    std::cout << "[Sanjeev Server] Listening on http://localhost:" << port_ << std::endl;
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;

    if (!IS_INVALID_SOCKET(server_fd_)) {
        closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[Sanjeev Server] Server stopped." << std::endl;
}

void HttpServer::run() {
    while (running_) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        socket_t client_fd = accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        if (IS_INVALID_SOCKET(client_fd)) {
            if (!running_) break;
            continue;
        }

        std::thread(&HttpServer::handle_client, this, client_fd).detach();
    }
}

void HttpServer::handle_client(socket_t client_fd) {
    std::string raw_request;
    char buffer[4096];

    while (true) {
        int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) break;
        raw_request.append(buffer, bytes_read);

        // Check if headers end
        size_t header_end = raw_request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            // Check Content-Length for body
            size_t cl_pos = raw_request.find("Content-Length: ");
            if (cl_pos != std::string::npos && cl_pos < header_end) {
                size_t val_end = raw_request.find("\r\n", cl_pos);
                int content_len = std::stoi(raw_request.substr(cl_pos + 16, val_end - (cl_pos + 16)));
                if (raw_request.size() < header_end + 4 + content_len) {
                    continue; // Wait for full body
                }
            }
            break;
        }
    }

    if (!raw_request.empty()) {
        HttpRequest req = parse_request(raw_request);
        HttpResponse resp = route_request(req);
        std::string raw_resp = build_response_string(resp);
        send(client_fd, raw_resp.data(), static_cast<int>(raw_resp.size()), 0);
    }

    closesocket(client_fd);
}

HttpRequest HttpServer::parse_request(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;

    if (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream line_stream(line);
        line_stream >> req.method;
        std::string full_path;
        line_stream >> full_path;

        size_t q_pos = full_path.find('?');
        if (q_pos != std::string::npos) {
            req.path = full_path.substr(0, q_pos);
            req.query = full_path.substr(q_pos + 1);
        } else {
            req.path = full_path;
        }
    }

    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        if (line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && val.front() == ' ') val.erase(0, 1);
            req.headers[key] = val;
        }
    }

    size_t body_pos = raw.find("\r\n\r\n");
    if (body_pos != std::string::npos) {
        req.body = raw.substr(body_pos + 4);
    }

    return req;
}

std::string HttpServer::build_response_string(const HttpResponse& resp) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << resp.status_code << " " << resp.status_text << "\r\n";
    oss << "Content-Type: " << resp.content_type << "\r\n";
    oss << "Content-Length: " << resp.body.size() << "\r\n";
    oss << "Connection: close\r\n";

    for (const auto& h : resp.extra_headers) {
        oss << h.first << ": " << h.second << "\r\n";
    }

    oss << "\r\n";
    oss << resp.body;
    return oss.str();
}

HttpResponse HttpServer::route_request(const HttpRequest& req) {
    HttpResponse resp;
    resp.extra_headers["Access-Control-Allow-Origin"] = "*";
    resp.extra_headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
    resp.extra_headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";

    if (req.method == "OPTIONS") {
        resp.status_code = 204;
        resp.status_text = "No Content";
        return resp;
    }

    try {
        // 1. GET /api/v1/chain/status
        if (req.method == "GET" && req.path == "/api/v1/chain/status") {
            JsonValue j = JsonValue::object();
            j["status"] = "online";
            j["chain_height"] = static_cast<double>(blockchain_.get_chain_height());
            j["mempool_size"] = static_cast<double>(blockchain_.get_mempool().size());
            j["authority_name"] = authority_name_;

            resp.body = j.to_string();
            return resp;
        }

        // 2. GET /api/v1/chain/blocks
        if (req.method == "GET" && req.path == "/api/v1/chain/blocks") {
            JsonValue arr = JsonValue::array();
            uint64_t height = blockchain_.get_chain_height();
            for (uint64_t i = 0; i < height; ++i) {
                auto b_opt = blockchain_.get_block_by_index(i);
                if (b_opt) {
                    JsonValue bj = JsonValue::object();
                    bj["index"] = static_cast<double>(b_opt->header.index);
                    bj["timestamp"] = static_cast<double>(b_opt->header.timestamp);
                    bj["hash"] = b_opt->get_hash_hex();
                    bj["prev_hash"] = hash_to_hex(b_opt->header.prev_hash);
                    bj["merkle_root"] = hash_to_hex(b_opt->header.merkle_root);
                    bj["authority_name"] = b_opt->header.authority_name;
                    bj["tx_count"] = static_cast<double>(b_opt->transactions.size());
                    arr.push_back(bj);
                }
            }
            resp.body = arr.to_string();
            return resp;
        }

        // 3. POST /api/v1/blob/store
        if (req.method == "POST" && req.path == "/api/v1/blob/store") {
            JsonValue body_j = JsonValue::parse(req.body);
            Transaction tx;
            tx.version = 1;
            tx.type = TxType::BLOB_STORE;
            tx.timestamp = Crypto::current_timestamp();
            tx.nonce = static_cast<uint64_t>(body_j["nonce"].int_val);
            tx.sender = hex_to_address(body_j["sender"].str_val);

            JsonValue p = body_j["payload"];
            EncryptedBlob blob;
            blob.blob_id = hex_to_hash(p["blob_id"].str_val);
            blob.previous_blob_id = hex_to_hash(p["previous_blob_id"].str_val);
            blob.owner_address = hex_to_address(p["owner_address"].str_val);
            blob.updater_address = tx.sender;
            blob.timestamp = tx.timestamp;
            blob.iv = hex_to_bytes(p["iv_hex"].str_val);
            blob.tag = hex_to_bytes(p["tag_hex"].str_val);
            blob.data = Crypto::from_base64(p["data_b64"].str_val);

            tx.payload = BlobStorePayload{blob};
            if (blockchain_.add_transaction(tx)) {
                JsonValue res = JsonValue::object();
                res["status"] = "accepted";
                res["tx_id"] = tx.get_id_hex();
                res["blob_id"] = hash_to_hex(blob.blob_id);
                resp.body = res.to_string();
            } else {
                resp.status_code = 400;
                resp.body = R"({"error":"Transaction rejected by mempool"})";
            }
            return resp;
        }

        // 4. POST /api/v1/token/grant
        if (req.method == "POST" && req.path == "/api/v1/token/grant") {
            JsonValue body_j = JsonValue::parse(req.body);
            Transaction tx;
            tx.version = 1;
            tx.type = TxType::TOKEN_GRANT;
            tx.timestamp = Crypto::current_timestamp();
            tx.nonce = static_cast<uint64_t>(body_j["nonce"].int_val);
            tx.sender = hex_to_address(body_j["sender"].str_val);

            JsonValue p = body_j["payload"];
            TemporalAccessToken t;
            t.token_id = hex_to_hash(p["token_id"].str_val);
            t.target_blob_id = hex_to_hash(p["target_blob_id"].str_val);
            t.grantor_address = hex_to_address(p["grantor_address"].str_val);
            t.recipient_address = hex_to_address(p["recipient_address"].str_val);
            t.valid_from = static_cast<uint64_t>(p["valid_from"].int_val);
            t.valid_until = static_cast<uint64_t>(p["valid_until"].int_val);
            t.encrypted_symkey = hex_to_bytes(p["encrypted_symkey_hex"].str_val);
            t.status = 1;

            tx.payload = TokenGrantPayload{t};
            if (blockchain_.add_transaction(tx)) {
                JsonValue res = JsonValue::object();
                res["status"] = "accepted";
                res["tx_id"] = tx.get_id_hex();
                res["token_id"] = hash_to_hex(t.token_id);
                resp.body = res.to_string();
            } else {
                resp.status_code = 400;
                resp.body = R"({"error":"Token grant rejected"})";
            }
            return resp;
        }

        // 5. POST /api/v1/token/audit_decrypt
        if (req.method == "POST" && req.path == "/api/v1/token/audit_decrypt") {
            JsonValue body_j = JsonValue::parse(req.body);
            Hash256 token_id = hex_to_hash(body_j["token_id"].str_val);
            Address accessor = hex_to_address(body_j["accessor_address"].str_val);
            uint64_t cur_time = body_j["timestamp"].int_val ? static_cast<uint64_t>(body_j["timestamp"].int_val) : Crypto::current_timestamp();

            std::vector<uint8_t> symkey;
            if (blockchain_.request_decryption(token_id, accessor, cur_time, symkey)) {
                JsonValue res = JsonValue::object();
                res["authorized"] = true;
                res["encrypted_symkey_hex"] = bytes_to_hex(symkey.data(), symkey.size(), false);
                resp.body = res.to_string();
            } else {
                resp.status_code = 403;
                resp.body = R"({"authorized":false,"error":"Access denied: invalid or expired temporal token"})";
            }
            return resp;
        }

        // 6. POST /api/v1/node/mine
        if (req.method == "POST" && req.path == "/api/v1/node/mine") {
            if (authority_privkey_.empty() || authority_pubkey_.empty()) {
                resp.status_code = 400;
                resp.body = R"({"error":"Node lacks authority signing credentials"})";
                return resp;
            }

            Block b = blockchain_.mine_block(authority_privkey_, authority_pubkey_, authority_name_);
            JsonValue res = JsonValue::object();
            res["status"] = "mined";
            res["index"] = static_cast<double>(b.header.index);
            res["hash"] = b.get_hash_hex();
            res["tx_count"] = static_cast<double>(b.transactions.size());
            resp.body = res.to_string();
            return resp;
        }

        // 7. GET /api/v1/blob/:id
        if (req.method == "GET" && req.path.rfind("/api/v1/blob/", 0) == 0) {
            std::string id_str = req.path.substr(13);
            auto blob_opt = blockchain_.get_blob(hex_to_hash(id_str));
            if (blob_opt) {
                JsonValue res = JsonValue::object();
                res["blob_id"] = hash_to_hex(blob_opt->blob_id);
                res["previous_blob_id"] = hash_to_hex(blob_opt->previous_blob_id);
                res["owner_address"] = address_to_hex(blob_opt->owner_address);
                res["updater_address"] = address_to_hex(blob_opt->updater_address);
                res["timestamp"] = static_cast<double>(blob_opt->timestamp);
                res["iv_hex"] = bytes_to_hex(blob_opt->iv.data(), blob_opt->iv.size(), false);
                res["tag_hex"] = bytes_to_hex(blob_opt->tag.data(), blob_opt->tag.size(), false);
                res["data_b64"] = Crypto::to_base64(blob_opt->data.data(), blob_opt->data.size());
                resp.body = res.to_string();
            } else {
                resp.status_code = 404;
                resp.body = R"({"error":"Encrypted blob not found"})";
            }
            return resp;
        }

        // 404 Fallback
        resp.status_code = 404;
        resp.body = R"({"error":"Endpoint not found"})";
    } catch (const std::exception& e) {
        resp.status_code = 500;
        JsonValue err = JsonValue::object();
        err["error"] = e.what();
        resp.body = err.to_string();
    }

    return resp;
}

} // namespace Sanjeev
