#include "ipc_server.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx-utils/log.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace text_injector {

namespace {

std::string jsonStringOr(const json &value, const char *key,
                         const std::string &fallback = "") {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

int jsonIntOr(const json &value, const char *key, int fallback) {
    const auto it = value.find(key);
    return it != value.end() && it->is_number_integer() ? it->get<int>()
                                                        : fallback;
}

std::string handleRequest(const json &request,
                          const IPCServer::CommitHandler &on_commit,
                          const IPCServer::ShowHandler &on_show,
                          const IPCServer::PreeditHandler &on_preedit,
                          const IPCServer::ClearHandler &on_clear) {
    const std::string type = jsonStringOr(request, "type");

    if (type == "ping") {
        return json{{"pong", true}}.dump();
    }

    if (type == "commit") {
        const std::string text = jsonStringOr(request, "text");
        if (text.empty()) {
            return json{{"success", false}, {"error", "empty text"}}.dump();
        }
        on_commit(text);
        return json{{"success", true}}.dump();
    }

    if (type == "show") {
        const std::string message = jsonStringOr(request, "text");
        on_show(message);
        return json{{"success", true}}.dump();
    }

    if (type == "preedit") {
        const std::string text = jsonStringOr(request, "text");
        const int cursor = jsonIntOr(request, "cursor", 0);
        on_preedit(text, cursor);
        return json{{"success", true}}.dump();
    }

    if (type == "clear") {
        on_clear();
        return json{{"success", true}}.dump();
    }

    return json{{"success", false}, {"error", "unknown command: " + type}}.dump();
}

} // namespace

IPCServer::IPCServer(const std::string &socket_path, CommitHandler on_commit,
                     ShowHandler on_show, PreeditHandler on_preedit,
                     ClearHandler on_clear)
    : socket_path_(socket_path), on_commit_(std::move(on_commit)),
      on_show_(std::move(on_show)), on_preedit_(std::move(on_preedit)),
      on_clear_(std::move(on_clear)) {}

IPCServer::~IPCServer() {
    stop();
}

bool IPCServer::start() {
    if (running_.load()) {
        return true;
    }

    ::unlink(socket_path_.c_str());

    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        FCITX_ERROR() << "IPC: socket() failed: " << strerror(errno);
        return false;
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, reinterpret_cast<struct sockaddr *>(&addr),
             sizeof(addr)) < 0) {
        FCITX_ERROR() << "IPC: bind() failed on " << socket_path_ << ": "
                      << strerror(errno);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (listen(server_fd_, 8) < 0) {
        FCITX_ERROR() << "IPC: listen() failed: " << strerror(errno);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void IPCServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    ::unlink(socket_path_.c_str());
}

void IPCServer::acceptLoop() {
    while (running_.load()) {
        struct sockaddr_un client_addr {};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = accept(
            server_fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
            &client_len);

        if (client_fd < 0) {
            if (running_.load() && errno == EINTR) {
                continue;
            }
            break;
        }

        handleConnection(client_fd);
    }
}

void IPCServer::handleConnection(int client_fd) {
    const struct timeval timeout = {2, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::string request_data;
    char buffer[4096];
    while (true) {
        const ssize_t len = recv(client_fd, buffer, sizeof(buffer), 0);
        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(client_fd);
            return;
        }
        if (len == 0) {
            break;
        }
        request_data.append(buffer, static_cast<size_t>(len));
    }

    std::string response;
    if (request_data.empty()) {
        response = json{{"success", false}, {"error", "empty request"}}.dump();
    } else {
        try {
            const json request = json::parse(request_data);
            response = handleRequest(request, on_commit_, on_show_,
                                     on_preedit_, on_clear_);
        } catch (const json::exception &e) {
            response =
                json{{"success", false}, {"error", "invalid JSON"}}.dump();
        }
    }

    size_t total_sent = 0;
    while (total_sent < response.size()) {
        const ssize_t sent =
            send(client_fd, response.data() + total_sent,
                 response.size() - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        total_sent += static_cast<size_t>(sent);
    }

    close(client_fd);
}

} // namespace text_injector
