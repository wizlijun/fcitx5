// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/dbus/objectvtable.h>
#include <fcitx-utils/dbus/servicewatcher.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/userinterfacemanager.h>
#include <dbus_public.h>

namespace fcitx {
namespace {
using Candidates = std::vector<dbus::DBusStruct<int32_t, std::string, std::string>>;
constexpr const char *Path = "/org/fcitx/Fcitx5/SumiCandidates";
constexpr const char *Interface = "org.fcitx.Fcitx.SumiCandidates1";
}

class SumiCandidates final : public AddonInstance,
                             public dbus::ObjectVTable<SumiCandidates> {
    struct Callback {
        SumiCandidates *owner;
        void operator()(InputContext *ic) const { owner->publish(ic); }
    };
public:
    explicit SumiCandidates(Instance *instance)
        : instance_(instance),
          bus_(instance->addonManager().addon("dbus")->call<IDBusModule::bus>()),
          watcher_(*bus_) {
        bus_->addObjectVTable(Path, Interface, *this);
        for (auto type : {EventType::InputContextFocusIn,
                          EventType::InputContextUpdateUI,
                          EventType::InputContextFlushUI}) {
            events_.emplace_back(instance_->watchEvent(
                type, EventWatcherPhase::Default, [this](Event &event) {
                    if (owner_.empty()) {
                        return;
                    }
                    auto *ic = static_cast<InputContextEvent &>(event).inputContext();
                    if (!bound_.isNull()) {
                        if (bound_.get() == ic && ic->hasFocus()) {
                            attach(ic);
                        }
                    } else if (matches(ic)) {
                        if (!bind(ic)) {
                            revoke();
                        }
                    }
                }));
        }
        for (auto type : {EventType::InputContextFocusOut,
                          EventType::InputContextDestroyed}) {
            events_.emplace_back(instance_->watchEvent(
                type, EventWatcherPhase::Default, [this](Event &event) {
                    auto *ic = static_cast<InputContextEvent &>(event).inputContext();
                    if (bound_.get() == ic) {
                        revoke(typeIsDestroyed(event));
                    }
                }));
        }
    }
    ~SumiCandidates() override { revoke(true); }

    bool setActive(const std::string &token,
                   const std::vector<std::string> &programs, bool active) {
        const auto sender = currentMessage()->sender();
        if (!active) {
            if (sender != owner_ || token != token_) {
                return false;
            }
            revoke();
            return true;
        }
        if (token.empty() || token.size() > 256 || programs.empty() || programs.size() > 8 ||
            std::any_of(programs.begin(), programs.end(), [](const auto &program) {
                return program.empty() || program.size() > 256 ||
                       program.find_first_of("*?[]") != std::string::npos;
            })) {
            return false;
        }
        if (!owner_.empty()) {
            // A renewal cannot transfer ownership to another input context.
            return owner_ == sender && token_ == token && programs_ == programs;
        }
        owner_ = sender;
        token_ = token;
        programs_ = programs;
        ownerWatch_ = watcher_.watchService(owner_,
            [this](const std::string &, const std::string &, const std::string &next) {
                if (next.empty()) {
                    revoke();
                }
            });
        auto *ic = instance_->lastFocusedInputContext();
        if (matches(ic) && !bind(ic)) {
            revoke();
            return false;
        }
        return true;
    }

    void select(const std::string &token, uint64_t serial, int32_t index) {
        auto *ic = validated(token, serial);
        if (!ic || std::find(indices_.begin(), indices_.end(), index) == indices_.end()) {
            return;
        }
        auto list = ic->inputPanel().candidateList();
        if (list && index >= 0 && index < list->size() &&
            !list->candidate(index).isPlaceHolder()) {
            ++serial_; // A second action for the same frame must not run.
            list->candidate(index).select(ic);
        }
    }

    void page(const std::string &token, uint64_t serial, bool next) {
        auto *ic = validated(token, serial);
        if (!ic) {
            return;
        }
        auto list = ic->inputPanel().candidateList();
        auto *pageable = list ? list->toPageable() : nullptr;
        if (pageable && (next ? pageable->hasNext() : pageable->hasPrev())) {
            ++serial_;
            if (next) {
                pageable->next();
            } else {
                pageable->prev();
            }
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
    }

private:
    static bool typeIsDestroyed(const Event &event) {
        return event.type() == EventType::InputContextDestroyed;
    }
    bool matches(InputContext *ic) const {
        return ic && ic->hasFocus() &&
               std::find(programs_.begin(), programs_.end(), ic->program()) != programs_.end();
    }
    bool ownsCallback(InputContext *ic) const {
        auto *callback = ic->inputPanel().customInputPanelCallback().target<Callback>();
        return callback && callback->owner == this;
    }
    bool bind(InputContext *ic) {
        auto &panel = ic->inputPanel();
        if (panel.customInputPanelCallback() || panel.customVirtualKeyboardCallback()) {
            return false;
        }
        const bool panelVisible = !panel.empty() || !panel.overlayMessage().empty();
        // GTK may already be displaying its own client-side popup. Until that
        // frontend supplies a hide operation, preserve it rather than duplicate it.
        if (panelVisible && ic->capabilityFlags().test(CapabilityFlag::ClientSideInputPanel)) {
            return false;
        }
        const auto uiName = instance_->currentUI();
        if (!uiName.empty() && uiName != "classicui") {
            return false;
        }
        auto *ui = uiName.empty() ? nullptr : instance_->addonManager().addon(uiName);
        if (panelVisible && ui) {
            try {
                if (!ui->callWithSignature<bool(InputContext *)>("hideInputPanel", ic)) {
                    return false;
                }
            } catch (const std::runtime_error &) {
                // Stock Classic UI has no hide API: preserve its current popup.
                return false;
            }
        }
        bound_ = ic->watch();
        attach(ic);
        publish(ic);
        return true;
    }
    void attach(InputContext *ic) {
        auto &panel = ic->inputPanel();
        if (panel.customVirtualKeyboardCallback() ||
            (panel.customInputPanelCallback() && !ownsCallback(ic))) {
            revoke();
            return;
        }
        if (!panel.customInputPanelCallback()) {
            panel.setCustomInputPanelCallback(Callback{this});
        }
    }
    InputContext *validated(const std::string &token, uint64_t serial) {
        auto *ic = bound_.get();
        if (currentMessage()->sender() != owner_ || token != token_ || serial != serial_ ||
            !matches(ic) || !ownsCallback(ic)) {
            return nullptr;
        }
        return ic;
    }
    void publish(InputContext *ic) {
        if (owner_.empty() || bound_.get() != ic || !matches(ic)) {
            return;
        }
        Candidates rows;
        indices_.clear();
        int32_t cursor = -1;
        bool prev = false, next = false;
        if (auto list = ic->inputPanel().candidateList()) {
            cursor = list->cursorIndex();
            for (int i = 0; i < list->size(); ++i) {
                const auto &candidate = list->candidate(i);
                if (candidate.isPlaceHolder()) {
                    continue;
                }
                const auto label = instance_->outputFilter(ic, candidate.hasCustomLabel()
                    ? candidate.customLabel() : list->label(i));
                const auto text = instance_->outputFilter(ic,
                    candidate.textWithComment(candidate.spaceBetweenComment() ? " " : ""));
                rows.emplace_back(std::make_tuple(i, label.toString(), text.toString()));
                indices_.push_back(i);
            }
            if (auto *pageable = list->toPageable()) {
                prev = pageable->hasPrev();
                next = pageable->hasNext();
            }
        }
        candidatesTo(owner_, token_, ++serial_, rows, cursor, prev, next);
    }
    void revoke(bool destroying = false) {
        auto *ic = bound_.get();
        if (ic && ownsCallback(ic)) {
            ic->inputPanel().setCustomInputPanelCallback({});
        }
        if (!owner_.empty()) {
            candidatesTo(owner_, token_, ++serial_, Candidates{}, -1, false, false);
            revokedTo(owner_, token_);
        }
        bound_.unwatch();
        owner_.clear();
        token_.clear();
        programs_.clear();
        indices_.clear();
        ownerWatch_.reset();
        if (ic && !destroying) {
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
    }

    FCITX_OBJECT_VTABLE_METHOD(setActive, "SetActive", "sasb", "b");
    FCITX_OBJECT_VTABLE_METHOD(select, "Select", "sti", "");
    FCITX_OBJECT_VTABLE_METHOD(page, "Page", "stb", "");
    FCITX_OBJECT_VTABLE_SIGNAL(candidates, "Candidates", "sta(iss)ibb");
    FCITX_OBJECT_VTABLE_SIGNAL(revoked, "Revoked", "s");
    Instance *instance_;
    dbus::Bus *bus_;
    dbus::ServiceWatcher watcher_;
    std::unique_ptr<dbus::ServiceWatcherEntry> ownerWatch_;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> events_;
    TrackableObjectReference<InputContext> bound_;
    std::string owner_, token_;
    std::vector<std::string> programs_;
    std::vector<int32_t> indices_;
    uint64_t serial_ = 0;
};

class SumiCandidatesFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new SumiCandidates(manager->instance());
    }
};
} // namespace fcitx
FCITX_ADDON_FACTORY_V2(sumicandidates, fcitx::SumiCandidatesFactory)
