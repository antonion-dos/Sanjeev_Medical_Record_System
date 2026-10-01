#pragma once

#include "blockchain.hpp"
#include <string>
#include <atomic>
#include <thread>
#include <functional>

namespace Sanjeev {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::string content_type = "application/json";
    std::string body;
    std::map<std::string, std::string> extra_headers;
};

class HttpServer {
public:
    HttpServer(Blockchain& blockchain, int port = 8080);
    ~HttpServer();

    void set_authority_credentials(const std::string& privkey, const std::string& pubkey, const std::string& name);

    void start();
    void stop();

private:
    void run();
    void handle_client(int client_fd);

    HttpResponse route_request(const HttpRequest& req);
    HttpRequest parse_request(const std::string& raw_request);
    std::string build_response_string(const HttpResponse& resp);

    Blockchain& blockchain_;
    int port_;
    int server_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread server_thread_;

    std::string authority_privkey_;
    std::string authority_pubkey_;
    std::string authority_name_ = "Ministry of Health Validator Node #1";
};

} // namespace Sanjeev
