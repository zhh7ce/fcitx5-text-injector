#include "module.h"
#include "ipc_server.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/log.h>
#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#define TI_HAS_STANDARD_PATHS 1
#else
#include <fcitx-utils/standardpath.h>
#define TI_HAS_STANDARD_PATHS 0
#endif
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <nlohmann/json.hpp>

namespace text_injector {

namespace {

#if TI_HAS_STANDARD_PATHS
constexpr auto CONFIG_PATH_TYPE = fcitx::StandardPathsType::PkgConfig;
#else
constexpr auto CONFIG_PATH_TYPE = fcitx::StandardPath::Type::PkgConfig;
#endif

constexpr auto FCITX_CONFIG_PATH = "conf/text_injector.conf";

std::string defaultSocketPath() {
    if (const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
        runtime_dir && runtime_dir[0] == '/') {
        return std::string(runtime_dir) + "/text-injector.sock";
    }
    return "/tmp/text-injector-" +
           std::to_string(static_cast<unsigned long>(getuid())) + ".sock";
}

} // namespace

TextInjectorModule::TextInjectorModule(fcitx::Instance *instance)
    : instance_(instance) {
    event_dispatcher_.attach(&instance_->eventLoop());
    reloadConfig();

    key_handler_ = instance_->watchEvent(
        fcitx::EventType::InputContextKeyEvent,
        fcitx::EventWatcherPhase::PreInputMethod,
        [this](fcitx::Event &event) {
            handleKeyEvent(static_cast<fcitx::KeyEvent &>(event));
        });

    ipc_server_ = std::make_unique<IPCServer>(
        socket_path_,
        [this](const std::string &text) {
            event_dispatcher_.schedule([this, text] { commitText(text); });
        },
        [this](const std::string &msg) {
            event_dispatcher_.schedule([this, msg] { showStatus(msg); });
        },
        [this](const std::string &text, int cursor) {
            event_dispatcher_.schedule(
                [this, text, cursor] { showPreedit(text, cursor); });
        },
        [this] { event_dispatcher_.schedule([this] { clearUI(); }); });

    if (!ipc_server_->start()) {
        FCITX_WARN() << "Failed to start IPC server on " << socket_path_;
    } else {
        FCITX_INFO() << "Text Injector module loaded; socket=" << socket_path_;
    }
}

TextInjectorModule::~TextInjectorModule() {
    if (ipc_server_) {
        ipc_server_->stop();
    }
    event_dispatcher_.detach();
}

void TextInjectorModule::reloadConfig() {
    fcitx::readAsIni(config_, CONFIG_PATH_TYPE, FCITX_CONFIG_PATH);
    applyConfig();
}

void TextInjectorModule::save() {
    if (!fcitx::safeSaveAsIni(config_, CONFIG_PATH_TYPE, FCITX_CONFIG_PATH)) {
        FCITX_WARN() << "Failed to save text injector config";
    }
}

const fcitx::Configuration *TextInjectorModule::getConfig() const {
    return &config_;
}

void TextInjectorModule::setConfig(const fcitx::RawConfig &config) {
    auto updated = config_;
    updated.load(config, true);
    config_ = updated;
    applyConfig();
    save();
}

void TextInjectorModule::applyConfig() {
    socket_path_ = resolveSocketPath();
}

std::string TextInjectorModule::resolveSocketPath() const {
    const std::string configured = config_.socketPath.value();
    if (!configured.empty()) {
        return configured;
    }
    return defaultSocketPath();
}

void TextInjectorModule::handleKeyEvent(fcitx::KeyEvent &event) {
    if (event.isRelease()) {
        return;
    }
    const fcitx::Key key = event.key();
    const fcitx::Key configured = config_.triggerKey.value().normalize();
    if (!configured.isValid() || key != configured) {
        return;
    }
    FCITX_INFO() << "Trigger key pressed; active_ic="
                 << (event.inputContext() ? "yes" : "no");
    if (auto *ic = event.inputContext()) {
        showStatus("Text Injector: ready");
    }
}

void TextInjectorModule::commitText(const std::string &text) {
    auto *ic = instance_->lastFocusedInputContext();
    if (!ic || !ic->hasFocus()) {
        FCITX_WARN() << "commitText: no focused input context";
        return;
    }
    if (text.empty()) {
        return;
    }

    clearUI();
    ic->commitString(text);
    FCITX_INFO() << "Committed " << text.size() << " bytes";
}

void TextInjectorModule::showStatus(const std::string &message) {
    auto *ic = instance_->lastFocusedInputContext();
    if (!ic) {
        return;
    }
    auto &panel = ic->inputPanel();
    panel.reset();
    fcitx::Text aux;
    aux.append(message);
    panel.setAuxUp(aux);
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void TextInjectorModule::showPreedit(const std::string &text, int cursor_pos) {
    auto *ic = instance_->lastFocusedInputContext();
    if (!ic) {
        return;
    }
    auto &panel = ic->inputPanel();
    panel.reset();
    fcitx::Text preedit;
    preedit.append(text);
    panel.setClientPreedit(preedit);
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void TextInjectorModule::clearUI() {
    auto *ic = instance_->lastFocusedInputContext();
    if (!ic) {
        return;
    }
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

} // namespace text_injector

class TextInjectorModuleFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new text_injector::TextInjectorModule(manager->instance());
    }
};

FCITX_ADDON_FACTORY(TextInjectorModuleFactory);
