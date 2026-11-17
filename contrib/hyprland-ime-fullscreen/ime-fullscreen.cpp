// SPDX-License-Identifier: MIT
// Hyprland 0.56.2: solitary fullscreen rendering skips renderIME().
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <stdexcept>
#include <string_view>

static CHyprSignalListener beforeChecks;

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    if (std::string_view(__hyprland_api_get_hash()) != __hyprland_api_get_client_hash())
        throw std::runtime_error("ime-fullscreen: Hyprland ABI mismatch; rebuild plugin");
    if (HyprlandAPI::getHyprlandVersion(handle).hash != GIT_COMMIT_HASH)
        throw std::runtime_error("ime-fullscreen: unsupported compositor version");

    beforeChecks = Event::bus()->m_events.render.preChecks.listen([](PHLMONITOR monitor) {
        if (!monitor || !monitor->m_solitaryClient || !g_pInputManager)
            return;
        auto& relay = g_pInputManager->m_relay;
        auto* input = relay.getFocusedTextInput();
        if (relay.m_inputMethod.expired() || !input || !input->isEnabled())
            return;
        // Only the display containing the focused input is affected. The
        // normal scene renderer retains popup visibility and lockscreen rules.
        // Run before direct-scanout/tearing checks, not just before drawing.
        if (Desktop::focusState()->monitor() == monitor)
            monitor->m_solitaryClient.reset();
    });
    return {"ime-fullscreen", "Keep native IME popups visible in true fullscreen", "Bruce", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() { beforeChecks.reset(); }
