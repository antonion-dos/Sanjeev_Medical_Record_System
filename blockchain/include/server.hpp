#pragma once

#include "blockchain.hpp"
#include <string>
#include <atomic>
#include <thread>
#include <map>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  #define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  using socket_t = int;
  #define INVALID_SOCKET (-1)
  #define IS_INVALID_SOCKET(s) ((s) < 0)
  #define closesocket(s) close(s)
#endif

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
    bool is_running() const { return running_; }

private:
    void run();
    void handle_client(socket_t client_fd);

    HttpResponse route_request(const HttpRequest& req);
    HttpRequest parse_request(const std::string& raw_request);
    std::string build_response_string(const HttpResponse& resp);

    Blockchain& blockchain_;
    int port_;
    socket_t server_fd_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread server_thread_;

    std::string authority_privkey_;
    std::string authority_pubkey_;
    std::string authority_name_ = "Ministry of Health Validator Node #1";
};

} // namespace Sanjeev
