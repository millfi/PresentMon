// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "../Core/ConfigurationStore.h"
#include "../Core/Specification.h"
#include "../Interop/KernelClient.h"
#include "../Interop/KernelProtocol.h"

#include <Windows.h>
#include <evntrace.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <stop_token>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace pmon::ui::tests
{
    namespace
    {
        using namespace std::chrono_literals;

        constexpr auto kOverlayTitle = L"P2C#OVERLAY";
        constexpr auto kOverlayClass = L"PMON2-CAP-CLS";
        constexpr auto kOverlayAppearTimeout = 45s;
        constexpr auto kOverlayStableDuration = 5s;
        constexpr auto kUiInitializationDelay = 2s;
        constexpr auto kUiIdentityProperty = L"FluentPresentMon.UiMutexSuffixAtom";

        struct SmokeReport
        {
            std::wstring RunId;
            bool Complete = false;
            std::vector<std::string> Steps;
            std::string Failure;
        };

        class ScopedHandle
        {
        public:
            ScopedHandle() = default;
            explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
            ~ScopedHandle() { Reset(); }
            ScopedHandle(const ScopedHandle&) = delete;
            ScopedHandle& operator=(const ScopedHandle&) = delete;
            ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.Release()) {}
            ScopedHandle& operator=(ScopedHandle&& other) noexcept
            {
                if (this != &other) Reset(other.Release());
                return *this;
            }
            HANDLE Get() const noexcept { return handle_; }
            HANDLE Release() noexcept { return std::exchange(handle_, nullptr); }
            void Reset(HANDLE handle = nullptr) noexcept
            {
                if (handle_) CloseHandle(handle_);
                handle_ = handle;
            }
        private:
            HANDLE handle_ = nullptr;
        };

        class KillOnCloseJob
        {
        public:
            KillOnCloseJob()
                : job_(CreateJobObjectW(nullptr, nullptr))
            {
                if (!job_.Get()) throw std::system_error((int)GetLastError(), std::system_category(), "Unable to create overlay smoke job");
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
                information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!SetInformationJobObject(job_.Get(), JobObjectExtendedLimitInformation, &information, sizeof(information))) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Unable to configure overlay smoke job");
                }
            }

            void Attach(HANDLE process)
            {
                if (!AssignProcessToJobObject(job_.Get(), process)) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Unable to assign process to overlay smoke job");
                }
            }

            void Terminate() noexcept
            {
                if (job_.Get()) TerminateJobObject(job_.Get(), 1);
            }
        private:
            ScopedHandle job_;
        };

        void StopOwnedTraceSession(const std::wstring& name) noexcept
        {
            if (name.empty()) return;
            struct Properties : EVENT_TRACE_PROPERTIES
            {
                wchar_t LoggerName[1024];
            } properties{};
            properties.Wnode.BufferSize = sizeof(properties);
            properties.LoggerNameOffset = offsetof(Properties, LoggerName);
            const auto status = ControlTraceW(0, name.c_str(), &properties, EVENT_TRACE_CONTROL_STOP);
            if (status != ERROR_SUCCESS && status != ERROR_WMI_INSTANCE_NOT_FOUND && status != ERROR_FILE_NOT_FOUND) {
                std::cerr << "Unable to stop overlay smoke ETW session: " << status << '\n';
            }
        }

        class OverlaySmokeCleanup
        {
        public:
            explicit OverlaySmokeCleanup(std::wstring etwSession) : etwSession_(std::move(etwSession)) {}
            ~OverlaySmokeCleanup()
            {
                Terminate();
                StopOwnedTraceSession(etwSession_);
            }

            KillOnCloseJob& Job() noexcept { return job_; }
            void Terminate() noexcept { job_.Terminate(); }
            void StopTraceSession() noexcept { StopOwnedTraceSession(etwSession_); }

        private:
            KillOnCloseJob job_;
            std::wstring etwSession_;
        };

        class ChildProcess
        {
        public:
            ChildProcess() = default;
            ~ChildProcess() { Close(); }
            ChildProcess(const ChildProcess&) = delete;
            ChildProcess& operator=(const ChildProcess&) = delete;

            void Start(const std::wstring& command, const std::filesystem::path& workingDirectory, KillOnCloseJob* job)
            {
                std::vector<wchar_t> commandLine(command.begin(), command.end());
                commandLine.push_back(L'\0');
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                startup.dwFlags = STARTF_USESHOWWINDOW;
                startup.wShowWindow = SW_SHOWNOACTIVATE;
                PROCESS_INFORMATION process{};
                if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, workingDirectory.c_str(), &startup, &process)) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Unable to start overlay smoke process");
                }
                process_.Reset(process.hProcess);
                thread_.Reset(process.hThread);
                try {
                    // Packaged processes already belong to a Windows-managed job.
                    if (job) job->Attach(process_.Get());
                    if (ResumeThread(thread_.Get()) == (DWORD)-1) {
                        throw std::system_error((int)GetLastError(), std::system_category(), "Unable to resume overlay smoke process");
                    }
                }
                catch (...) {
                    TerminateProcess(process_.Get(), 1);
                    throw;
                }
            }

            DWORD Id() const noexcept { return process_.Get() ? GetProcessId(process_.Get()) : 0; }
            DWORD ExitCode() const
            {
                if (!process_.Get()) return 0;
                DWORD exitCode = 0;
                if (!GetExitCodeProcess(process_.Get(), &exitCode)) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Unable to query overlay smoke process exit code");
                }
                return exitCode;
            }
            bool IsRunning() const
            {
                if (!process_.Get()) return false;
                const auto result = WaitForSingleObject(process_.Get(), 0);
                if (result == WAIT_TIMEOUT) return true;
                if (result == WAIT_OBJECT_0) return false;
                throw std::system_error((int)GetLastError(), std::system_category(), "Unable to query overlay smoke process");
            }

            void WaitForExit(DWORD timeoutMilliseconds) const
            {
                if (!process_.Get()) return;
                const auto result = WaitForSingleObject(process_.Get(), timeoutMilliseconds);
                if (result == WAIT_TIMEOUT) {
                    throw std::runtime_error("Timed out waiting for overlay smoke process cleanup.");
                }
                if (result != WAIT_OBJECT_0) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Unable to wait for overlay smoke process");
                }
            }

            void Close() noexcept
            {
                if (process_.Get() && WaitForSingleObject(process_.Get(), 0) == WAIT_TIMEOUT) {
                    TerminateProcess(process_.Get(), 1);
                    WaitForSingleObject(process_.Get(), 5000);
                }
                thread_.Reset();
                process_.Reset();
            }
        private:
            ScopedHandle process_;
            ScopedHandle thread_;
        };

        std::wstring Quote(const std::filesystem::path& path)
        {
            return L"\"" + path.wstring() + L"\"";
        }

        std::wstring Quote(const std::wstring& value)
        {
            return L"\"" + value + L"\"";
        }

        std::string Utf8(const std::wstring& value)
        {
            if (value.empty()) return {};
            const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
            std::string result((size_t)size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), size, nullptr, nullptr);
            return result;
        }

        std::string JsonString(const std::string& value)
        {
            std::string result;
            result.reserve(value.size() + 2);
            result.push_back('"');
            for (const auto character : value) {
                switch (character) {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result.push_back(character); break;
                }
            }
            result.push_back('"');
            return result;
        }

        void WriteReport(const std::filesystem::path& path, const SmokeReport& report)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Unable to write overlay smoke report.");
            output << "{\"runId\":" << JsonString(Utf8(report.RunId)) << ",\"complete\":"
                << (report.Complete ? "true" : "false") << ",\"steps\":[";
            for (size_t index = 0; index < report.Steps.size(); ++index) {
                if (index) output << ',';
                output << JsonString(report.Steps[index]);
            }
            output << "],\"failure\":" << JsonString(report.Failure) << "}";
            if (!output) throw std::runtime_error("Unable to finish overlay smoke report.");
        }

        std::wstring MakeSuffix(const std::wstring& runId)
        {
            std::wstring suffix;
            for (const auto character : runId) {
                if ((character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z')
                    || (character >= L'0' && character <= L'9')) {
                    suffix.push_back(character);
                }
            }
            if (suffix.empty()) throw std::invalid_argument("Overlay smoke run ID must include alphanumeric characters.");
            return suffix;
        }

        struct OverlaySearch
        {
            DWORD KernelPid = 0;
            HWND Window = nullptr;
        };

        BOOL CALLBACK FindOverlayWindow(HWND window, LPARAM data)
        {
            auto& search = *reinterpret_cast<OverlaySearch*>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid != search.KernelPid || !IsWindowVisible(window)) return TRUE;
            wchar_t title[64]{};
            wchar_t className[64]{};
            GetWindowTextW(window, title, (int)std::size(title));
            GetClassNameW(window, className, (int)std::size(className));
            if (std::wstring_view(title) == kOverlayTitle && std::wstring_view(className) == kOverlayClass) {
                search.Window = window;
                return FALSE;
            }
            return TRUE;
        }

        HWND FindOverlayWindowForKernel(DWORD kernelPid)
        {
            OverlaySearch search{ .KernelPid = kernelPid };
            EnumWindows(FindOverlayWindow, reinterpret_cast<LPARAM>(&search));
            return search.Window;
        }

        struct UiSearch
        {
            ATOM Identity = 0;
            HWND Window = nullptr;
        };

        BOOL CALLBACK FindUiWindow(HWND window, LPARAM data)
        {
            auto& search = *reinterpret_cast<UiSearch*>(data);
            if (!IsWindowVisible(window)) return TRUE;
            const auto property = GetPropW(window, kUiIdentityProperty);
            if ((ATOM)reinterpret_cast<UINT_PTR>(property) == search.Identity) {
                search.Window = window;
                return FALSE;
            }
            return TRUE;
        }

        void ThrowIfTimedOut(std::stop_token deadline)
        {
            if (deadline.stop_requested()) throw std::runtime_error("Overlay smoke exceeded its total timeout.");
        }

        void SleepWithDeadline(std::chrono::milliseconds duration, std::stop_token deadline)
        {
            const auto end = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < end) {
                ThrowIfTimedOut(deadline);
                std::this_thread::sleep_for(25ms);
            }
        }

        HWND WaitForUiInitialization(const std::wstring& mutexName, const ChildProcess& kernel, std::stop_token deadline)
        {
            while (true) {
                ThrowIfTimedOut(deadline);
                if (!kernel.IsRunning()) {
                    throw std::runtime_error("Staged kernel exited before its UI initialized with exit code " + std::to_string(kernel.ExitCode()) + ".");
                }
                const auto identity = GlobalFindAtomW(mutexName.c_str());
                if (identity != 0) {
                    UiSearch search{ .Identity = identity };
                    EnumWindows(FindUiWindow, reinterpret_cast<LPARAM>(&search));
                    if (search.Window) {
                        // Root_Loaded starts the asynchronous session initialization after this identity appears.
                        // Give that initial empty-specification push a bounded interval to finish before testing.
                        SleepWithDeadline(std::chrono::duration_cast<std::chrono::milliseconds>(kUiInitializationDelay), deadline);
                        return search.Window;
                    }
                }
                std::this_thread::sleep_for(50ms);
            }
        }

        void ThrowIfKernelExited(const ChildProcess& kernel)
        {
            if (!kernel.IsRunning()) {
                throw std::runtime_error("Staged kernel exited before overlay smoke completed with exit code " + std::to_string(kernel.ExitCode()) + ".");
            }
        }

        void CheckKernelEvents(const std::atomic_bool& failed, std::mutex& failureMutex, const std::string& failure)
        {
            if (!failed.load()) return;
            std::scoped_lock lock(failureMutex);
            throw std::runtime_error(failure.empty() ? "Kernel reported an overlay smoke failure." : failure);
        }

        core::Specification LoadBasicSpecification(const std::filesystem::path& dataDirectory,
            const std::filesystem::path& presetDirectory, int presenterPid, const interop::IntrospectionData& intro)
        {
            core::ConfigurationStore store(dataDirectory, presetDirectory);
            auto preferences = core::Preferences::CreateDefault(intro);
            preferences.SelectedPreset = core::Preset::Basic;
            preferences.HideAlways = false;
            preferences.HideDuringCapture = false;
            preferences.EnableAutotargetting = false;
            preferences.EnableTargetBlocklist = false;
            preferences.IndependentWindow = false;
            auto loadout = store.LoadPreset(0, intro, preferences);
            return core::SpecificationBuilder::Build(presenterPid, preferences, loadout.Widgets, intro);
        }

        void Run(const std::filesystem::path& presenterExecutable, const std::filesystem::path& kernelExecutable,
            const std::filesystem::path& apiDll, const std::filesystem::path& dataDirectory,
            const std::wstring& runId, DWORD timeoutMilliseconds, bool installedService)
        {
            const auto timeoutDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
            std::stop_source deadlineSource;
            std::jthread deadlineWatchdog([&](std::stop_token stop) {
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < timeoutDeadline) {
                    std::this_thread::sleep_for(25ms);
                }
                if (!stop.stop_requested()) deadlineSource.request_stop();
            });
            const auto deadline = deadlineSource.get_token();
            if (!std::filesystem::is_regular_file(presenterExecutable)) throw std::runtime_error("PresentBench executable is missing.");
            if (!std::filesystem::is_regular_file(kernelExecutable)) throw std::runtime_error("Staged PresentMon executable is missing.");
            if (!std::filesystem::is_regular_file(apiDll)) throw std::runtime_error("Staged PresentMon API DLL is missing.");
            const auto serviceExecutable = kernelExecutable.parent_path() / "PresentMonService.exe";
            if (!std::filesystem::is_regular_file(serviceExecutable)) throw std::runtime_error("Staged PresentMon service is missing.");
            const auto presetDirectory = std::filesystem::path{ [] {
                std::vector<wchar_t> path(32768);
                const auto length = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
                if (!length || length == path.size()) throw std::runtime_error("Unable to locate overlay smoke executable.");
                return std::filesystem::path(path.data()).parent_path();
            }() } / "Fixtures";
            if (!std::filesystem::is_directory(presetDirectory)) throw std::runtime_error("Overlay smoke preset fixtures are missing.");
            std::filesystem::create_directories(dataDirectory);
            const auto suffix = MakeSuffix(runId);
            const auto controlPipe = L"\\\\.\\pipe\\pm-overlay-smoke-" + suffix;
            const auto sharedMemory = L"pm-overlay-smoke-" + suffix;
            const auto etwSession = L"pm-overlay-smoke-" + suffix;
            const auto mutexName = L"OverlaySmokeUi-" + suffix;
            const auto logDirectory = dataDirectory / "logs";
            const auto serviceLogDirectory = logDirectory / "service";
            std::filesystem::create_directories(logDirectory);
            std::filesystem::create_directories(serviceLogDirectory);

            OverlaySmokeCleanup cleanup(installedService ? L"" : etwSession);
            ChildProcess presenter;
            ChildProcess kernel;
            presenter.Start(Quote(presenterExecutable) + L" /width=320 /height=240", presenterExecutable.parent_path(), &cleanup.Job());
            SleepWithDeadline(500ms, deadline);
            if (!presenter.IsRunning()) throw std::runtime_error("PresentBench exited before overlay smoke started.");

            const auto serviceArguments = installedService ? std::wstring{} :
                L" --svc-as-child --control-pipe " + Quote(controlPipe)
                + L" --shm-name-prefix " + Quote(sharedMemory)
                + L" --etw-session-name " + Quote(etwSession)
                + L" --svc-option log-dir " + Quote(serviceLogDirectory);
            const auto kernelCommand = Quote(kernelExecutable) + serviceArguments
                + L" --middleware-dll-path " + Quote(apiDll)
                + L" --ui-mutex-name " + Quote(mutexName)
                + L" --ui-data-directory " + Quote(dataDirectory)
                + L" --duplicate-ui-response no --files-working --log-level Debug --log-folder " + Quote(logDirectory.wstring());
            kernel.Start(kernelCommand, kernelExecutable.parent_path(), installedService ? nullptr : &cleanup.Job());
            const auto kernelPid = kernel.Id();
            const auto actionPipe = "ipm-cef-channel-" + std::to_string(kernelPid);
            const auto uiWindow = WaitForUiInitialization(mutexName, kernel, deadline);

            std::unique_ptr<interop::KernelClient> client;
            std::exception_ptr lastConnectError;
            while (!deadline.stop_requested()) {
                ThrowIfKernelExited(kernel);
                try {
                    client = interop::KernelClient::Connect(actionPipe, deadline);
                    break;
                }
                catch (...) {
                    ThrowIfTimedOut(deadline);
                    lastConnectError = std::current_exception();
                    std::this_thread::sleep_for(100ms);
                }
            }
            if (!client) {
                if (lastConnectError) std::rethrow_exception(lastConnectError);
                throw std::runtime_error("Timed out connecting to staged kernel action pipe.");
            }

            std::atomic_bool eventFailure = false;
            std::mutex eventFailureMutex;
            std::string eventFailureText;
            std::jthread eventReader([&](std::stop_token stop) {
                try {
                    while (!stop.stop_requested()) {
                        interop::KernelEvent event;
                        if (!client->WaitEvent(event, stop)) return;
                        if (event.Kind == interop::KernelEventKind::OverlayDied
                            || event.Kind == interop::KernelEventKind::PresentmonInitFailed
                            || event.Kind == interop::KernelEventKind::TargetLost
                            || event.Kind == interop::KernelEventKind::StalePid) {
                            std::scoped_lock lock(eventFailureMutex);
                            eventFailureText = "Kernel reported overlay failure event: " + std::to_string((int)event.Kind);
                            eventFailure = true;
                            return;
                        }
                    }
                }
                catch (const std::exception& error) {
                    if (!stop.stop_requested()) {
                        std::scoped_lock lock(eventFailureMutex);
                        eventFailureText = std::string("Unable to receive kernel events: ") + error.what();
                        eventFailure = true;
                    }
                }
            });

            const auto intro = client->Introspect(deadline);
            const auto specification = LoadBasicSpecification(dataDirectory, presetDirectory, (int)presenter.Id(), intro);
            client->PushSpecification(specification, deadline);

            HWND overlay = nullptr;
            const auto appearDeadline = (std::min)(std::chrono::steady_clock::now() + kOverlayAppearTimeout, timeoutDeadline);
            while (std::chrono::steady_clock::now() < appearDeadline && !deadline.stop_requested()) {
                CheckKernelEvents(eventFailure, eventFailureMutex, eventFailureText);
                ThrowIfKernelExited(kernel);
                overlay = FindOverlayWindowForKernel(kernelPid);
                if (overlay) break;
                std::this_thread::sleep_for(100ms);
            }
            ThrowIfTimedOut(deadline);
            if (!overlay) throw std::runtime_error("Staged kernel did not create a visible overlay window.");

            const auto stableDeadline = std::chrono::steady_clock::now() + kOverlayStableDuration;
            while (std::chrono::steady_clock::now() < stableDeadline && !deadline.stop_requested()) {
                CheckKernelEvents(eventFailure, eventFailureMutex, eventFailureText);
                ThrowIfKernelExited(kernel);
                if (!IsWindow(overlay) || !IsWindowVisible(overlay) || FindOverlayWindowForKernel(kernelPid) != overlay) {
                    throw std::runtime_error("Overlay window did not remain visible for five seconds.");
                }
                std::this_thread::sleep_for(100ms);
            }
            ThrowIfTimedOut(deadline);

            client->PushSpecification(core::Specification{}, deadline);
            eventReader.request_stop();
            eventReader.join();
            client->Close();
            PostMessageW(uiWindow, WM_CLOSE, 0, 0);
            kernel.WaitForExit(10000);
            if (kernel.ExitCode() != 0) throw std::runtime_error("Kernel did not shut down cleanly.");
            cleanup.Terminate();
            presenter.WaitForExit(5000);
            kernel.WaitForExit(5000);
            cleanup.StopTraceSession();
        }
    }

    int RunOverlaySmokeHost(int argc, wchar_t* argv[])
    {
        if (argc != 8 && argc != 9) {
            std::wcerr << L"Usage: PresentMonUI.OverlaySmokeHost.exe <presenter-exe> <kernel-exe> <api-dll> <report> <data-directory> <run-id> <timeout-seconds> [--installed-service]\n";
            return 2;
        }
        SmokeReport report{ .RunId = argv[6] };
        const std::filesystem::path reportPath{ argv[4] };
        try {
            const auto timeoutSeconds = wcstoul(argv[7], nullptr, 10);
            if (!timeoutSeconds || timeoutSeconds > 180) throw std::invalid_argument("Timeout must be between one and 180 seconds.");
            if (argc == 9 && std::wstring_view(argv[8]) != L"--installed-service") throw std::invalid_argument("Unknown overlay smoke option.");
            Run(argv[1], argv[2], argv[3], argv[5], report.RunId, (DWORD)(timeoutSeconds * 1000), argc == 9);
            report.Steps = { "presenter-started", "kernel-connected", "basic-spec-pushed", "overlay-visible", "overlay-stable", "cleanup" };
            report.Complete = true;
        }
        catch (const std::exception& error) {
            report.Failure = error.what();
        }
        try {
            WriteReport(reportPath, report);
        }
        catch (const std::exception& error) {
            std::cerr << "FAIL " << error.what() << '\n';
            return 1;
        }
        if (!report.Complete) {
            std::cerr << "FAIL " << report.Failure << '\n';
            return 1;
        }
        return 0;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    return pmon::ui::tests::RunOverlaySmokeHost(argc, argv);
}
