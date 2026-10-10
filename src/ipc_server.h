#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace text_injector {

class IPCServer {
public:
    using CommitHandler = std::function<void(const std::string &text)>;
    using ShowHandler = std::function<void(const std::string &message)>;
    using PreeditHandler = std::function<void(const std::string &text, int cursor)>;
    using ClearHandler = std::function<void()>;

    IPCServer(const std::string &socket_path,
              CommitHandler on_commit,
              ShowHandler on_show,
              PreeditHandler on_preedit,
              ClearHandler on_clear);
    ~IPCServer();

    bool start();
    void stop();

private:
    void acceptLoop();
    void handleConnection(int client_fd);

    std::string socket_path_;
    int server_fd_ = -1;
    std::atomic_bool running_{false};
    std::thread accept_thread_;

    CommitHandler on_commit_;
    ShowHandler on_show_;
    PreeditHandler on_preedit_;
    ClearHandler on_clear_;
};

} // namespace text_injector
