#include "pch.h"
#include "App.xaml.h"
#include "UiDispatcher.h"
#include "MainWindow.xaml.h"
#include "ThinAcrylicBackdrop.h"
#include "Core/StartupOptions.h"
#include "Tests/UiSmoke.h"
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace winrt::PresentMon::UI::implementation
{
    App::App()
    {
        UnhandledException([this](auto const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& args) {
            Log("Unhandled UI exception: " + to_string(args.Message()));
        });
        InitializeComponent();
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        try {
            int count = 0;
            auto const arguments = CommandLineToArgvW(GetCommandLineW(), &count);
            if (!arguments) throw_last_error();
            std::vector<std::string> values;
            for (int i = 1; i < count; ++i) values.push_back(to_string(arguments[i]));
            LocalFree(arguments);
            bool smoke = false, shellSmoke = false;
            std::string reportPath, runId;
            std::vector<std::string> startup;
            for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] == "--native-smoke-test") smoke = true;
                else if (values[i] == "--native-shell-smoke-test") shellSmoke = true;
                else if (values[i] == "--smoke-report" || values[i] == "--smoke-run-id") {
                    auto const name = values[i];
                    if (++i == values.size() || values[i].empty()) throw std::invalid_argument("Missing smoke option value.");
                    (name == "--smoke-report" ? reportPath : runId) = values[i];
                }
                else startup.push_back(values[i]);
            }
            if (smoke) {
                if (reportPath.empty() || runId.empty()) throw std::invalid_argument("Smoke tests require a report path and run ID.");
                RunSmokeAsync(std::move(reportPath), std::move(runId));
                return;
            }
            auto const options = pmon::ui::services::StartupOptions::Parse(startup);
            if (shellSmoke) {
                if (reportPath.empty() || runId.empty() || options.PipeName.empty()) {
                    throw std::invalid_argument("Shell smoke requires a mock pipe, report path and run ID.");
                }
                RunShellSmokeAsync(options, std::move(reportPath), std::move(runId));
                return;
            }
            logDirectory_ = options.LogDirectory;
            auto const mutexName = to_hstring("Local\\IntelPresentMon." + options.MutexSuffix);
            instanceMutex_.attach(CreateMutexW(nullptr, TRUE, mutexName.c_str()));
            if (!instanceMutex_) throw_last_error();
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                instanceMutex_.close();
                ExitCode = 2;
                Exit();
                return;
            }
            ownsMutex_ = true;
            window_ = make<MainWindow>(options);
            window_.SystemBackdrop(make<ThinAcrylicBackdrop>());
            window_.Closed([weak = get_weak()](auto const&, auto const&) {
                if (auto self = weak.get()) {
                    self->window_ = nullptr;
                    self->ReleaseMutex();
                }
            });
            window_.Activate();
        }
        catch (hresult_error const& error) {
            Log("Startup failure: " + to_string(error.message()));
            ReleaseMutex();
            ExitCode = 1;
            MessageBoxW(nullptr, error.message().c_str(), L"PresentMon Startup Error", MB_OK | MB_ICONERROR);
            Exit();
        }
        catch (std::exception const& error) {
            Log(std::string("Startup failure: ") + error.what());
            ReleaseMutex();
            ExitCode = 1;
            MessageBoxW(nullptr, to_hstring(error.what()).c_str(), L"PresentMon Startup Error", MB_OK | MB_ICONERROR);
            Exit();
        }
    }

    winrt::fire_and_forget App::RunSmokeAsync(std::string reportPath, std::string runId)
    {
        auto lifetime = get_strong();
        auto report = std::make_shared<pmon::ui::tests::BackdropSmokeReport>();
        Microsoft::UI::Xaml::Window host;
        host.Title(L"PresentMon test host");
        host.Content(Microsoft::UI::Xaml::Controls::Grid());
        host.Activate();
        host.AppWindow().Hide();
        report->RunId = std::move(runId);
        try { co_await pmon::ui::tests::RunBackdropTestsAsync(report); }
        catch (hresult_error const& error) { report->Failure = to_string(error.message()); }
        catch (std::exception const& error) { report->Failure = error.what(); }
        ExitCode = report->Failure.empty() ? 0 : 1;
        try {
            std::ofstream output(std::filesystem::u8path(reportPath), std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << nlohmann::json{{"runId", report->RunId}, {"complete", true},
                {"steps", report->Steps}, {"failure", report->Failure}}.dump(2);
            output.close();
        }
        catch (...) { ExitCode = 1; }
        host.Close();
        Exit();
    }

    void App::ReleaseMutex() noexcept
    {
        if (ownsMutex_) { ::ReleaseMutex(instanceMutex_.get()); ownsMutex_ = false; }
        instanceMutex_.close();
    }

    winrt::fire_and_forget App::RunShellSmokeAsync(pmon::ui::services::StartupOptions options,
        std::string reportPath, std::string runId)
    {
        auto lifetime = get_strong();
        auto const dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        Microsoft::UI::Xaml::Window host;
        host.Title(L"PresentMon test host");
        host.Content(Microsoft::UI::Xaml::Controls::Grid());
        host.Activate();
        host.AppWindow().Hide();
        std::string failure;
        std::vector<std::string> steps;
        winrt::com_ptr<MainWindow> shell;
        auto closed = std::make_shared<bool>(false);
        try {
            shell = make_self<MainWindow>(options);
            window_ = shell.as<Microsoft::UI::Xaml::Window>();
            window_.SystemBackdrop(make<ThinAcrylicBackdrop>());
            window_.Closed([closed](auto const&, auto const&) { *closed = true; });
            window_.Activate();
            co_await shell->RunShellSmokeAsync();
            steps.push_back("shell-navigation-edit-autosave");
        }
        catch (hresult_error const& error) { failure = to_string(error.message()); }
        catch (std::exception const& error) { failure = error.what(); }
        co_await pmon::ui::ResumeForeground{dispatcher};
        if (window_) {
            window_.Close();
            for (int attempt = 0; attempt < 100 && !*closed; ++attempt) {
                co_await winrt::resume_after(std::chrono::milliseconds(25));
                co_await pmon::ui::ResumeForeground{dispatcher};
            }
            if (!*closed && failure.empty()) failure = "Shell did not close cleanly.";
            if (*closed) steps.push_back("shell-close");
            window_ = nullptr;
        }
        ExitCode = failure.empty() ? 0 : 1;
        try {
            std::ofstream output(std::filesystem::u8path(reportPath), std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << nlohmann::json{{"runId", runId}, {"complete", true},
                {"steps", steps}, {"failure", failure}}.dump(2);
            output.close();
        }
        catch (...) { ExitCode = 1; }
        host.Close();
        Exit();
    }

    void App::Log(std::string const& message) noexcept
    {
        OutputDebugStringA((message + "\n").c_str());
        if (logDirectory_.empty()) return;
        try {
            auto directory = std::filesystem::u8path(logDirectory_);
            std::filesystem::create_directories(directory);
            std::ofstream(directory / "PresentMonUI.log", std::ios::app) << message << '\n';
        }
        catch (...) {}
    }
}
