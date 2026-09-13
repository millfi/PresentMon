// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "../Interop/CerealBinary.h"
#include "../Interop/KernelProtocol.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace pmon::ui::tests
{
    namespace
    {
        using namespace interop;

        class PipeClosed : public std::runtime_error
        {
        public:
            PipeClosed() : std::runtime_error("Mock kernel pipe closed.") {}
        };

        class MockKernel
        {
        public:
            MockKernel()
                : name_("presentmon-shell-smoke-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()))
            {
                requestPipe_ = CreateNamedPipeA(("\\\\.\\pipe\\" + name_ + "-in").c_str(), PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
                eventPipe_ = CreateNamedPipeA(("\\\\.\\pipe\\" + name_ + "-out").c_str(), PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
                if (requestPipe_ == INVALID_HANDLE_VALUE || eventPipe_ == INVALID_HANDLE_VALUE) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Unable to create mock kernel pipes");
                }
            }

            ~MockKernel()
            {
                Stop();
                if (worker_.joinable()) {
                    worker_.join();
                }
                if (requestPipe_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(requestPipe_);
                }
                if (eventPipe_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(eventPipe_);
                }
            }

            MockKernel(const MockKernel&) = delete;
            MockKernel& operator=(const MockKernel&) = delete;

            const std::string& Name() const noexcept
            {
                return name_;
            }

            void Start()
            {
                worker_ = std::thread([this] {
                    try {
                        Accept(requestPipe_);
                        Accept(eventPipe_);
                        Serve();
                    }
                    catch (const PipeClosed&) {
                    }
                    catch (...) {
                        if (!stopping_) {
                            error_ = std::current_exception();
                        }
                    }
                });
            }

            void Join()
            {
                Stop();
                if (worker_.joinable()) {
                    worker_.join();
                }
                if (error_) {
                    std::rethrow_exception(error_);
                }
            }

            void Stop() noexcept
            {
                stopping_ = true;
                if (requestPipe_ != INVALID_HANDLE_VALUE) {
                    CancelIoEx(requestPipe_, nullptr);
                }
                if (eventPipe_ != INVALID_HANDLE_VALUE) {
                    CancelIoEx(eventPipe_, nullptr);
                }
            }

        private:
            struct Request
            {
                KernelPacketHeader Header;
                CerealReader Payload;
            };

            static bool IsClosedError(DWORD error)
            {
                return error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA || error == ERROR_PIPE_NOT_CONNECTED;
            }

            static void ReadExactly(HANDLE pipe, std::span<uint8_t> bytes)
            {
                size_t offset = 0;
                while (offset < bytes.size()) {
                    DWORD count = 0;
                    if (!ReadFile(pipe, bytes.data() + offset, (DWORD)(bytes.size() - offset), &count, nullptr) || count == 0) {
                        if (IsClosedError(GetLastError())) {
                            throw PipeClosed{};
                        }
                        throw std::system_error((int)GetLastError(), std::system_category(), "Mock kernel read failed");
                    }
                    offset += count;
                }
            }

            static void WriteExactly(HANDLE pipe, std::span<const uint8_t> bytes)
            {
                size_t offset = 0;
                while (offset < bytes.size()) {
                    DWORD count = 0;
                    if (!WriteFile(pipe, bytes.data() + offset, (DWORD)(bytes.size() - offset), &count, nullptr) || count == 0) {
                        if (IsClosedError(GetLastError())) {
                            throw PipeClosed{};
                        }
                        throw std::system_error((int)GetLastError(), std::system_category(), "Mock kernel write failed");
                    }
                    offset += count;
                }
            }

            static void Accept(HANDLE pipe)
            {
                if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "Mock kernel accept failed");
                }
            }

            Request ReadRequest()
            {
                std::array<uint8_t, sizeof(uint32_t)> prefix{};
                ReadExactly(requestPipe_, prefix);
                const auto size = (uint32_t)prefix[0] | ((uint32_t)prefix[1] << 8) | ((uint32_t)prefix[2] << 16) | ((uint32_t)prefix[3] << 24);
                if (size < KernelProtocol::MinimumPacketBytes || size > KernelProtocol::MaximumPacketBytes) {
                    throw std::runtime_error("Mock kernel received an invalid packet length.");
                }
                auto body = std::vector<uint8_t>(size);
                ReadExactly(requestPipe_, body);
                bodies_.push_back(std::move(body));
                CerealReader reader(bodies_.back());
                return { KernelProtocol::Header(reader), std::move(reader) };
            }

            void Reply(KernelPacketHeader const& request, const std::function<void(CerealWriter&)>& payload = {})
            {
                CerealWriter writer;
                writer.String(request.Identifier);
                writer.UInt32(request.CommandToken);
                writer.Int32(0);
                writer.Int32(0);
                writer.Int32(0);
                writer.UInt16(1);
                writer.UInt16(1);
                if (payload) {
                    payload(writer);
                }
                WriteExactly(requestPipe_, writer.Packet());
            }

            static void WriteIntrospection(CerealWriter& writer)
            {
                writer.UInt64(1);
                writer.Int32(8);
                writer.String("Frame time");
                writer.String("Mock frame time metric for native shell smoke testing.");
                writer.Int32((int)core::MetricDeviceType::Independent);
                writer.Int32(0);
                writer.UInt64(1);
                writer.UInt32(0);
                writer.Int32(1);
                writer.Int32(0);
                writer.UInt64(1);
                writer.Int32(1);
                writer.Bool(true);

                writer.UInt64(1);
                writer.Int32(1);
                writer.String("Average");
                writer.String("Avg");
                writer.String("Average frame time.");

                writer.UInt64(0);

                writer.UInt64(1);
                writer.UInt32(1);
                writer.String("Intel");
                writer.String("Mock graphics adapter");
                writer.UInt32(65536);
                writer.UInt32(1);

                writer.UInt64(1);
                writer.Int32(0);
                writer.String("Available");
            }

            void Serve()
            {
                while (true) {
                    auto request = ReadRequest();
                    if (request.Header.Identifier == "OpenSession") {
                        request.Payload.UInt32();
                        request.Payload.RequireEnd();
                        Reply(request.Header, [](CerealWriter& writer) {
                            writer.UInt32(GetCurrentProcessId());
                            writer.String("native-shell-smoke");
                            writer.String("native-shell-smoke");
                            writer.String("native-shell-smoke");
                            writer.String("native-shell-smoke");
                        });
                    }
                    else if (request.Header.Identifier == "Introspect") {
                        request.Payload.RequireEnd();
                        Reply(request.Header, WriteIntrospection);
                    }
                    else if (request.Header.Identifier == "ProbeGpuBusy") {
                        Reply(request.Header, [](CerealWriter& writer) {
                            writer.UInt64(0);
                        });
                    }
                    else {
                        Reply(request.Header);
                    }
                }
            }

            std::string name_;
            HANDLE requestPipe_ = INVALID_HANDLE_VALUE;
            HANDLE eventPipe_ = INVALID_HANDLE_VALUE;
            std::thread worker_;
            std::exception_ptr error_;
            std::vector<std::vector<uint8_t>> bodies_;
            std::atomic_bool stopping_{ false };
        };

        std::wstring Quote(const std::wstring& value)
        {
            std::wstring result = L"\"";
            for (const auto character : value) {
                if (character == L'\"') {
                    result += L"\\\"";
                }
                else {
                    result += character;
                }
            }
            return result + L"\"";
        }

        void VerifyOutput(const std::filesystem::path& report, const std::filesystem::path& dataDirectory)
        {
            if (!std::filesystem::exists(report)) {
                throw std::runtime_error("Native shell smoke did not write its report.");
            }
            if (!std::filesystem::exists(dataDirectory / "preferences.json")) {
                throw std::runtime_error("Native shell smoke did not persist preferences.");
            }
            if (!std::filesystem::exists(dataDirectory / "Loadouts" / "custom-auto.json")) {
                throw std::runtime_error("Native shell smoke did not persist the custom loadout.");
            }
        }
    }

    int RunShellSmokeHost(const std::filesystem::path& uiExecutable, const std::filesystem::path& report,
        const std::filesystem::path& dataDirectory, const std::wstring& runId, DWORD timeoutMilliseconds)
    {
        if (!std::filesystem::is_regular_file(uiExecutable)) {
            throw std::runtime_error("Native UI executable is missing.");
        }
        std::filesystem::create_directories(dataDirectory);
        MockKernel kernel;
        const auto command = Quote(uiExecutable.wstring()) + L" --native-shell-smoke-test --smoke-report " + Quote(report.wstring())
            + L" --smoke-run-id " + Quote(runId) + L" --p2c-act-name " + Quote(std::wstring(kernel.Name().begin(), kernel.Name().end()))
            + L" --data-directory " + Quote(dataDirectory.wstring()) + L" --p2c-enable-ui-dev-options";
        std::vector<wchar_t> commandLine(command.begin(), command.end());
        commandLine.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                uiExecutable.parent_path().c_str(), &startup, &process)) {
            throw std::system_error((int)GetLastError(), std::system_category(), "Unable to start native shell smoke");
        }
        CloseHandle(process.hThread);
        kernel.Start();
        const auto waited = WaitForSingleObject(process.hProcess, timeoutMilliseconds);
        if (waited == WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            CloseHandle(process.hProcess);
            throw std::runtime_error("Native shell smoke timed out.");
        }
        if (waited != WAIT_OBJECT_0) {
            CloseHandle(process.hProcess);
            throw std::system_error((int)GetLastError(), std::system_category(), "Unable to wait for native shell smoke");
        }
        DWORD exitCode = 0;
        const auto exitCodeRead = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hProcess);
        if (!exitCodeRead) {
            throw std::system_error((int)GetLastError(), std::system_category(), "Unable to read native shell smoke exit code");
        }
        kernel.Join();
        if (exitCode != 0) {
            throw std::runtime_error("Native shell smoke reported failure.");
        }
        VerifyOutput(report, dataDirectory);
        return 0;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 6) {
        std::wcerr << L"Usage: PresentMonUI.ShellSmokeHost.exe <ui-exe> <report> <data-directory> <run-id> <timeout-seconds>\n";
        return 2;
    }
    try {
        const auto timeoutSeconds = wcstoul(argv[5], nullptr, 10);
        if (timeoutSeconds == 0 || timeoutSeconds > 300) {
            throw std::invalid_argument("Timeout must be between one and 300 seconds.");
        }
        return pmon::ui::tests::RunShellSmokeHost(argv[1], argv[2], argv[3], argv[4], (DWORD)(timeoutSeconds * 1000));
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
