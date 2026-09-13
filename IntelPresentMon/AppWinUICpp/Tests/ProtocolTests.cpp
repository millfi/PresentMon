// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "TestSupport.h"

#include "../Core/ConfigModels.h"
#include "../Core/Specification.h"
#include "../Interop/CerealBinary.h"
#include "../Interop/KernelClient.h"
#include "../Interop/KernelProtocol.h"

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>

namespace pmon::ui::tests
{
    namespace
    {
        using namespace interop;

        std::vector<uint8_t> ReadFixture(const std::string& name)
        {
            std::array<wchar_t, 32768> executablePath{};
            const auto length = GetModuleFileNameW(nullptr, executablePath.data(), (DWORD)executablePath.size());
            Expect(length != 0 && length < executablePath.size(), "Unable to locate native test executable.");
            const auto path = std::filesystem::path(executablePath.data()).parent_path() / "NativeFixtures" / (name + ".bin");
            std::ifstream stream(path, std::ios::binary);
            Expect(stream.good(), "Missing native protocol fixture: " + path.string());
            return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        }

        std::string Hex(const std::vector<uint8_t>& bytes)
        {
            std::ostringstream stream;
            stream << std::uppercase << std::hex << std::setfill('0');
            for (const auto byte : bytes) {
                stream << std::setw(2) << (int)byte;
            }
            return stream.str();
        }

        std::vector<uint8_t> ReadFragmentedPacket(const std::vector<uint8_t>& packet)
        {
            HANDLE readPipe = INVALID_HANDLE_VALUE;
            HANDLE writePipe = INVALID_HANDLE_VALUE;
            if (!CreatePipe(&readPipe, &writePipe, nullptr, 0)) {
                throw std::runtime_error("Unable to create fragmented packet test pipe.");
            }
            std::thread writer([writePipe, &packet] {
                for (size_t offset = 0; offset < packet.size(); offset += 3) {
                    DWORD written = 0;
                    const auto count = (DWORD)std::min<size_t>(3, packet.size() - offset);
                    if (!WriteFile(writePipe, packet.data() + offset, count, &written, nullptr) || written != count) {
                        CloseHandle(writePipe);
                        return;
                    }
                }
                CloseHandle(writePipe);
            });
            try {
                auto body = KernelClient::ReadPacket(readPipe);
                CloseHandle(readPipe);
                writer.join();
                return body;
            }
            catch (...) {
                CloseHandle(readPipe);
                writer.join();
                throw;
            }
        }

        void ExpectInvalidPacket(const std::vector<uint8_t>& bytes)
        {
            HANDLE readPipe = INVALID_HANDLE_VALUE;
            HANDLE writePipe = INVALID_HANDLE_VALUE;
            if (!CreatePipe(&readPipe, &writePipe, nullptr, 0)) {
                throw std::runtime_error("Unable to create invalid packet test pipe.");
            }
            DWORD written = 0;
            if (!bytes.empty() && (!WriteFile(writePipe, bytes.data(), (DWORD)bytes.size(), &written, nullptr) || written != bytes.size())) {
                CloseHandle(readPipe);
                CloseHandle(writePipe);
                throw std::runtime_error("Unable to write invalid packet test data.");
            }
            CloseHandle(writePipe);
            bool rejected = false;
            try { (void)KernelClient::ReadPacket(readPipe); }
            catch (const std::exception&) { rejected = true; }
            CloseHandle(readPipe);
            Expect(rejected, "Invalid length-prefixed packet was accepted.");
        }

        core::WidgetMetric MakeWidgetMetric(int metricId, int arrayIndex, int deviceId, int statId, int desiredUnitId,
            core::RgbaColor lineColor, core::RgbaColor fillColor, core::AxisAffinity axisAffinity)
        {
            core::WidgetMetric result;
            result.Metric = { metricId, arrayIndex, deviceId, statId, desiredUnitId };
            result.LineColor = std::move(lineColor);
            result.FillColor = std::move(fillColor);
            result.AxisAffinity = axisAffinity;
            return result;
        }

        class TestServer
        {
        public:
            enum class Mode { Commands, InvalidReply, ExecutionFailure, StallCapture, ConcurrentCapture };

            explicit TestServer(Mode mode = Mode::Commands)
                : name_("pm-winui-cpp-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64())), mode_(mode)
            {
                requestPipe_ = CreateNamedPipeA(("\\\\.\\pipe\\" + name_ + "-in").c_str(), PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
                eventPipe_ = CreateNamedPipeA(("\\\\.\\pipe\\" + name_ + "-out").c_str(), PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
                if (requestPipe_ == INVALID_HANDLE_VALUE || eventPipe_ == INVALID_HANDLE_VALUE) {
                    throw std::runtime_error("Unable to create test named pipes.");
                }
            }

            ~TestServer()
            {
                if (worker_.joinable()) worker_.join();
                if (requestPipe_ != INVALID_HANDLE_VALUE) CloseHandle(requestPipe_);
                if (eventPipe_ != INVALID_HANDLE_VALUE) CloseHandle(eventPipe_);
            }

            const std::string& Name() const noexcept { return name_; }

            void Start()
            {
                worker_ = std::thread([this] {
                    try {
                        Accept(requestPipe_);
                        Accept(eventPipe_);
                        Handshake();
                        switch (mode_) {
                        case Mode::Commands: CommandsAndEvents(); break;
                        case Mode::InvalidReply: InvalidReply(); break;
                        case Mode::ExecutionFailure: ExecutionFailure(); break;
                        case Mode::StallCapture: StallCapture(); break;
                        case Mode::ConcurrentCapture: ConcurrentCapture(); break;
                        }
                    }
                    catch (...) { error_ = std::current_exception(); }
                });
            }

            void Join()
            {
                worker_.join();
                if (error_) std::rethrow_exception(error_);
            }

            void WaitForCapture()
            {
                std::unique_lock lock(captureMutex_);
                captureReady_.wait(lock, [this] { return captureReceived_; });
            }

        private:
            static void ReadExactly(HANDLE pipe, std::span<uint8_t> bytes)
            {
                size_t offset = 0;
                while (offset < bytes.size()) {
                    DWORD count = 0;
                    if (!ReadFile(pipe, bytes.data() + offset, (DWORD)(bytes.size() - offset), &count, nullptr) || count == 0) {
                        throw std::runtime_error("Test server read failed.");
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
                        throw std::runtime_error("Test server write failed.");
                    }
                    offset += count;
                }
            }

            static void Accept(HANDLE pipe)
            {
                if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
                    throw std::runtime_error("Test server accept failed.");
                }
            }

            struct ReceivedRequest
            {
                std::shared_ptr<std::vector<uint8_t>> Body;
                KernelPacketHeader Header;
                CerealReader Payload;
            };

            ReceivedRequest Request(const std::string& identifier)
            {
                std::array<uint8_t, sizeof(uint32_t)> prefix{};
                ReadExactly(requestPipe_, prefix);
                const auto size = (uint32_t)prefix[0] | ((uint32_t)prefix[1] << 8) | ((uint32_t)prefix[2] << 16) | ((uint32_t)prefix[3] << 24);
                auto body = std::make_shared<std::vector<uint8_t>>(size);
                ReadExactly(requestPipe_, *body);
                CerealReader reader(*body);
                const auto header = KernelProtocol::Header(reader);
                Expect(header.Identifier == identifier, "Unexpected kernel request identifier.");
                return { std::move(body), header, std::move(reader) };
            }

            void Reply(const KernelPacketHeader& request, const std::function<void(CerealWriter&)>& payload = {},
                int transportStatus = 0, int executionStatus = 0, uint32_t tokenOffset = 0)
            {
                CerealWriter writer;
                writer.String(request.Identifier);
                writer.UInt32(request.CommandToken + tokenOffset);
                writer.Int32(transportStatus);
                writer.Int32(executionStatus);
                writer.Int32(0);
                writer.UInt16(1);
                writer.UInt16(1);
                if (payload) payload(writer);
                WriteExactly(requestPipe_, writer.Packet());
            }

            void SendEvent(const std::string& identifier, const std::function<void(CerealWriter&)>& payload = {})
            {
                CerealWriter writer;
                writer.String(identifier);
                writer.UInt32(0);
                writer.Int32(0);
                writer.Int32(0);
                writer.Int32(2);
                writer.UInt16(1);
                writer.UInt16(1);
                if (payload) payload(writer);
                WriteExactly(eventPipe_, writer.Packet());
            }

            void Handshake()
            {
                auto request = Request("OpenSession");
                Expect(request.Payload.UInt32() == GetCurrentProcessId(), "Client OpenSession PID changed.");
                request.Payload.RequireEnd();
                Reply(request.Header, [](CerealWriter& writer) {
                    writer.UInt32(123);
                    writer.String("service-build");
                    writer.String("service-time");
                    writer.String("service-version");
                    writer.String("api-version");
                });
            }

            void CommandsAndEvents()
            {
                auto capture = Request("SetCapture");
                Expect(capture.Payload.Bool(), "SetCapture payload changed.");
                capture.Payload.RequireEnd();
                Reply(capture.Header);

                auto bind = Request("BindHotkey");
                Expect(bind.Payload.UInt32() == 42, "BindHotkey key changed.");
                const auto modifiers = bind.Payload.List<uint32_t>([&bind] { return bind.Payload.UInt32(); }, 4);
                Expect(modifiers == std::vector<uint32_t>{ 2, 4 } && bind.Payload.Int32() == 0, "BindHotkey payload changed.");
                bind.Payload.RequireEnd();
                Reply(bind.Header);

                auto clear = Request("ClearHotkey");
                Expect(clear.Payload.Int32() == 2, "ClearHotkey payload changed.");
                clear.Payload.RequireEnd();
                Reply(clear.Header);

                auto etl = Request("SetEtlLogging");
                Expect(!etl.Payload.Bool(), "SetEtlLogging payload changed.");
                etl.Payload.RequireEnd();
                Reply(etl.Header);

                auto probe = Request("ProbeGpuBusy");
                const auto pids = probe.Payload.List<uint32_t>([&probe] { return probe.Payload.UInt32(); }, 4);
                Expect(pids == std::vector<uint32_t>{ 123, 456 }, "ProbeGpuBusy payload changed.");
                probe.Payload.RequireEnd();
                Reply(probe.Header, [](CerealWriter& writer) { writer.List(std::vector<uint32_t>{ 456 }, [&writer](uint32_t pid) { writer.UInt32(pid); }); });

                auto releaseProbe = Request("ProbeGpuBusy");
                const auto released = releaseProbe.Payload.List<uint32_t>([&releaseProbe] { return releaseProbe.Payload.UInt32(); }, 4);
                Expect(released.empty(), "Empty ProbeGpuBusy did not release trackers.");
                releaseProbe.Payload.RequireEnd();
                Reply(releaseProbe.Header, [](CerealWriter& writer) { writer.UInt64(0); });

                SendEvent("HotkeyFiredAction", [](CerealWriter& writer) { writer.Int32(1); });
                SendEvent("TargetLostAction", [](CerealWriter& writer) { writer.UInt32(456); });
                SendEvent("PresentmonInitFailedAction");
                SendEvent("OverlayDiedAction");
                SendEvent("StalePidAction");
            }

            void InvalidReply()
            {
                auto capture = Request("SetCapture");
                Expect(capture.Payload.Bool(), "SetCapture payload changed.");
                capture.Payload.RequireEnd();
                Reply(capture.Header, {}, 0, 0, 1);
            }

            void ExecutionFailure()
            {
                auto failed = Request("SetCapture");
                failed.Payload.Bool();
                failed.Payload.RequireEnd();
                Reply(failed.Header, {}, 1, 7);
                auto succeeded = Request("SetCapture");
                Expect(!succeeded.Payload.Bool(), "Second SetCapture payload changed.");
                succeeded.Payload.RequireEnd();
                Reply(succeeded.Header);
            }

            void StallCapture()
            {
                auto capture = Request("SetCapture");
                capture.Payload.Bool();
                capture.Payload.RequireEnd();
                {
                    std::scoped_lock lock(captureMutex_);
                    captureReceived_ = true;
                }
                captureReady_.notify_all();
            }

            void ConcurrentCapture()
            {
                auto first = Request("SetCapture");
                const auto firstToken = first.Header.CommandToken;
                first.Payload.Bool();
                first.Payload.RequireEnd();
                Reply(first.Header);
                auto second = Request("SetCapture");
                Expect(second.Header.CommandToken == firstToken + 1, "Concurrent requests were not serialized.");
                second.Payload.Bool();
                second.Payload.RequireEnd();
                Reply(second.Header);
            }

            std::string name_;
            HANDLE requestPipe_ = INVALID_HANDLE_VALUE;
            HANDLE eventPipe_ = INVALID_HANDLE_VALUE;
            std::thread worker_;
            std::exception_ptr error_;
            Mode mode_;
            std::mutex captureMutex_;
            std::condition_variable captureReady_;
            bool captureReceived_ = false;
        };

        core::Specification FullSpecification()
        {
            core::Specification specification;
            specification.Pid = 4242;
            auto& preferences = specification.Preferences;
            preferences.CapturePath = "C:\\Captures\\native-fixture";
            preferences.CaptureDelay = 3;
            preferences.EnableCaptureDelay = true;
            preferences.CaptureDuration = 61;
            preferences.EnableCaptureDuration = false;
            preferences.HideDuringCapture = true;
            preferences.HideAlways = false;
            preferences.EnablePerMetricDeviceSelection = true;
            preferences.IndependentWindow = true;
            preferences.MetricPollRate = 47;
            preferences.OverlayDrawRate = 29;
            preferences.TelemetrySamplingPeriodMs = 113;
            preferences.EtwFlushPeriod = 17;
            preferences.ManualEtwFlush = false;
            preferences.MetricsOffset = 157;
            preferences.MetricsWindow = 1021;
            preferences.OverlayPosition = core::OverlayPosition::BottomRight;
            preferences.TimeRange = 13.5;
            preferences.OverlayMargin = 1.25;
            preferences.OverlayBorder = 2.5;
            preferences.OverlayPadding = 3.75;
            preferences.GraphMargin = 4.25;
            preferences.GraphBorder = 5.5;
            preferences.GraphPadding = 6.75;
            preferences.OverlayBorderColor = { 11, 23, 37, .25 };
            preferences.OverlayBackgroundColor = { 41, 53, 67, .5 };
            preferences.GraphFont = { "Segoe UI", 12.5 };
            preferences.OverlayWidth = 613;
            preferences.Upscale = true;
            preferences.GenerateStats = false;
            preferences.EnableTargetBlocklist = true;
            preferences.EnableAutotargetting = false;
            preferences.UpscaleFactor = 1.75;
            preferences.AdapterId = 7;

            auto graph = std::make_shared<core::Graph>();
            graph->Metrics = {
                MakeWidgetMetric(11, 2, 7, 2, 4, { 71, 83, 97, .75 }, { 101, 113, 127, .125 }, core::AxisAffinity::Left),
                MakeWidgetMetric(14, 3, 9, 8, 6, { 131, 149, 157, .625 }, { 163, 179, 191, .375 }, core::AxisAffinity::Right),
            };
            graph->Height = 227;
            graph->VDivs = 7;
            graph->HDivs = 11;
            graph->ShowBottomAxis = false;
            graph->GraphType = { "Histogram", { -17, 239 }, { -31, 401 }, 37, { 5, 97 }, true, false, true };
            graph->GridColor = { 13, 29, 43, .125 };
            graph->DividerColor = { 59, 73, 89, .25 };
            graph->BackgroundColor = { 103, 109, 137, .375 };
            graph->BorderColor = { 151, 167, 181, .5 };
            graph->TextColor = { 193, 211, 227, .875 };
            graph->TextSize = 14.25;
            graph->LabelIncludeDeviceId = true;
            graph->LabelIncludeDeviceName = false;

            auto readout = std::make_shared<core::Readout>();
            readout->Metrics = {
                MakeWidgetMetric(9, 4, 12, 1, 7, { 17, 31, 47, .5 }, { 61, 79, 101, .75 }, core::AxisAffinity::Right),
            };
            readout->ShowLabel = true;
            readout->FontSize = 19.5;
            readout->FontColor = { 107, 139, 173, .625 };
            readout->BackgroundColor = { 199, 223, 251, .125 };
            readout->LabelIncludeDeviceId = false;
            readout->LabelIncludeDeviceName = true;
            specification.Widgets = { graph, readout };
            return specification;
        }

        void PrimitiveContracts()
        {
            CerealWriter writer;
            writer.OptionalUInt32(std::nullopt);
            writer.OptionalUInt32(0x01020304);
            writer.OptionalInt32(-2);
            Expect(Hex(writer.Data()) == "01000403020100FEFFFFFF", "Cereal optional nullopt marker changed.");
            ExpectThrows<ProtocolError>([] { CerealReader(std::vector<uint8_t>{ 2 }).Bool(); }, "Invalid cereal bool was accepted.");
            ExpectThrows<ProtocolError>([] { CerealReader(std::vector<uint8_t>{ 0, 0, 0 }).UInt32(); }, "Truncated uint32 was accepted.");
            ExpectThrows<ProtocolError>([] {
                CerealReader reader(std::vector<uint8_t>{ 255, 255, 255, 255, 255, 255, 255, 255 });
                reader.String();
            }, "Invalid string length was accepted.");
            ExpectThrows<ProtocolError>([] {
                CerealReader reader(std::vector<uint8_t>{ 0 });
                reader.RequireEnd();
            }, "Trailing packet data was accepted.");
            ExpectThrows<ProtocolError>([] {
                CerealReader reader(std::vector<uint8_t>{ 255, 255, 255, 255, 255, 255, 255, 255 });
                (void)reader.List<int>([] { return 1; });
            }, "Oversized cereal list length was accepted.");
            ExpectThrows<ProtocolError>([] {
                KernelProtocol::ValidateResponse({ "SetCapture", 3, 0, 0, 1, 1, 1 }, "SetCapture", 3);
            }, "Response packet type was not checked.");

            core::Specification invalidSpecification;
            invalidSpecification.Preferences.CaptureDelay = std::numeric_limits<double>::quiet_NaN();
            ExpectThrows<std::out_of_range>([&invalidSpecification] {
                CerealWriter invalidWriter;
                KernelProtocol::Specification(invalidWriter, invalidSpecification);
            }, "Non-finite specification setting was accepted.");

            core::Specification fractionalSpecification;
            fractionalSpecification.Preferences.CaptureDelay = 12.5;
            CerealWriter fractionalWriter;
            KernelProtocol::Specification(fractionalWriter, fractionalSpecification);
            const auto fractionalBytes = fractionalWriter.Data();
            CerealReader fractionalReader(fractionalBytes);
            Expect(fractionalReader.Bool(), "Fractional specification target marker changed.");
            Expect(fractionalReader.String().empty(), "Fractional specification capture path changed.");
            Expect(fractionalReader.UInt32() == 12, "Fractional CaptureDelay did not truncate at the native boundary.");
        }

        void PacketAndEventContracts()
        {
            const auto packet = KernelProtocol::Request("SetCapture", 17, [](CerealWriter& writer) { writer.Bool(true); });
            const auto fragmentedBody = ReadFragmentedPacket(packet);
            CerealReader reader(fragmentedBody);
            const auto header = KernelProtocol::Header(reader);
            Expect(header.Identifier == "SetCapture" && header.CommandToken == 17 && reader.Bool(), "Request framing changed.");
            reader.RequireEnd();

            ExpectInvalidPacket({ 255, 255, 255, 127 });
            ExpectInvalidPacket({ 0, 0, 0, 0 });
            ExpectInvalidPacket(std::vector<uint8_t>(packet.begin(), packet.end() - 1));

            CerealWriter eventWriter;
            eventWriter.String("TargetLostAction");
            eventWriter.UInt32(0);
            eventWriter.Int32(0);
            eventWriter.Int32(0);
            eventWriter.Int32(2);
            eventWriter.UInt16(1);
            eventWriter.UInt16(1);
            eventWriter.UInt32(456);
            const auto eventBytes = eventWriter.Data();
            CerealReader eventReader(eventBytes);
            const auto event = KernelProtocol::Event(KernelProtocol::Header(eventReader), eventReader);
            Expect(event.Kind == KernelEventKind::TargetLost && event.ProcessId == 456u, "Kernel event was decoded incorrectly.");
        }

        void ClientCommandsAndEvents()
        {
            TestServer server;
            server.Start();
            auto client = KernelClient::Connect(server.Name());
            Expect(client->Session().KernelPid == 123 && client->Session().ServiceBuildId == "service-build",
                "Kernel OpenSession response was decoded incorrectly.");
            client->SetCapture(true);
            core::HotkeyBinding binding;
            binding.Action = core::HotkeyAction::ToggleCapture;
            binding.Combination = core::HotkeyCombination{ 42, { 2, 4 } };
            client->BindHotkey(binding);
            client->ClearHotkey(2);
            client->SetEtlLogging(false);
            Expect(client->ProbeGpuBusy({ 123, 456 }) == std::vector<int>{ 456 }, "ProbeGpuBusy response changed.");
            Expect(client->ProbeGpuBusy({}).empty(), "Empty ProbeGpuBusy response changed.");
            server.Join();
            for (const auto kind : { KernelEventKind::HotkeyFired, KernelEventKind::TargetLost,
                KernelEventKind::PresentmonInitFailed, KernelEventKind::OverlayDied, KernelEventKind::StalePid }) {
                KernelEvent event{};
                Expect(client->WaitEvent(event) && event.Kind == kind, "Kernel event stream changed.");
            }
            client->Close();
        }

        void InvalidReplyClosesClient()
        {
            TestServer server(TestServer::Mode::InvalidReply);
            server.Start();
            auto client = KernelClient::Connect(server.Name());
            ExpectThrows<ProtocolError>([&client] { client->SetCapture(true); }, "Invalid reply was accepted.");
            Expect(!client->IsConnected(), "Invalid reply left client connected.");
            server.Join();
        }

        void ExecutionFailurePreservesClient()
        {
            TestServer server(TestServer::Mode::ExecutionFailure);
            server.Start();
            auto client = KernelClient::Connect(server.Name());
            ExpectThrows<KernelRequestException>([&client] { client->SetCapture(true); }, "Execution failure was accepted.");
            client->SetCapture(false);
            Expect(client->IsConnected(), "Execution failure closed a valid client.");
            server.Join();
            client->Close();
        }

        void CancellationAndDisposeCloseClient()
        {
            {
                TestServer server(TestServer::Mode::StallCapture);
                server.Start();
                auto client = KernelClient::Connect(server.Name());
                std::stop_source cancellation;
                std::exception_ptr error;
                std::thread request([&] {
                    try { client->SetCapture(true, cancellation.get_token()); }
                    catch (...) { error = std::current_exception(); }
                });
                server.WaitForCapture();
                cancellation.request_stop();
                request.join();
                Expect(error != nullptr && !client->IsConnected(), "Canceled request left client reusable.");
                server.Join();
            }
            {
                TestServer server(TestServer::Mode::StallCapture);
                server.Start();
                auto client = KernelClient::Connect(server.Name());
                std::exception_ptr error;
                std::thread request([&] {
                    try { client->SetCapture(true); }
                    catch (...) { error = std::current_exception(); }
                });
                server.WaitForCapture();
                client->Close();
                request.join();
                Expect(error != nullptr && !client->IsConnected(), "Close did not interrupt the active request.");
                server.Join();
            }
        }

        void ConcurrentRequestsAreSerialized()
        {
            TestServer server(TestServer::Mode::ConcurrentCapture);
            server.Start();
            auto client = KernelClient::Connect(server.Name());
            std::exception_ptr firstError;
            std::exception_ptr secondError;
            std::thread first([&] { try { client->SetCapture(true); } catch (...) { firstError = std::current_exception(); } });
            std::thread second([&] { try { client->SetCapture(false); } catch (...) { secondError = std::current_exception(); } });
            first.join();
            second.join();
            Expect(firstError == nullptr && secondError == nullptr, "Concurrent requests failed.");
            server.Join();
            client->Close();
        }

        void NativeFixturesMatchProtocol()
        {
            CerealWriter specificationWriter;
            KernelProtocol::Specification(specificationWriter, FullSpecification());
            const auto fullSpec = KernelProtocol::Request("PushSpecification", 17, [&specificationWriter](CerealWriter& writer) {
                writer.Bytes(specificationWriter.Data());
            });
            Expect(fullSpec == ReadFixture("full-spec"), "Native full specification fixture differs.");
            const auto openSession = KernelProtocol::Request("OpenSession", 29, [](CerealWriter& writer) { writer.UInt32(54321); });
            Expect(openSession == ReadFixture("open-session"), "Native OpenSession fixture differs.");
            const auto bindHotkey = KernelProtocol::Request("BindHotkey", 31, [](CerealWriter& writer) {
                writer.UInt32(42);
                writer.List(std::vector<uint32_t>{ 2, 4 }, [&writer](uint32_t value) { writer.UInt32(value); });
                writer.Int32(3);
            });
            Expect(bindHotkey == ReadFixture("bind-hotkey"), "Native BindHotkey fixture differs.");

            auto response = ReadFixture("open-session-response");
            CerealReader sessionReader(std::span<const uint8_t>(response).subspan(sizeof(uint32_t)));
            KernelProtocol::ValidateResponse(KernelProtocol::Header(sessionReader), "OpenSession", 29);
            const auto session = KernelProtocol::Session(sessionReader);
            sessionReader.RequireEnd();
            Expect(session.KernelPid == 65432 && session.ServiceBuildId == "0123456789abcdef" &&
                session.ServiceBuildTime == "2026-09-12T09:10:11Z" && session.ServiceVersion == "2.4.0" &&
                session.MiddlewareApiVersion == "3.4.0 fixture Release", "Native session response differs.");

            response = ReadFixture("introspect");
            CerealReader introReader(std::span<const uint8_t>(response).subspan(sizeof(uint32_t)));
            KernelProtocol::ValidateResponse(KernelProtocol::Header(introReader), "Introspect", 23);
            const auto introspection = KernelProtocol::Introspection(introReader);
            introReader.RequireEnd();
            Expect(introspection.Metrics.size() == 3 && introspection.Stats.size() == 4 && introspection.Units.empty() &&
                introspection.Adapters.size() == 2 && introspection.SystemDeviceId == 12 && introspection.DefaultAdapterId == 9 &&
                introspection.MetricAvailabilityReasons.size() == 3, "Native introspection collection layout differs.");
            const auto& fps = introspection.Metrics[0];
            const auto& gpu = introspection.Metrics[1];
            const auto& cpu = introspection.Metrics[2];
            Expect(fps.Id == 11 && fps.Name == "Displayed FPS" && fps.Description == "Displayed frames per second" &&
                fps.DeviceType == core::MetricDeviceType::Independent && fps.PreferredUnitId == 4 && fps.Numeric &&
                fps.AvailableStatIds == std::vector<int>{ 1, 2 } && fps.DeviceAvailability.size() == 2 &&
                fps.DeviceAvailability[0].DeviceId == 0 && fps.DeviceAvailability[0].ArraySize == 1 && fps.DeviceAvailability[0].AvailabilityId == 0 &&
                fps.DeviceAvailability[1].DeviceId == 7 && fps.DeviceAvailability[1].ArraySize == 2 && fps.DeviceAvailability[1].AvailabilityId == 3,
                "Native independent metric differs.");
            Expect(gpu.Id == 14 && gpu.Name == "GPU Busy" && gpu.Description == "GPU busy time" &&
                gpu.DeviceType == core::MetricDeviceType::GraphicsAdapter && gpu.PreferredUnitId == 6 && gpu.Numeric &&
                gpu.AvailableStatIds == std::vector<int>{ 8 } && gpu.DeviceAvailability.size() == 2 &&
                gpu.DeviceAvailability[0].DeviceId == 7 && gpu.DeviceAvailability[0].ArraySize == 3 && gpu.DeviceAvailability[0].AvailabilityId == 0 &&
                gpu.DeviceAvailability[1].DeviceId == 9 && gpu.DeviceAvailability[1].ArraySize == 4 && gpu.DeviceAvailability[1].AvailabilityId == 2,
                "Native graphics metric differs.");
            Expect(cpu.Id == 5 && cpu.Name == "CPU Name" && cpu.Description == "Processor model" &&
                cpu.DeviceType == core::MetricDeviceType::System && cpu.PreferredUnitId == 0 && !cpu.Numeric &&
                cpu.AvailableStatIds == std::vector<int>{ 0 } && cpu.DeviceAvailability.size() == 1 &&
                cpu.DeviceAvailability[0].DeviceId == 12 && cpu.DeviceAvailability[0].ArraySize == 1 && cpu.DeviceAvailability[0].AvailabilityId == 0,
                "Native system metric differs.");
            Expect(introspection.Stats[0].Id == 0 && introspection.Stats[0].Name == "None" && introspection.Stats[0].ShortName.empty() && introspection.Stats[0].Description == "Unaggregated value" &&
                introspection.Stats[1].Id == 1 && introspection.Stats[1].Name == "Average" && introspection.Stats[1].ShortName == "Avg" && introspection.Stats[1].Description == "Arithmetic mean" &&
                introspection.Stats[2].Id == 2 && introspection.Stats[2].Name == "99th percentile" && introspection.Stats[2].ShortName == "P99" && introspection.Stats[2].Description == "Upper percentile" &&
                introspection.Stats[3].Id == 8 && introspection.Stats[3].Name == "Maximum" && introspection.Stats[3].ShortName == "Max" && introspection.Stats[3].Description == "Largest sample",
                "Native stats differ.");
            Expect(introspection.Adapters[0].Id == 7 && introspection.Adapters[0].Vendor == "Intel" && introspection.Adapters[0].Name == "Fixture Arc" &&
                introspection.Adapters[1].Id == 9 && introspection.Adapters[1].Vendor == "Other" && introspection.Adapters[1].Name == "Fixture GPU" &&
                introspection.MetricAvailabilityReasons[0].Id == 0 && introspection.MetricAvailabilityReasons[0].Description == "Available" &&
                introspection.MetricAvailabilityReasons[1].Id == 2 && introspection.MetricAvailabilityReasons[1].Description == "Not exported by source" &&
                introspection.MetricAvailabilityReasons[2].Id == 3 && introspection.MetricAvailabilityReasons[2].Description == "Not supported by device",
                "Native adapters or availability reasons differ.");
        }

        void IncompatibleIntrospectionIsRejected()
        {
            CerealWriter writer;
            writer.UInt64(0);
            writer.UInt64(0);
            writer.UInt64(0);
            writer.UInt64(0);
            writer.UInt32(65536);
            writer.UInt64(0);
            const auto bytes = writer.Data();
            CerealReader reader(bytes);
            ExpectThrows<ProtocolError>([&reader] { KernelProtocol::Introspection(reader); },
                "Missing default adapter ID was accepted.");
        }
    }
}

int RunProtocolTests()
{
    using namespace pmon::ui::tests;
    return RunTest(PrimitiveContracts)
        + RunTest(PacketAndEventContracts)
        + RunTest(ClientCommandsAndEvents)
        + RunTest(InvalidReplyClosesClient)
        + RunTest(ExecutionFailurePreservesClient)
        + RunTest(CancellationAndDisposeCloseClient)
        + RunTest(ConcurrentRequestsAreSerialized)
        + RunTest(NativeFixturesMatchProtocol)
        + RunTest(IncompatibleIntrospectionIsRejected);
}

int RunCoreTests();

int main()
{
    try {
        const auto passed = RunProtocolTests() + RunCoreTests();
        std::cout << "All " << passed << " native UI core regression checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}
