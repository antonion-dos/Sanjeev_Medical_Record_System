#include "server.hpp"
#include "crypto.hpp"
#include "json_utils.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
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

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Failed to create socket: " + std::string(strerror(errno)));
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to bind socket to port " + std::to_string(port_) + ": " + std::string(strerror(errno)));
    }

    if (listen(server_fd_, 64) < 0) {
        close(server_fd_);
        throw std::runtime_error("Failed to listen on socket: " + std::string(strerror(errno)));
    }

    running_ = true;
    server_thread_ = std::thread(&HttpServer::run, this);
    std::cout << "[Sanjeev Blockchain Server] Listening on http://0.0.0.0:" << port_ << std::endl;
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;

    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::run() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (!running_) break;
            continue;
        }

        std::thread([this, client_fd]() {
            handle_client(client_fd);
        }).detach();
    }
}

void HttpServer::handle_client(int client_fd) {
    std::string raw_request;
    char buffer[4096];
    ssize_t bytes_read = 0;

    // Read initial header block
    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        raw_request.append(buffer, bytes_read);
        if (raw_request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    if (raw_request.empty()) {
        close(client_fd);
        return;
    }

    HttpRequest req = parse_request(raw_request);

    // Read remaining body if Content-Length specified
    auto cl_it = req.headers.find("content-length");
    if (cl_it != req.headers.end()) {
        size_t expected_length = std::stoul(cl_it->second);
        size_t header_end = raw_request.find("\r\n\r\n") + 4;
        size_t current_body_len = raw_request.size() - header_end;
        req.body = raw_request.substr(header_end);

        while (req.body.size() < expected_length) {
            bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes_read <= 0) break;
            req.body.append(buffer, bytes_read);
        }
    }

    HttpResponse resp = route_request(req);
    std::string response_data = build_response_string(resp);

    send(client_fd, response_data.data(), response_data.size(), 0);
    close(client_fd);
}

HttpRequest HttpServer::parse_request(const std::string& raw_request) {
    HttpRequest req;
    std::istringstream stream(raw_request);
    std::string line;

    if (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream line_stream(line);
        line_stream >> req.method;
        std::string full_path;
        line_stream >> full_path;

        size_t qmark = full_path.find('?');
        if (qmark != std::string::npos) {
            req.path = full_path.substr(0, qmark);
            req.query = full_path.substr(qmark + 1);
        } else {
            req.path = full_path;
        }
    }

    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && val.front() == ' ') val.erase(0, 1);
            // Lowercase key
            for (auto& c : key) c = tolower(c);
            req.headers[key] = val;
        }
    }

    return req;
}

std::string HttpServer::build_response_string(const HttpResponse& resp) {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << resp.status_code << " " << resp.status_text << "\r\n";
    ss << "Content-Type: " << resp.content_type << "\r\n";
    ss << "Content-Length: " << resp.body.size() << "\r\n";
    ss << "Access-Control-Allow-Origin: *\r\n";
    ss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    ss << "Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With\r\n";
    ss << "Connection: close\r\n";

    for (const auto& h : resp.extra_headers) {
        ss << h.first << ": " << h.second << "\r\n";
    }

    ss << "\r\n";
    ss << resp.body;
    return ss.str();
}

HttpResponse HttpServer::route_request(const HttpRequest& req) {
    HttpResponse resp;

    // Handle CORS preflight
    if (req.method == "OPTIONS") {
        resp.status_code = 204;
        resp.status_text = "No Content";
        return resp;
    }

    // 1. GET /api/status
    if (req.method == "GET" && req.path == "/api/status") {
        JsonValue val = JsonValue::object();
        val["name"] = "Sanjeev Blockchain Node";
        val["version"] = "1.0.0";
        val["chain_height"] = static_cast<int64_t>(blockchain_.get_chain_length());
        val["mempool_tx_count"] = static_cast<int64_t>(blockchain_.get_mempool().size());
        val["current_timestamp"] = Crypto::current_timestamp();
        val["authority_node"] = authority_name_;
        val["authority_address"] = Crypto::derive_address(authority_pubkey_);
        val["is_chain_valid"] = blockchain_.is_chain_valid();

        resp.body = val.dump();
        return resp;
    }

    // 2. GET /api/blocks
    if (req.method == "GET" && req.path == "/api/blocks") {
        const auto& blocks = blockchain_.get_blocks();
        JsonValue arr = JsonValue::array();
        for (const auto& b : blocks) {
            arr.push_back(JsonValue::parse(b.to_json()));
        }
        resp.body = arr.dump();
        return resp;
    }

    // 3. GET /api/birds_eye
    if (req.method == "GET" && req.path == "/api/birds_eye") {
        resp.body = blockchain_.get_birds_eye_view_json();
        return resp;
    }

    // 4. GET /api/records
    if (req.method == "GET" && req.path == "/api/records") {
        std::vector<Transaction> records;
        size_t p_pos = req.query.find("patient=");
        if (p_pos != std::string::npos) {
            std::string patient_addr = req.query.substr(p_pos + 8);
            size_t ampersand = patient_addr.find('&');
            if (ampersand != std::string::npos) patient_addr = patient_addr.substr(0, ampersand);
            records = blockchain_.get_records_for_patient(patient_addr);
        } else {
            records = blockchain_.get_all_records();
        }

        JsonValue arr = JsonValue::array();
        for (const auto& r : records) {
            arr.push_back(JsonValue::parse(r.to_json()));
        }
        resp.body = arr.dump();
        return resp;
    }

    // 5. POST /api/records (submit new encrypted document record)
    if (req.method == "POST" && req.path == "/api/records") {
        try {
            Transaction tx = Transaction::from_json(req.body);
            tx.type = TxType::RECORD_STORE;
            if (tx.timestamp == 0) tx.timestamp = Crypto::current_timestamp();
            if (tx.tx_id.empty()) tx.tx_id = tx.calculate_hash();

            bool added = blockchain_.add_transaction(tx);
            if (!added) {
                resp.status_code = 400;
                resp.status_text = "Bad Request";
                resp.body = "{\"success\":false,\"error\":\"Transaction validation or signature check failed\"}";
                return resp;
            }

            // Auto-mine into block by authority
            if (!authority_privkey_.empty()) {
                blockchain_.mine_block(authority_privkey_, authority_pubkey_, authority_name_);
            }

            JsonValue res = JsonValue::object();
            res["success"] = true;
            res["tx_id"] = tx.tx_id;
            res["record_hash"] = tx.record_hash;
            resp.body = res.dump();
            return resp;
        } catch (const std::exception& e) {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
            resp.body = std::string("{\"success\":false,\"error\":\"") + e.what() + "\"}";
            return resp;
        }
    }

    // 6. POST /api/keys/temporal (patient issues temporal key)
    if (req.method == "POST" && req.path == "/api/keys/temporal") {
        try {
            Transaction tx = Transaction::from_json(req.body);
            tx.type = TxType::TEMPORAL_KEY_GRANT;
            if (tx.timestamp == 0) tx.timestamp = Crypto::current_timestamp();
            if (tx.valid_from == 0) tx.valid_from = tx.timestamp;
            if (tx.tx_id.empty()) tx.tx_id = tx.calculate_hash();

            bool added = blockchain_.add_transaction(tx);
            if (!added) {
                resp.status_code = 400;
                resp.status_text = "Bad Request";
                resp.body = "{\"success\":false,\"error\":\"Temporal key grant rejected\"}";
                return resp;
            }

            if (!authority_privkey_.empty()) {
                blockchain_.mine_block(authority_privkey_, authority_pubkey_, authority_name_);
            }

            JsonValue res = JsonValue::object();
            res["success"] = true;
            res["tx_id"] = tx.tx_id;
            res["valid_until"] = tx.valid_until;
            resp.body = res.dump();
            return resp;
        } catch (const std::exception& e) {
            resp.status_code = 400;
            resp.body = std::string("{\"success\":false,\"error\":\"") + e.what() + "\"}";
            return resp;
        }
    }

    // 7. POST /api/keys/delegate (hospital delegates temporal key to doctor)
    if (req.method == "POST" && req.path == "/api/keys/delegate") {
        try {
            Transaction tx = Transaction::from_json(req.body);
            tx.type = TxType::TEMPORAL_KEY_DELEGATE;
            if (tx.timestamp == 0) tx.timestamp = Crypto::current_timestamp();
            if (tx.valid_from == 0) tx.valid_from = tx.timestamp;
            if (tx.tx_id.empty()) tx.tx_id = tx.calculate_hash();

            bool added = blockchain_.add_transaction(tx);
            if (!added) {
                resp.status_code = 400;
                resp.body = "{\"success\":false,\"error\":\"Temporal key delegation rejected (check parent key validity and expiration)\"}";
                return resp;
            }

            if (!authority_privkey_.empty()) {
                blockchain_.mine_block(authority_privkey_, authority_pubkey_, authority_name_);
            }

            JsonValue res = JsonValue::object();
            res["success"] = true;
            res["tx_id"] = tx.tx_id;
            res["parent_tx_id"] = tx.parent_tx_id;
            res["doctor_recipient"] = tx.recipient;
            resp.body = res.dump();
            return resp;
        } catch (const std::exception& e) {
            resp.status_code = 400;
            resp.body = std::string("{\"success\":false,\"error\":\"") + e.what() + "\"}";
            return resp;
        }
    }

    // 8. POST /api/keys/revoke
    if (req.method == "POST" && req.path == "/api/keys/revoke") {
        try {
            Transaction tx = Transaction::from_json(req.body);
            tx.type = TxType::TEMPORAL_KEY_REVOKE;
            if (tx.timestamp == 0) tx.timestamp = Crypto::current_timestamp();
            if (tx.tx_id.empty()) tx.tx_id = tx.calculate_hash();

            bool added = blockchain_.add_transaction(tx);
            if (added && !authority_privkey_.empty()) {
                blockchain_.mine_block(authority_privkey_, authority_pubkey_, authority_name_);
            }

            JsonValue res = JsonValue::object();
            res["success"] = added;
            resp.body = res.dump();
            return resp;
        } catch (const std::exception& e) {
            resp.status_code = 400;
            resp.body = std::string("{\"success\":false,\"error\":\"") + e.what() + "\"}";
            return resp;
        }
    }

    // 9. GET /api/keys/active
    if (req.method == "GET" && req.path == "/api/keys/active") {
        auto active = blockchain_.get_active_temporal_keys();
        JsonValue arr = JsonValue::array();
        for (const auto& k : active) {
            arr.push_back(JsonValue::parse(k.to_json()));
        }
        resp.body = arr.dump();
        return resp;
    }

    // 10. GET /api/keys/by_holder?holder=0x...
    if (req.method == "GET" && req.path == "/api/keys/by_holder") {
        std::string holder;
        size_t h_pos = req.query.find("holder=");
        if (h_pos != std::string::npos) {
            holder = req.query.substr(h_pos + 7);
            size_t amp = holder.find('&');
            if (amp != std::string::npos) holder = holder.substr(0, amp);
        }
        auto keys = blockchain_.get_temporal_keys_for_holder(holder);
        JsonValue arr = JsonValue::array();
        for (const auto& k : keys) {
            arr.push_back(JsonValue::parse(k.to_json()));
        }
        resp.body = arr.dump();
        return resp;
    }

    // 11. GET /api/access/check?record=...&accessor=...
    if (req.method == "GET" && req.path == "/api/access/check") {
        std::string rec;
        std::string acc;
        size_t r_pos = req.query.find("record=");
        if (r_pos != std::string::npos) {
            rec = req.query.substr(r_pos + 7);
            size_t amp = rec.find('&');
            if (amp != std::string::npos) rec = rec.substr(0, amp);
        }
        size_t a_pos = req.query.find("accessor=");
        if (a_pos != std::string::npos) {
            acc = req.query.substr(a_pos + 9);
            size_t amp = acc.find('&');
            if (amp != std::string::npos) acc = acc.substr(0, amp);
        }

        bool authorized = blockchain_.is_access_authorized(rec, acc);
        JsonValue res = JsonValue::object();
        res["record_hash"] = rec;
        res["accessor"] = acc;
        res["is_authorized"] = authorized;
        resp.body = res.dump();
        return resp;
    }

    // 12. GET /api/trace?id=...
    if (req.method == "GET" && req.path == "/api/trace") {
        std::string target_id;
        size_t id_pos = req.query.find("id=");
        if (id_pos != std::string::npos) {
            target_id = req.query.substr(id_pos + 3);
            size_t amp = target_id.find('&');
            if (amp != std::string::npos) target_id = target_id.substr(0, amp);
        }

        LineageNode node = blockchain_.trace_lineage(target_id);
        JsonValue res = JsonValue::object();
        res["tx_id"] = node.tx_id;
        res["type"] = node.type;
        res["sender"] = node.sender;
        res["recipient"] = node.recipient;
        res["record_hash"] = node.record_hash;
        res["valid_from"] = node.valid_from;
        res["valid_until"] = node.valid_until;
        res["parent_tx_id"] = node.parent_tx_id;
        res["is_active"] = node.is_active;

        JsonValue delegations = JsonValue::array();
        for (const auto& child : node.child_delegations) {
            delegations.push_back(child);
        }
        res["child_delegations"] = delegations;

        resp.body = res.dump();
        return resp;
    }

    // 13. POST /api/wallet/generate (EC keypair generator helper)
    if (req.method == "POST" && req.path == "/api/wallet/generate") {
        KeyPair kp = Crypto::generate_ec_keypair();
        JsonValue res = JsonValue::object();
        res["address"] = kp.address;
        res["public_key_pem"] = kp.public_key_pem;
        res["private_key_pem"] = kp.private_key_pem;
        resp.body = res.dump();
        return resp;
    }

    // 14. POST /api/mine
    if (req.method == "POST" && req.path == "/api/mine") {
        try {
            Block b = blockchain_.mine_block(authority_privkey_, authority_pubkey_, authority_name_);
            JsonValue res = JsonValue::object();
            res["success"] = true;
            res["block_index"] = static_cast<int64_t>(b.index);
            res["block_hash"] = b.hash;
            res["tx_count"] = static_cast<int64_t>(b.transactions.size());
            resp.body = res.dump();
            return resp;
        } catch (const std::exception& e) {
            resp.status_code = 400;
            resp.body = std::string("{\"success\":false,\"error\":\"") + e.what() + "\"}";
            return resp;
        }
    }

    resp.status_code = 404;
    resp.status_text = "Not Found";
    resp.body = "{\"error\":\"Endpoint not found\"}";
    return resp;
}

} // namespace Sanjeev
