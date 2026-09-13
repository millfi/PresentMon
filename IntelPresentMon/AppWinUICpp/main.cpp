#include "pch.h"
#include "App.xaml.h"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::PresentMon::UI::implementation::App>();
    });
    return winrt::PresentMon::UI::implementation::App::ExitCode;
}
