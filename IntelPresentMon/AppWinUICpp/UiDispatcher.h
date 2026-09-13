#pragma once
#include <coroutine>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace pmon::ui
{
    struct ResumeForeground
    {
        winrt::Microsoft::UI::Dispatching::DispatcherQueue Dispatcher;
        bool await_ready() const { return Dispatcher.HasThreadAccess(); }
        void await_suspend(std::coroutine_handle<> continuation) const
        {
            if (!Dispatcher.TryEnqueue([continuation] { continuation.resume(); })) {
                throw winrt::hresult_canceled();
            }
        }
        void await_resume() const noexcept {}
    };
}
