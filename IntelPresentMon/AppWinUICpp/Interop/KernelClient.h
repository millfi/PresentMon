// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include "KernelProtocol.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

namespace pmon::ui::interop
{
    class KernelClient
    {
    public:
        static std::unique_ptr<KernelClient> Connect(const std::string& pipeBaseName, std::stop_token cancellation = {});
        ~KernelClient();
        KernelClient(const KernelClient&) = delete;
        KernelClient& operator=(const KernelClient&) = delete;

        bool IsConnected() const noexcept;
        const KernelSessionInfo& Session() const noexcept;

        KernelSessionInfo OpenSession(uint32_t processId, std::stop_token cancellation = {});
        IntrospectionData Introspect(std::stop_token cancellation = {});
        void BindHotkey(const core::HotkeyBinding& binding, std::stop_token cancellation = {});
        void ClearHotkey(int action, std::stop_token cancellation = {});
        void PushSpecification(const core::Specification& specification, std::stop_token cancellation = {});
        void SetCapture(bool active, std::stop_token cancellation = {});
        std::vector<int> ProbeGpuBusy(const std::vector<int>& pids, std::stop_token cancellation = {});
        void SetEtlLogging(bool active, std::stop_token cancellation = {});
        bool WaitEvent(KernelEvent& event, std::stop_token cancellation = {});
        void Close() noexcept;

        // Kept public for protocol framing verification and native test hosts.
        static std::vector<uint8_t> ReadPacket(HANDLE pipe, std::stop_token cancellation = {}, DWORD timeoutMs = INFINITE);

    private:
        KernelClient(HANDLE requestPipe, HANDLE eventPipe);
        static HANDLE ConnectPipe(const std::string& name, std::stop_token cancellation);
        static std::string NormalizePipeBaseName(const std::string& pipeBaseName);
        static void WritePacket(HANDLE pipe, std::span<const uint8_t> packet, std::stop_token cancellation, DWORD timeoutMs);
        static void ReadExactly(HANDLE pipe, std::span<uint8_t> bytes, std::stop_token cancellation, DWORD timeoutMs);
        static void WriteExactly(HANDLE pipe, std::span<const uint8_t> bytes, std::stop_token cancellation, DWORD timeoutMs);
        static void WaitForOverlapped(HANDLE pipe, OVERLAPPED& operation, std::stop_token cancellation, DWORD timeoutMs);
        void StartEventReader();
        void ReadEventsLoop(std::stop_token cancellation) noexcept;
        void FailConnection(std::exception_ptr error) noexcept;
        void ThrowIfClosed() const;
        void Request(const std::string& identifier, const std::function<void(CerealWriter&)>& write,
            const std::function<void(CerealReader&)>& read, std::stop_token cancellation);
        void EnqueueEvent(KernelEvent event);

        HANDLE requestPipe_ = INVALID_HANDLE_VALUE;
        HANDLE eventPipe_ = INVALID_HANDLE_VALUE;
        KernelSessionInfo session_;
        std::atomic<bool> closed_ = false;
        std::atomic<uint32_t> nextToken_ = 0;
        std::mutex requestMutex_;
        std::mutex eventMutex_;
        std::condition_variable_any eventReady_;
        std::deque<KernelEvent> events_;
        std::jthread eventReader_;
        std::exception_ptr terminalError_;
    };
}
