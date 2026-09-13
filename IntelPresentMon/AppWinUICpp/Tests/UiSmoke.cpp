// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "pch.h"
#include "UiSmoke.h"
#include "../UiDispatcher.h"

#include "../ThinAcrylicBackdrop.h"

#include <chrono>
#include <optional>
#include <sstream>
#include <utility>

namespace pmon::ui::tests
{
    using namespace winrt::Windows::Foundation;

    namespace
    {
        using namespace winrt;
        using namespace Microsoft::UI::Composition::SystemBackdrops;
        using namespace Microsoft::UI::Dispatching;
        using namespace Microsoft::UI::Windowing;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;

        std::string CreateRunId()
        {
            std::ostringstream stream;
            stream << "backdrop-" << std::hex
                << std::chrono::steady_clock::now().time_since_epoch().count();
            return stream.str();
        }

        ElementTheme ExpectedTheme(ElementTheme requested)
        {
            if (requested != ElementTheme::Default) {
                return requested;
            }
            return Application::Current().RequestedTheme() == ApplicationTheme::Light
                ? ElementTheme::Light : ElementTheme::Dark;
        }

        void ThrowIfUnexpectedTheme(Grid const& root, ElementTheme expected)
        {
            if (root.ActualTheme() != expected) {
                throw hresult_error(E_FAIL, L"Backdrop theme did not update.");
            }
        }

        std::string ThemeStep(ElementTheme theme)
        {
            switch (theme) {
            case ElementTheme::Light:
                return "theme-light";
            case ElementTheme::Dark:
                return "theme-dark";
            default:
                return "theme-default";
            }
        }

        IAsyncAction DelayOnUiThread(DispatcherQueue const& dispatcher)
        {
            co_await resume_after(std::chrono::milliseconds(100));
            co_await pmon::ui::ResumeForeground{dispatcher};
        }
    }

    IAsyncAction RunBackdropTestsAsync(std::shared_ptr<BackdropSmokeReport> report)
    {
        if (!report) {
            throw hresult_invalid_argument(L"Backdrop smoke report is required.");
        }
        auto const smokeReport = std::move(report);
        if (smokeReport->RunId.empty()) {
            smokeReport->RunId = CreateRunId();
        }

        if (!DesktopAcrylicController::IsSupported()) {
            smokeReport->Failure = "Desktop acrylic support is required for backdrop smoke tests.";
            co_return;
        }

        auto const dispatcher = DispatcherQueue::GetForCurrentThread();
        if (!dispatcher) {
            smokeReport->Failure = "Backdrop smoke tests require the XAML UI thread.";
            co_return;
        }

        std::optional<hresult> unhandledException;
        hstring unhandledMessage;
        auto const application = Application::Current();
        auto const unhandledRevoker = application.UnhandledException(auto_revoke,
            [&unhandledException, &unhandledMessage](IInspectable const&, UnhandledExceptionEventArgs const& args) {
                unhandledException = args.Exception();
                unhandledMessage = args.Message();
                args.Handled(true);
            });

        Window window{ nullptr };
        try {
            Grid root;
            root.RequestedTheme(ElementTheme::Dark);
            window = Window();
            window.Title(L"PresentMon backdrop regression tests");
            window.Content(root);
            auto backdrop = make<PresentMon::UI::implementation::ThinAcrylicBackdrop>();
            window.SystemBackdrop(backdrop);
            window.Activate();
            co_await DelayOnUiThread(dispatcher);
            smokeReport->Steps.push_back("activate");

            for (int pass = 0; pass < 3; ++pass) {
                for (auto theme : { ElementTheme::Light, ElementTheme::Dark, ElementTheme::Default,
                         ElementTheme::Light, ElementTheme::Dark }) {
                    root.RequestedTheme(theme);
                    co_await DelayOnUiThread(dispatcher);
                    if (unhandledException) {
                        throw hresult_error(*unhandledException, unhandledMessage);
                    }
                    ThrowIfUnexpectedTheme(root, ExpectedTheme(theme));
                    smokeReport->Steps.push_back(ThemeStep(theme));
                }

                auto const presenter = window.AppWindow().Presenter().as<OverlappedPresenter>();
                presenter.Minimize();
                co_await DelayOnUiThread(dispatcher);
                smokeReport->Steps.push_back("minimize");
                presenter.Restore();
                window.Activate();
                co_await DelayOnUiThread(dispatcher);
                if (unhandledException) {
                    throw hresult_error(*unhandledException, unhandledMessage);
                }
                smokeReport->Steps.push_back("restore");

                window.SystemBackdrop(nullptr);
                backdrop = nullptr;
                auto replacement = make<PresentMon::UI::implementation::ThinAcrylicBackdrop>();
                window.SystemBackdrop(replacement);
                co_await DelayOnUiThread(dispatcher);
                if (unhandledException) {
                    throw hresult_error(*unhandledException, unhandledMessage);
                }
                backdrop = replacement;
                smokeReport->Steps.push_back("reconnect");
            }

            window.Close();
            window = nullptr;
            backdrop = nullptr;
            co_await DelayOnUiThread(dispatcher);
            if (unhandledException) {
                throw hresult_error(*unhandledException, unhandledMessage);
            }
            smokeReport->Steps.push_back("close");
        }
        catch (hresult_error const& error) {
            smokeReport->Failure = to_string(error.message());
        }
        catch (std::exception const& error) {
            smokeReport->Failure = error.what();
        }
        co_return;
    }
}
