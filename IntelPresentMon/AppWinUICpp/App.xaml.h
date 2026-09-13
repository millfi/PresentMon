#pragma once
#include "App.xaml.g.h"
#include "Core/StartupOptions.h"

namespace winrt::PresentMon::UI::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        static inline int ExitCode = 0;
    private:
        winrt::fire_and_forget RunSmokeAsync(std::string reportPath, std::string runId);
        winrt::fire_and_forget RunShellSmokeAsync(pmon::ui::services::StartupOptions options,
            std::string reportPath, std::string runId);
        void ReleaseMutex() noexcept;
        void Log(std::string const& message) noexcept;
        Microsoft::UI::Xaml::Window window_{nullptr};
        winrt::handle instanceMutex_;
        bool ownsMutex_ = false;
        std::string logDirectory_;
    };
}
