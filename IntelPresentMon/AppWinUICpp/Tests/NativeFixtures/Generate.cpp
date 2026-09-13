// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "NativeTypes.generated.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/version.hpp>

using pmon::ipc::act::PacketHeader;
using pmon::ipc::act::PacketType;
using pmon::ipc::act::TransportStatus;
namespace spec = kproc::kact::push_spec_impl;
namespace fs = std::filesystem;

static_assert(std::endian::native == std::endian::little);
static_assert(CEREAL_VERSION == 10302);
static_assert(sizeof(bool) == 1 && sizeof(int) == 4 && sizeof(float) == 4);
static_assert(sizeof(cereal::size_type) == 8);
static_assert(std::is_same_v<std::underlying_type_t<PM_METRIC>, int>);
static_assert(std::is_same_v<std::underlying_type_t<PM_STAT>, int>);
static_assert(std::is_same_v<std::underlying_type_t<PM_UNIT>, int>);
static_assert(std::is_same_v<std::underlying_type_t<PM_DEVICE_TYPE>, int>);
static_assert(std::is_same_v<std::underlying_type_t<PacketType>, int>);
static_assert(std::is_same_v<std::underlying_type_t<TransportStatus>, int>);
static_assert(std::is_same_v<std::underlying_type_t<p2c::gfx::lay::AxisAffinity>, int>);
static_assert(std::is_same_v<std::underlying_type_t<p2c::kern::OverlaySpec::OverlayPosition>, int>);

p2c::gfx::Color Color(int r, int g, int b, float alpha)
{
    return { (float)r / 255.f, (float)g / 255.f, (float)b / 255.f, alpha };
}

PacketHeader Header(const std::string& action, uint32_t token)
{
    return { action, token, TransportStatus::Success, 0, PacketType::ActionRequest, 1, 1 };
}

spec::Params FullSpecification()
{
    spec::Params result{};
    result.pid = 4242;
    auto& p = result.preferences;
    p.capturePath = "C:\\Captures\\native-fixture";
    p.captureDelay = 3;
    p.enableCaptureDelay = true;
    p.captureDuration = 61;
    p.enableCaptureDuration = false;
    p.hideDuringCapture = true;
    p.hideAlways = false;
    p.enablePerMetricDeviceSelection = true;
    p.independentWindow = true;
    p.metricPollRate = 47;
    p.overlayDrawRate = 29;
    p.telemetrySamplingPeriodMs = 113;
    p.etwFlushPeriod = 17;
    p.manualEtwFlush = false;
    p.metricsOffset = 157;
    p.metricsWindow = 1021;
    p.overlayPosition = p2c::kern::OverlaySpec::OverlayPosition::BottomRight;
    p.timeRange = 13.5f;
    p.overlayMargin = 1.25f;
    p.overlayBorder = 2.5f;
    p.overlayPadding = 3.75f;
    p.graphMargin = 4.25f;
    p.graphBorder = 5.5f;
    p.graphPadding = 6.75f;
    p.overlayBorderColor = Color(11, 23, 37, .25f);
    p.overlayBackgroundColor = Color(41, 53, 67, .5f);
    p.graphFont = { "Segoe UI", 12.5f };
    p.overlayWidth = 613;
    p.upscale = true;
    p.generateStats = false;
    p.enableTargetBlocklist = true;
    p.enableAutotargetting = false;
    p.upscaleFactor = 1.75f;
    p.adapterId = 7;

    spec::Graph graph{};
    graph.metrics = {
        { { PM_METRIC_DISPLAYED_FPS, 2, 7, PM_STAT_PERCENTILE_99, PM_UNIT_FPS },
            Color(71, 83, 97, .75f), Color(101, 113, 127, .125f), p2c::gfx::lay::AxisAffinity::Left },
        { { PM_METRIC_GPU_BUSY, 3, 9, PM_STAT_MAX, PM_UNIT_MILLISECONDS },
            Color(131, 149, 157, .625f), Color(163, 179, 191, .375f), p2c::gfx::lay::AxisAffinity::Right },
    };
    graph.height = 227;
    graph.vDivs = 7;
    graph.hDivs = 11;
    graph.showBottomAxis = false;
    graph.graphType = { "Histogram", { -17, 239 }, { -31, 401 }, 37, { 5, 97 }, true, false, true };
    graph.gridColor = Color(13, 29, 43, .125f);
    graph.dividerColor = Color(59, 73, 89, .25f);
    graph.backgroundColor = Color(103, 109, 137, .375f);
    graph.borderColor = Color(151, 167, 181, .5f);
    graph.textColor = Color(193, 211, 227, .875f);
    graph.textSize = 14.25f;
    graph.labelIncludeDeviceId = true;
    graph.labelIncludeDeviceName = false;

    spec::Readout readout{};
    readout.metrics = {
        { { PM_METRIC_CPU_BUSY, 4, 12, PM_STAT_AVG, PM_UNIT_SECONDS },
            Color(17, 31, 47, .5f), Color(61, 79, 101, .75f), p2c::gfx::lay::AxisAffinity::Right },
    };
    readout.showLabel = true;
    readout.fontSize = 19.5f;
    readout.fontColor = Color(107, 139, 173, .625f);
    readout.backgroundColor = Color(199, 223, 251, .125f);
    readout.labelIncludeDeviceId = false;
    readout.labelIncludeDeviceName = true;
    result.widgets = { graph, readout };
    return result;
}

native_fixture::Introspect::Response Introspection()
{
    native_fixture::Introspect::Response result{};
    result.metrics = {
        { PM_METRIC_DISPLAYED_FPS, "Displayed FPS", "Displayed frames per second", PM_DEVICE_TYPE_INDEPENDENT,
            PM_UNIT_FPS, { { 0, 1, 0 }, { 7, 2, 3 } }, { PM_STAT_AVG, PM_STAT_PERCENTILE_99 }, true },
        { PM_METRIC_GPU_BUSY, "GPU Busy", "GPU busy time", PM_DEVICE_TYPE_GRAPHICS_ADAPTER,
            PM_UNIT_MILLISECONDS, { { 7, 3, 0 }, { 9, 4, 2 } }, { PM_STAT_MAX }, true },
        { PM_METRIC_CPU_NAME, "CPU Name", "Processor model", PM_DEVICE_TYPE_SYSTEM,
            PM_UNIT_DIMENSIONLESS, { { 12, 1, 0 } }, { PM_STAT_NONE }, false },
    };
    result.stats = {
        { PM_STAT_NONE, "None", "", "Unaggregated value" },
        { PM_STAT_AVG, "Average", "Avg", "Arithmetic mean" },
        { PM_STAT_PERCENTILE_99, "99th percentile", "P99", "Upper percentile" },
        { PM_STAT_MAX, "Maximum", "Max", "Largest sample" },
    };
    result.adapters = { { 7, "Intel", "Fixture Arc" }, { 9, "Other", "Fixture GPU" } };
    result.systemDeviceId = 12;
    result.defaultAdapterId = 9;
    result.metricAvailabilityReasons = {
        { 0, "Available" }, { 2, "Not exported by source" }, { 3, "Not supported by device" },
    };
    return result;
}

template<class Payload>
std::string Binary(const PacketHeader& header, const Payload& payload)
{
    std::ostringstream stream{ std::ios::out | std::ios::binary };
    cereal::BinaryOutputArchive archive{ stream };
    archive(header, payload);
    return stream.str();
}

template<class Payload>
void WriteFixture(const fs::path& folder, const std::string& name, const PacketHeader& header, const Payload& payload)
{
    const auto body = Binary(header, payload);
    // This is the uint32 body-length framing in CommonUtilities/pipe/Pipe.h.
    const auto size = (uint32_t)body.size();
    std::ofstream stream{ folder / (name + ".bin"), std::ios::binary };
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
    stream.write(body.data(), (std::streamsize)body.size());
    stream.close();

    std::ofstream manifest{ folder / (name + ".json") };
    manifest.exceptions(std::ios::failbit | std::ios::badbit);
    {
        cereal::JSONOutputArchive archive{ manifest };
        archive(cereal::make_nvp("header", header), cereal::make_nvp("payload", payload));
    }
    std::cout << name << ".bin: " << size + sizeof(size) << " bytes\n";
}

template<class Payload>
void VerifyFixture(const fs::path& folder, const std::string& name)
{
    std::ifstream stream{ folder / (name + ".bin"), std::ios::binary };
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    uint32_t size = 0;
    stream.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (fs::file_size(folder / (name + ".bin")) != sizeof(size) + size) {
        throw std::runtime_error{ name + ": invalid packet framing" };
    }
    std::string body(size, '\0');
    stream.read(body.data(), (std::streamsize)size);
    std::istringstream input{ body, std::ios::in | std::ios::binary };
    PacketHeader header{};
    Payload payload{};
    cereal::BinaryInputArchive archive{ input };
    archive(header, payload);
    if (input.peek() != std::char_traits<char>::eof() || Binary(header, payload) != body) {
        throw std::runtime_error{ name + ": native roundtrip failed" };
    }
    std::cout << "PASS " << name << " native framing/roundtrip\n";
}

int main(int argc, char** argv)
{
    try {
        if (argc != 3) throw std::runtime_error{ "Usage: native-fixtures.exe OUTPUT_DIRECTORY generate|verify" };
        const fs::path folder{ argv[1] };
        const std::string mode{ argv[2] };
        if (mode == "generate") {
            fs::create_directories(folder);
            const auto full = FullSpecification();
            WriteFixture(folder, "full-spec", Header("PushSpecification", 17), full);
            auto empty = full;
            empty.pid.reset();
            empty.preferences.adapterId.reset();
            empty.widgets.clear();
            WriteFixture(folder, "empty-spec", Header("PushSpecification", 19), empty);
            WriteFixture(folder, "introspect", MakeResponseHeader(Header("Introspect", 23), TransportStatus::Success, 0), Introspection());
            WriteFixture(folder, "open-session", Header("OpenSession", 29), native_fixture::OpenSession::Params{ 54321 });
            WriteFixture(folder, "open-session-response", MakeResponseHeader(Header("OpenSession", 29), TransportStatus::Success, 0), native_fixture::OpenSession::Response{
                65432, "0123456789abcdef", "2026-09-12T09:10:11Z", "2.4.0", "3.4.0 fixture Release" });
            WriteFixture(folder, "bind-hotkey", Header("BindHotkey", 31), native_fixture::BindHotkey::Params{ { 42, { 2, 4 } }, 3 });
        }
        else if (mode == "verify") {
            VerifyFixture<spec::Params>(folder, "full-spec");
            VerifyFixture<spec::Params>(folder, "empty-spec");
            VerifyFixture<native_fixture::Introspect::Response>(folder, "introspect");
            VerifyFixture<native_fixture::OpenSession::Params>(folder, "open-session");
            VerifyFixture<native_fixture::OpenSession::Response>(folder, "open-session-response");
            VerifyFixture<native_fixture::BindHotkey::Params>(folder, "bind-hotkey");
        }
        else throw std::runtime_error{ "Unknown mode: " + mode };
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
