// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "../Core/ConfigModels.h"
#include "../Core/Introspection.h"
#include "../Core/StartupOptions.h"
#include "WindowsServices.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Foundation.h>

namespace pmon::ui::core {
class ConfigurationStore;
}

namespace pmon::ui::interop {
class KernelClient;
struct KernelEvent;
}

namespace pmon::ui::services {

class AppSession : public std::enable_shared_from_this<AppSession> {
public:
    using StateChangedHandler = std::function<void()>;
    using NotificationHandler = std::function<void(std::string const&)>;

    AppSession(StartupOptions options, winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);
    ~AppSession();
    AppSession(AppSession const&) = delete;
    AppSession& operator=(AppSession const&) = delete;

    winrt::Windows::Foundation::IAsyncAction InitializeAsync();
    winrt::Windows::Foundation::IAsyncAction CloseAsync();
    winrt::Windows::Foundation::IAsyncAction RefreshProcessesAsync();
    winrt::Windows::Foundation::IAsyncAction SelectProcessAsync(std::optional<int> pid,
        std::function<bool()> isCurrent = {});
    winrt::Windows::Foundation::IAsyncAction SelectPresetAsync(core::Preset preset);
    winrt::Windows::Foundation::IAsyncAction ToggleCaptureAsync();
    winrt::Windows::Foundation::IAsyncAction SetCaptureAsync(bool active);
    winrt::Windows::Foundation::IAsyncAction BindHotkeyAsync(core::HotkeyBinding binding);
    winrt::Windows::Foundation::IAsyncAction ResetPreferencesAsync();
    winrt::Windows::Foundation::IAsyncAction ImportLoadoutAsync(std::string const& path);
    void ExportLoadout(std::string const& path);
    void ExploreFolder(std::string const& kind);

    void Changed();
    void Notify(std::string const& message) const;
    core::HotkeyBinding GetBinding(core::HotkeyAction action) const;

    StartupOptions const& Options() const noexcept;
    core::PreferenceFile const& PreferenceFile() const noexcept;
    core::PreferenceFile& PreferenceFile() noexcept;
    core::Preferences const& Preferences() const noexcept;
    core::Preferences& Preferences() noexcept;
    core::IntrospectionData const& Introspection() const noexcept;
    std::vector<std::shared_ptr<core::Widget>> const& Widgets() const noexcept;
    std::vector<std::shared_ptr<core::Widget>>& Widgets() noexcept;
    std::vector<ProcessEntry> const& Processes() const noexcept;
    std::unordered_map<std::string, std::string> const& AppInfo() const noexcept;
    bool IsConnected() const noexcept;
    bool IsReady() const noexcept;
    bool Capturing() const noexcept;
    std::string const& ConnectionStatus() const noexcept;
    std::optional<int> SelectedPid() const noexcept;
    ProcessEntry const* SelectedProcess() const noexcept;
    std::string CaptureDirectory() const;

    void StateChanged(StateChangedHandler handler);
    void DocumentChanged(StateChangedHandler handler);
    void Notification(NotificationHandler handler);

private:
    winrt::Windows::Foundation::IAsyncAction FlushAsync();
    winrt::Windows::Foundation::IAsyncAction PushAsync();
    winrt::Windows::Foundation::IAsyncAction ReceiveEventsAsync();
    winrt::Windows::Foundation::IAsyncAction PollProcessesAsync();
    winrt::Windows::Foundation::IAsyncAction HandleEventAsync(interop::KernelEvent message);
    winrt::Windows::Foundation::IAsyncAction SaveAfterDelayAsync(uint64_t generation);
    winrt::Windows::Foundation::IAsyncAction StopCaptureAfterDelayAsync(uint64_t generation, double seconds);
    void LoadBlocklist();
    void LoadSelectedPreset();
    void EmitStateChanged() const;
    void EmitDocumentChanged() const;
    void ThrowIfClosed() const;

    StartupOptions options_;
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{ nullptr };
    std::unique_ptr<core::ConfigurationStore> store_;
    std::shared_ptr<interop::KernelClient> kernel_;
    core::PreferenceFile preferenceFile_;
    core::IntrospectionData introspection_;
    std::vector<std::shared_ptr<core::Widget>> widgets_;
    std::vector<ProcessEntry> processes_;
    std::unordered_map<std::string, std::string> appInfo_;
    std::unordered_set<std::string> blocklist_;
    std::optional<core::Preset> loadedPreset_;
    std::optional<int> selectedPid_;
    std::string blocklistPath_;
    std::string connectionStatus_ = "Connecting to PresentMon...";
    mutable std::mutex mutex_;
    std::stop_source lifetime_;
    std::atomic<bool> isConnected_ = false;
    std::atomic<bool> isReady_ = false;
    std::atomic<bool> capturing_ = false;
    std::atomic<bool> loading_ = false;
    std::atomic<bool> dirty_ = false;
    std::atomic<bool> closed_ = false;
    std::atomic<bool> captureChanging_ = false;
    std::atomic<uint64_t> revision_ = 0;
    std::atomic<uint64_t> targetRevision_ = 0;
    std::atomic<uint64_t> saveGeneration_ = 0;
    std::atomic<uint64_t> captureGeneration_ = 0;
    mutable std::mutex updateMutex_;
    StateChangedHandler stateChanged_;
    StateChangedHandler documentChanged_;
    NotificationHandler notification_;
};

}
