#pragma once

#include <memory>
#include <string>

#include <fcitx-config/option.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/instance.h>

namespace text_injector {

class IPCServer;

FCITX_CONFIGURATION(
    TextInjectorConfig,
    fcitx::Option<fcitx::Key, fcitx::KeyConstrain> triggerKey{
        this, "TriggerKey", "Trigger key", fcitx::Key(FcitxKey_F12),
        fcitx::KeyConstrain({fcitx::KeyConstrainFlag::AllowModifierLess,
                             fcitx::KeyConstrainFlag::AllowModifierOnly})};
    fcitx::Option<std::string> socketPath{
        this, "SocketPath", "Unix socket path (empty = auto)", ""};);

class TextInjectorModule final : public fcitx::AddonInstance {
public:
    explicit TextInjectorModule(fcitx::Instance *instance);
    ~TextInjectorModule() override;

    void reloadConfig() override;
    void save() override;
    const fcitx::Configuration *getConfig() const override;
    void setConfig(const fcitx::RawConfig &config) override;

    void commitText(const std::string &text);
    void showStatus(const std::string &message);
    void showPreedit(const std::string &text, int cursor_pos);
    void clearUI();

private:
    void handleKeyEvent(fcitx::KeyEvent &event);
    void applyConfig();
    std::string resolveSocketPath() const;

    fcitx::Instance *instance_;
    fcitx::EventDispatcher event_dispatcher_;
    TextInjectorConfig config_;
    std::string socket_path_;
    std::unique_ptr<IPCServer> ipc_server_;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> key_handler_;
    fcitx::InputContext *active_ic_ = nullptr;
};

} // namespace text_injector
