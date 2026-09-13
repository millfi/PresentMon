// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "AppSession.h"

#include "../Core/AutomaticTargeting.h"
#include "../Core/ConfigurationStore.h"
#include "../Core/Specification.h"
#include "../Interop/KernelClient.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <coroutine>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <thread>

namespace pmon::ui::services {
namespace {

class DispatcherAwaiter {
public:
    explicit DispatcherAwaiter(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher)
        : dispatcher_(dispatcher)
    {}

    bool await_ready() const noexcept
    {
        return dispatcher_.HasThreadAccess();
    }

    void await_suspend(std::coroutine_handle<> continuation) const
    {
        if (!dispatcher_.TryEnqueue([continuation] { continuation.resume(); })) {
            throw winrt::hresult_canceled();
        }
    }

    void await_resume() const noexcept {}

private:
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{ nullptr };
};

DispatcherAwaiter ResumeOnDispatcher(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher)
{
    return DispatcherAwaiter(dispatcher);
}

std::vector<std::shared_ptr<core::Widget>> CloneWidgets(std::vector<std::shared_ptr<core::Widget>> const& widgets)
{
    std::vector<std::shared_ptr<core::Widget>> clone;
    clone.reserve(widgets.size());
    for (auto const& widget : widgets) {
        if (widget) {
            clone.push_back(widget->Clone());
        }
    }
    return clone;
}

std::filesystem::path PathFromUtf8(std::string const& value)
{
    return std::filesystem::u8path(value);
}

std::string Utf8FromPath(std::filesystem::path const& value)
{
    auto const text = value.u8string();
    return std::string(reinterpret_cast<char const*>(text.data()), text.size());
}

bool IsProcessRunning(int pid)
{
    auto const process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (process == nullptr) {
        return false;
    }
    DWORD exitCode = 0;
    auto const valid = GetExitCodeProcess(process, &exitCode) != FALSE;
    CloseHandle(process);
    return valid && exitCode == STILL_ACTIVE;
}

std::string ActionName(core::HotkeyAction action)
{
    switch (action) {
    case core::HotkeyAction::ToggleCapture: return "ToggleCapture";
    case core::HotkeyAction::ToggleOverlay: return "ToggleOverlay";
    case core::HotkeyAction::CyclePreset: return "CyclePreset";
    case core::HotkeyAction::ToggleEtlLogging: return "ToggleEtlLogging";
    }
    return {};
}

}

AppSession::AppSession(StartupOptions options, winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
    : options_(std::move(options))
    , dispatcher_(dispatcher)
    , store_(std::make_unique<core::ConfigurationStore>(PathFromUtf8(options_.DataDirectory),
        PathFromUtf8(options_.InstallDirectory).append("Presets")))
{
    appInfo_.emplace("Application", "Intel PresentMon");
    appInfo_.emplace("User interface", "WinUI 3 / Windows App SDK");
    appInfo_.emplace("Architecture", sizeof(void*) == 8 ? "X64" : "X86");
}

AppSession::~AppSession()
{
    lifetime_.request_stop();
    if (kernel_) {
        kernel_->Close();
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::InitializeAsync()
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    LoadBlocklist();
    loading_ = true;
    std::vector<std::string> bindNotifications;
    bool canceled = false;
    std::string initializationError;
    try {
        if (!options_.PipeName.empty()) {
            auto pipeName = options_.PipeName;
            auto token = lifetime_.get_token();
            co_await winrt::resume_background();
            auto connected = interop::KernelClient::Connect(pipeName, token);
            auto introspection = connected->Introspect(token);
            auto session = connected->Session();
            co_await ResumeOnDispatcher(dispatcher_);
            if (closed_) {
                connected->Close();
                co_return;
            }
            kernel_ = std::shared_ptr<interop::KernelClient>(connected.release());
            introspection_ = std::move(introspection);
            isConnected_ = true;
            connectionStatus_ = "Connected";
            appInfo_.insert_or_assign("Kernel connection", pipeName);
            appInfo_.insert_or_assign("Service version", session.ServiceVersion);
            appInfo_.insert_or_assign("Service build", session.ServiceBuildId);
            appInfo_.insert_or_assign("Service build time", session.ServiceBuildTime);
            appInfo_.insert_or_assign("Middleware API", session.MiddlewareApiVersion);
        } else {
            connectionStatus_ = "Not connected. Start PresentMon.exe to enable tracking and capture.";
        }

        co_await winrt::resume_background();
        auto preferences = store_->LoadPreferences(introspection_);
        auto warning = store_->LastWarning();
        co_await ResumeOnDispatcher(dispatcher_);
        ThrowIfClosed();
        preferenceFile_ = std::move(preferences);
        if (warning) {
            Notify(*warning);
        }
        LoadSelectedPreset();
        co_await RefreshProcessesAsync();
        if (kernel_) {
            auto bindings = preferenceFile_.HotkeyBindings;
            auto client = kernel_;
            auto token = lifetime_.get_token();
            co_await winrt::resume_background();
            for (auto const& entry : bindings) {
                try {
                    if (entry.second.Combination) {
                        client->BindHotkey(entry.second, token);
                    } else {
                        client->ClearHotkey((int)entry.second.Action, token);
                    }
                } catch (std::exception const& error) {
                    bindNotifications.push_back("Unable to bind " + ActionName(entry.second.Action) + ": " + error.what());
                }
            }
            co_await ResumeOnDispatcher(dispatcher_);
            for (auto const& notification : bindNotifications) {
                Notify(notification);
            }
            co_await PushAsync();
        }
    } catch (winrt::hresult_canceled const&) {
        canceled = true;
    } catch (std::exception const& error) {
        initializationError = error.what();
    }
    co_await ResumeOnDispatcher(dispatcher_);
    if (canceled && !closed_) {
        connectionStatus_ = "PresentMon disconnected";
    }
    if (!initializationError.empty()) {
        isConnected_ = false;
        connectionStatus_ = "Unable to connect to PresentMon";
        Notify(initializationError + " Start the matching PresentMon.exe build with PresentMon Service running.");
    }
    loading_ = false;
    isReady_ = true;
    EmitDocumentChanged();
    EmitStateChanged();
    if (isConnected_ && !closed_) {
        PollProcessesAsync();
        ReceiveEventsAsync();
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::CloseAsync()
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    if (closed_.exchange(true)) {
        co_return;
    }
    ++saveGeneration_;
    ++captureGeneration_;
    capturing_ = false;
    lifetime_.request_stop();
    try {
        co_await FlushAsync();
    } catch (std::exception const& error) {
        Notify(std::string("Shutdown: ") + error.what());
    }
    auto client = std::move(kernel_);
    isConnected_ = false;
    if (client) {
        co_await winrt::resume_background();
        client->Close();
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::RefreshProcessesAsync()
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    auto blocklist = preferenceFile_.Preferences.EnableTargetBlocklist ? blocklist_ : std::unordered_set<std::string>{};
    auto const useBlocklist = preferenceFile_.Preferences.EnableTargetBlocklist;
    auto token = lifetime_.get_token();
    co_await winrt::resume_background();
    auto processes = WindowsServices::EnumerateProcesses(useBlocklist ? &blocklist : nullptr, token);
    co_await ResumeOnDispatcher(dispatcher_);
    if (closed_) {
        co_return;
    }
    processes_ = std::move(processes);
    EmitStateChanged();
}

winrt::Windows::Foundation::IAsyncAction AppSession::SelectProcessAsync(std::optional<int> pid,
    std::function<bool()> isCurrent)
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    if (isCurrent && !isCurrent()) {
        co_return;
    }
    if (pid == selectedPid_) {
        co_return;
    }
    if (capturing_) {
        co_await SetCaptureAsync(false);
    }
    if (isCurrent && !isCurrent()) {
        co_return;
    }
    auto const previous = selectedPid_;
    selectedPid_ = pid;
    ++targetRevision_;
    try {
        co_await PushAsync();
    } catch (...) {
        selectedPid_ = previous;
        ++targetRevision_;
        throw;
    }
    EmitStateChanged();
}

winrt::Windows::Foundation::IAsyncAction AppSession::SelectPresetAsync(core::Preset preset)
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    if (preferenceFile_.Preferences.SelectedPreset == preset && loadedPreset_ == preset) {
        co_return;
    }
    if (preferenceFile_.Preferences.SelectedPreset == core::Preset::Custom && dirty_) {
        core::LoadoutFile custom;
        custom.Widgets = CloneWidgets(widgets_);
        auto store = store_.get();
        co_await winrt::resume_background();
        store->SaveCustom(custom);
        co_await ResumeOnDispatcher(dispatcher_);
        ThrowIfClosed();
    }
    loading_ = true;
    preferenceFile_.Preferences.SelectedPreset = preset;
    try {
        LoadSelectedPreset();
    } catch (...) {
        loading_ = false;
        throw;
    }
    loading_ = false;
    EmitDocumentChanged();
    Changed();
    co_await FlushAsync();
}

winrt::Windows::Foundation::IAsyncAction AppSession::ToggleCaptureAsync()
{
    co_await SetCaptureAsync(!capturing_);
}

winrt::Windows::Foundation::IAsyncAction AppSession::SetCaptureAsync(bool active)
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    while (captureChanging_.exchange(true)) {
        co_await winrt::resume_after(std::chrono::milliseconds(10));
        co_await ResumeOnDispatcher(dispatcher_);
        ThrowIfClosed();
    }
    auto clearCaptureChange = [this] { captureChanging_ = false; };
    try {
    if (active == capturing_) {
        clearCaptureChange();
        co_return;
    }
    if (!kernel_ || !isConnected_) {
        throw std::runtime_error("PresentMon is not connected.");
    }
    if (active && !selectedPid_) {
        throw std::runtime_error("Select an application before starting capture.");
    }
    auto const targetRevision = targetRevision_.load();
    co_await FlushAsync();
    if (active && (!selectedPid_ || targetRevision != targetRevision_ || !isConnected_)) {
        throw std::runtime_error("The target changed before capture could start.");
    }
    auto client = kernel_;
    auto token = lifetime_.get_token();
    co_await winrt::resume_background();
    client->SetCapture(active, token);
    co_await ResumeOnDispatcher(dispatcher_);
    if (active && (!selectedPid_ || targetRevision != targetRevision_ || !isConnected_)) {
        co_await winrt::resume_background();
        if (client->IsConnected()) {
            client->SetCapture(false, token);
        }
        co_await ResumeOnDispatcher(dispatcher_);
        throw std::runtime_error("Capture stopped because the target changed during startup.");
    }
    capturing_ = active;
    auto const generation = ++captureGeneration_;
    if (active && preferenceFile_.Preferences.EnableCaptureDuration) {
        StopCaptureAfterDelayAsync(generation, preferenceFile_.Preferences.CaptureDuration);
    }
    EmitStateChanged();
    clearCaptureChange();
    } catch (...) {
        clearCaptureChange();
        throw;
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::BindHotkeyAsync(core::HotkeyBinding binding)
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    if (binding.Combination) {
        for (auto const& entry : preferenceFile_.HotkeyBindings) {
            if (entry.second.Action != binding.Action && entry.second.Combination
                && binding.Combination->Matches(&*entry.second.Combination)) {
                throw std::runtime_error("This shortcut is already assigned to another PresentMon action.");
            }
        }
    }
    if (kernel_ && isConnected_) {
        auto client = kernel_;
        auto token = lifetime_.get_token();
        co_await winrt::resume_background();
        if (binding.Combination) {
            client->BindHotkey(binding, token);
        } else {
            client->ClearHotkey((int)binding.Action, token);
        }
        co_await ResumeOnDispatcher(dispatcher_);
        ThrowIfClosed();
    }
    preferenceFile_.HotkeyBindings.insert_or_assign(ActionName(binding.Action), std::move(binding));
    Changed();
    co_await FlushAsync();
}

winrt::Windows::Foundation::IAsyncAction AppSession::ResetPreferencesAsync()
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    if (capturing_) {
        co_await SetCaptureAsync(false);
    }
    loading_ = true;
    preferenceFile_ = core::PreferenceFile{};
    preferenceFile_.Preferences = core::Preferences::CreateDefault(introspection_);
    try {
        if (kernel_ && isConnected_) {
            auto bindings = preferenceFile_.HotkeyBindings;
            auto client = kernel_;
            auto token = lifetime_.get_token();
            co_await winrt::resume_background();
            for (auto const& entry : bindings) {
                if (entry.second.Combination) {
                    client->BindHotkey(entry.second, token);
                } else {
                    client->ClearHotkey((int)entry.second.Action, token);
                }
            }
            co_await ResumeOnDispatcher(dispatcher_);
            ThrowIfClosed();
        }
        LoadSelectedPreset();
    } catch (...) {
        loading_ = false;
        throw;
    }
    loading_ = false;
    EmitDocumentChanged();
    Changed();
    co_await FlushAsync();
}

winrt::Windows::Foundation::IAsyncAction AppSession::ImportLoadoutAsync(std::string const& path)
{
    auto self = shared_from_this();
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    auto preferences = preferenceFile_.Preferences;
    auto store = store_.get();
    auto intro = introspection_;
    auto loadoutPath = PathFromUtf8(path);
    co_await winrt::resume_background();
    auto document = store->LoadLoadout(loadoutPath, intro, preferences);
    co_await ResumeOnDispatcher(dispatcher_);
    ThrowIfClosed();
    loading_ = true;
    preferenceFile_.Preferences.SelectedPreset = core::Preset::Custom;
    loadedPreset_ = core::Preset::Custom;
    widgets_ = std::move(document.Widgets);
    loading_ = false;
    EmitDocumentChanged();
    Changed();
    co_await FlushAsync();
}

void AppSession::ExportLoadout(std::string const& path)
{
    ThrowIfClosed();
    core::LoadoutFile document;
    document.Widgets = CloneWidgets(widgets_);
    store_->SaveLoadout(PathFromUtf8(path), document);
}

void AppSession::ExploreFolder(std::string const& kind)
{
    std::filesystem::path path;
    if (kind == "blocklist") {
        if (blocklistPath_.empty()) {
            throw std::runtime_error("No target blocklist was found in the application or user data directory.");
        }
        path = PathFromUtf8(blocklistPath_);
    } else if (kind == "captures") {
        path = CaptureDirectory();
    } else if (kind == "etls") {
        path = PathFromUtf8(options_.DataDirectory) / "Etl";
    } else if (kind == "logs") {
        path = PathFromUtf8(options_.LogDirectory);
    } else {
        path = PathFromUtf8(options_.DataDirectory);
    }
    std::filesystem::create_directories(kind == "blocklist" ? path.parent_path() : path);
    auto const target = path.wstring();
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void AppSession::Changed()
{
    if (loading_ || closed_ || !isConnected_) {
        return;
    }
    dirty_ = true;
    ++revision_;
    auto const generation = ++saveGeneration_;
    SaveAfterDelayAsync(generation);
    EmitStateChanged();
}

void AppSession::Notify(std::string const& message) const
{
    auto callback = notification_;
    if (callback) {
        if (dispatcher_.HasThreadAccess()) {
            callback(message);
        } else {
            dispatcher_.TryEnqueue([callback = std::move(callback), message] { callback(message); });
        }
    }
    try {
        std::filesystem::create_directories(PathFromUtf8(options_.LogDirectory));
        std::ofstream log(PathFromUtf8(options_.LogDirectory) / "PresentMonUI.log", std::ios::app);
        log << message << '\n';
    } catch (...) {
    }
}

core::HotkeyBinding AppSession::GetBinding(core::HotkeyAction action) const
{
    auto const found = preferenceFile_.HotkeyBindings.find(ActionName(action));
    return found == preferenceFile_.HotkeyBindings.end() ? core::HotkeyBinding{ action } : found->second;
}

StartupOptions const& AppSession::Options() const noexcept { return options_; }
core::PreferenceFile const& AppSession::PreferenceFile() const noexcept { return preferenceFile_; }
core::PreferenceFile& AppSession::PreferenceFile() noexcept { return preferenceFile_; }
core::Preferences const& AppSession::Preferences() const noexcept { return preferenceFile_.Preferences; }
core::Preferences& AppSession::Preferences() noexcept { return preferenceFile_.Preferences; }
core::IntrospectionData const& AppSession::Introspection() const noexcept { return introspection_; }
std::vector<std::shared_ptr<core::Widget>> const& AppSession::Widgets() const noexcept { return widgets_; }
std::vector<std::shared_ptr<core::Widget>>& AppSession::Widgets() noexcept { return widgets_; }
std::vector<ProcessEntry> const& AppSession::Processes() const noexcept { return processes_; }
std::unordered_map<std::string, std::string> const& AppSession::AppInfo() const noexcept { return appInfo_; }
bool AppSession::IsConnected() const noexcept { return isConnected_; }
bool AppSession::IsReady() const noexcept { return isReady_; }
bool AppSession::Capturing() const noexcept { return capturing_; }
std::string const& AppSession::ConnectionStatus() const noexcept { return connectionStatus_; }
std::optional<int> AppSession::SelectedPid() const noexcept { return selectedPid_; }
ProcessEntry const* AppSession::SelectedProcess() const noexcept
{
    if (!selectedPid_) {
        return nullptr;
    }
    auto const found = std::ranges::find(processes_, *selectedPid_, &ProcessEntry::Pid);
    return found == processes_.end() ? nullptr : &*found;
}

std::string AppSession::CaptureDirectory() const
{
    auto const base = options_.FilesWorking ? std::filesystem::current_path() : core::ConfigurationStore::DefaultDataDirectory();
    return Utf8FromPath(base / "Captures");
}

void AppSession::StateChanged(StateChangedHandler handler) { stateChanged_ = std::move(handler); }
void AppSession::DocumentChanged(StateChangedHandler handler) { documentChanged_ = std::move(handler); }
void AppSession::Notification(NotificationHandler handler) { notification_ = std::move(handler); }

winrt::Windows::Foundation::IAsyncAction AppSession::FlushAsync()
{
    co_await ResumeOnDispatcher(dispatcher_);
    if (!dirty_) {
        co_return;
    }
    auto const savedRevision = revision_.load();
    auto preferenceSnapshot = preferenceFile_;
    auto widgetSnapshot = CloneWidgets(widgets_);
    auto specification = core::SpecificationBuilder::Build(selectedPid_, preferenceSnapshot.Preferences, widgetSnapshot, introspection_);
    core::LoadoutFile loadoutSnapshot;
    loadoutSnapshot.Widgets = CloneWidgets(widgetSnapshot);
    auto client = kernel_;
    auto store = store_.get();
    auto token = lifetime_.get_token();
    co_await winrt::resume_background();
    if (lifetime_.stop_requested()) {
        store->SavePreferences(preferenceSnapshot);
        if (preferenceSnapshot.Preferences.SelectedPreset == core::Preset::Custom) {
            store->SaveCustom(loadoutSnapshot);
        }
    } else {
        std::scoped_lock updateLock(updateMutex_);
        if (!closed_ && !lifetime_.stop_requested() && client && isConnected_) {
            client->PushSpecification(specification, token);
        }
        if (!closed_ && !lifetime_.stop_requested()) {
            store->SavePreferences(preferenceSnapshot);
            if (preferenceSnapshot.Preferences.SelectedPreset == core::Preset::Custom) {
                store->SaveCustom(loadoutSnapshot);
            }
        }
    }
    co_await ResumeOnDispatcher(dispatcher_);
    if (!closed_) {
        dirty_ = revision_ != savedRevision;
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::PushAsync()
{
    co_await ResumeOnDispatcher(dispatcher_);
    if (!kernel_ || !isConnected_) {
        co_return;
    }
    auto specification = core::SpecificationBuilder::Build(selectedPid_, preferenceFile_.Preferences, widgets_, introspection_);
    auto client = kernel_;
    auto token = lifetime_.get_token();
    co_await winrt::resume_background();
    client->PushSpecification(specification, token);
    co_await ResumeOnDispatcher(dispatcher_);
}

winrt::Windows::Foundation::IAsyncAction AppSession::SaveAfterDelayAsync(uint64_t generation)
{
    auto self = shared_from_this();
    co_await winrt::resume_after(std::chrono::milliseconds(400));
    co_await ResumeOnDispatcher(dispatcher_);
    if (closed_ || lifetime_.stop_requested() || generation != saveGeneration_) {
        co_return;
    }
    try {
        co_await FlushAsync();
    } catch (std::exception const& error) {
        Notify(std::string("Unable to apply settings: ") + error.what());
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::StopCaptureAfterDelayAsync(uint64_t generation, double seconds)
{
    auto self = shared_from_this();
    auto const duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(seconds));
    co_await winrt::resume_after(duration);
    co_await ResumeOnDispatcher(dispatcher_);
    if (closed_ || lifetime_.stop_requested() || generation != captureGeneration_) {
        co_return;
    }
    try {
        co_await SetCaptureAsync(false);
    } catch (std::exception const& error) {
        Notify(std::string("Unable to stop capture: ") + error.what());
    }
}

void AppSession::LoadBlocklist()
{
    std::vector<std::filesystem::path> candidates;
    if (options_.FilesWorking) {
        candidates.emplace_back(PathFromUtf8(options_.InstallDirectory) / "BlockLists" / "TargetBlockList.txt");
        candidates.emplace_back(PathFromUtf8(options_.InstallDirectory) / "TargetBlockList.txt");
    } else {
        candidates.emplace_back(PathFromUtf8(options_.AppDataDirectory) / "TargetBlockList.txt");
        candidates.emplace_back(PathFromUtf8(options_.InstallDirectory) / "TargetBlockList.txt");
    }
    for (auto const& path : candidates) {
        std::ifstream file(path);
        if (!file) {
            continue;
        }
        std::string line;
        while (std::getline(file, line)) {
            auto const first = line.find_first_not_of(" \t\r\n");
            auto const last = line.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) {
                auto value = line.substr(first, last - first + 1);
                std::ranges::transform(value, value.begin(), [](unsigned char character) { return (char)std::tolower(character); });
                blocklist_.insert(std::move(value));
            }
        }
        blocklistPath_ = Utf8FromPath(path);
        return;
    }
}

void AppSession::LoadSelectedPreset()
{
    auto const preset = preferenceFile_.Preferences.SelectedPreset.value_or(core::Preset::Basic);
    auto document = preset == core::Preset::Custom
        ? store_->LoadCustom(introspection_, preferenceFile_.Preferences)
        : store_->LoadPreset((int)preset, introspection_, preferenceFile_.Preferences);
    widgets_ = std::move(document.Widgets);
    loadedPreset_ = preset;
    auto const warning = store_->LastWarning();
    if (warning) {
        Notify(*warning);
    }
}

void AppSession::EmitStateChanged() const
{
    if (stateChanged_) {
        stateChanged_();
    }
}

void AppSession::EmitDocumentChanged() const
{
    if (documentChanged_) {
        documentChanged_();
    }
}

void AppSession::ThrowIfClosed() const
{
    if (closed_) {
        throw winrt::hresult_canceled();
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::ReceiveEventsAsync()
{
    auto self = shared_from_this();
    auto client = kernel_;
    if (!client) {
        co_return;
    }
    std::string terminalError;
    try {
        co_await winrt::resume_background();
        interop::KernelEvent event;
        while (!lifetime_.stop_requested() && client->WaitEvent(event, lifetime_.get_token())) {
            co_await ResumeOnDispatcher(dispatcher_);
            if (closed_) {
                co_return;
            }
            try {
                co_await HandleEventAsync(event);
            } catch (std::exception const& error) {
                Notify(std::string("PresentMon event: ") + error.what());
            }
            co_await winrt::resume_background();
        }
    } catch (winrt::hresult_canceled const&) {
    } catch (std::exception const& error) {
        terminalError = error.what();
    }
    if (!terminalError.empty()) {
        co_await ResumeOnDispatcher(dispatcher_);
        if (!closed_) {
            isConnected_ = false;
            capturing_ = false;
            connectionStatus_ = "PresentMon disconnected";
            Notify(terminalError);
            EmitStateChanged();
        }
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::HandleEventAsync(interop::KernelEvent event)
{
    co_await ResumeOnDispatcher(dispatcher_);
    switch (event.Kind) {
    case interop::KernelEventKind::HotkeyFired:
        switch ((core::HotkeyAction)event.ActionId.value_or(-1)) {
        case core::HotkeyAction::ToggleCapture: co_await ToggleCaptureAsync(); break;
        case core::HotkeyAction::ToggleOverlay:
            preferenceFile_.Preferences.HideAlways = !preferenceFile_.Preferences.HideAlways;
            Changed();
            break;
        case core::HotkeyAction::CyclePreset:
            co_await SelectPresetAsync(core::Preferences::NextPreset(preferenceFile_.Preferences.SelectedPreset));
            break;
        case core::HotkeyAction::ToggleEtlLogging: Notify("ETL capture is currently disabled."); break;
        }
        break;
    case interop::KernelEventKind::TargetLost:
    case interop::KernelEventKind::StalePid:
    case interop::KernelEventKind::OverlayDied:
        if (event.Kind == interop::KernelEventKind::TargetLost && event.ProcessId
            && selectedPid_ != (int)*event.ProcessId) {
            co_return;
        }
        selectedPid_.reset();
        ++targetRevision_;
        capturing_ = false;
        ++captureGeneration_;
        if (event.Kind != interop::KernelEventKind::TargetLost) {
            Notify(event.Kind == interop::KernelEventKind::StalePid
                ? "Selected process has already exited." : "The overlay stopped unexpectedly.");
        }
        EmitStateChanged();
        break;
    case interop::KernelEventKind::PresentmonInitFailed:
        Notify("Failed to initialize PresentMon API. Ensure PresentMon Service is running.");
        break;
    }
}

winrt::Windows::Foundation::IAsyncAction AppSession::PollProcessesAsync()
{
    auto self = shared_from_this();
    co_await winrt::resume_background();
    auto lastScan = std::chrono::steady_clock::now();
    bool probeActive = false;
    while (!lifetime_.stop_requested()) {
        for (int elapsed = 0; elapsed < 250 && !lifetime_.stop_requested(); elapsed += 25) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (lifetime_.stop_requested()) {
            co_return;
        }
        co_await ResumeOnDispatcher(dispatcher_);
        if (closed_ || !isConnected_) {
            co_return;
        }
        auto const automatic = preferenceFile_.Preferences.EnableAutotargetting;
        auto const selected = selectedPid_;
        auto const scanDue = automatic && std::chrono::steady_clock::now() - lastScan >= std::chrono::seconds(1);
        auto const targetRevision = targetRevision_.load();
        auto const settingsRevision = revision_.load();
        auto const useBlocklist = preferenceFile_.Preferences.EnableTargetBlocklist;
        auto blocklist = blocklist_;
        auto client = kernel_;
        auto token = lifetime_.get_token();
        co_await winrt::resume_background();
        std::string automaticTargetingError;
        try {
            if (scanDue && client) {
                lastScan = std::chrono::steady_clock::now();
                auto samples = WindowsServices::GetGpuProcessSamples(useBlocklist ? &blocklist : nullptr, token);
                std::vector<int> active;
                for (auto const& sample : samples) {
                    if (std::isfinite(sample.RunningTime) && sample.RunningTime > 0.0) {
                        active.push_back(sample.Pid);
                    }
                }
                std::ranges::sort(active);
                active.erase(std::unique(active.begin(), active.end()), active.end());
                auto measurable = client->ProbeGpuBusy(active, token);
                std::unordered_set<int> measurableSet(measurable.begin(), measurable.end());
                std::optional<int> best;
                double bestTime = -1.0;
                for (auto const& sample : samples) {
                    if (sample.RunningTime > bestTime && measurableSet.contains(sample.Pid)) {
                        best = sample.Pid;
                        bestTime = sample.RunningTime;
                    } else if (sample.RunningTime == bestTime && best && sample.Pid < *best
                        && measurableSet.contains(sample.Pid)) {
                        best = sample.Pid;
                    }
                }
                co_await ResumeOnDispatcher(dispatcher_);
                if (!closed_ && isConnected_ && preferenceFile_.Preferences.EnableAutotargetting
                    && targetRevision == targetRevision_ && settingsRevision == revision_ && best && best != selectedPid_) {
                    co_await RefreshProcessesAsync();
                    auto const exists = std::ranges::any_of(processes_, [best](ProcessEntry const& process) { return process.Pid == *best; });
                    if (exists) {
                        co_await SelectProcessAsync(best, [this, targetRevision, settingsRevision] {
                            return !closed_ && isConnected_ && preferenceFile_.Preferences.EnableAutotargetting
                                && targetRevision == targetRevision_ && settingsRevision == revision_;
                        });
                    }
                }
                probeActive = true;
                co_await winrt::resume_background();
            } else if (!automatic && probeActive && client) {
                client->ProbeGpuBusy({}, token);
                probeActive = false;
            }
            if (selected && !IsProcessRunning(*selected)) {
                co_await ResumeOnDispatcher(dispatcher_);
                if (targetRevision == targetRevision_) {
                    co_await SelectProcessAsync(std::nullopt, [this, targetRevision] {
                        return !closed_ && targetRevision == targetRevision_;
                    });
                }
                co_await winrt::resume_background();
            }
        } catch (std::exception const& error) {
            automaticTargetingError = error.what();
        }
        if (!automaticTargetingError.empty()) {
            co_await ResumeOnDispatcher(dispatcher_);
            if (!closed_) {
                Notify(std::string("Automatic targeting: ") + automaticTargetingError);
            }
            co_await winrt::resume_background();
            for (int elapsed = 0; elapsed < 5000 && !lifetime_.stop_requested(); elapsed += 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

}
