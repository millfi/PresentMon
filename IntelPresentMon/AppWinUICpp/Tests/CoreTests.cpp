// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "TestSupport.h"

#include "../Core/AutomaticTargeting.h"
#include "../Core/ConfigModels.h"
#include "../Core/ConfigurationJson.h"
#include "../Core/ConfigurationStore.h"
#include "../Core/Introspection.h"
#include "../Core/Specification.h"
#include "../Core/StartupOptions.h"
#include "../Core/EventLifetime.h"
#include "../Core/UiDiagnostics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_set>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pmon::ui::tests
{
    namespace
    {
        using namespace core;

        void RetiredEventOwnersRejectLateCallbacks()
        {
            int calls = 0;
            std::function<void()> late;
            {
                EventLifetime events;
                late = events.Guard([&] { ++calls; });
                late();
                events.Invalidate();
                late();
            }
            late();
            Expect(calls == 1, "Retired or destroyed owners must reject queued events.");
            {
                EventLifetime events;
                late = events.Guard([&] { ++calls; });
            }
            late();
            Expect(calls == 1, "Destruction alone must invalidate callbacks.");
        }

        void DiagnosticsPreserveExceptionsAndRotate()
        {
            auto directory = std::filesystem::temp_directory_path() / ("PresentMonDiagnosticsTest-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            diagnostics::Initialize(directory.string());
            auto path = std::filesystem::u8path(diagnostics::LogPath());
            diagnostics::Record("test.action", {{"control", "TimeRange"}, {"old", 10.0}, {"new", 2.5}});
            diagnostics::Exception("test.callback", (int32_t)0x80004005u, "bad function call\nsecond line");
            std::ifstream stream(path);
            std::string line;
            uint64_t previous = 0;
            bool sawAction = false, sawException = false;
            while (std::getline(stream, line)) {
                auto record = nlohmann::json::parse(line);
                auto sequence = record.at("sequence").get<uint64_t>();
                Expect(sequence > previous && record.contains("utc") && record.contains("tid"), "Diagnostics must be ordered and timestamped.");
                previous = sequence;
                if (record.at("event") == "test.action") sawAction = record.at("details").at("new") == 2.5;
                if (record.at("event") == "exception") {
                    auto const& detail = record.at("details");
                    sawException = detail.at("hresult") == "0x80004005" && !detail.at("handlerStack").empty()
                        && detail.at("message") == "bad function call\nsecond line";
                }
            }
            stream.close();
            Expect(sawAction && sawException, "Logs must flush actions and structured exception details before termination.");
            for (int i = 0; i < 2300; ++i) diagnostics::Record("test.rotation", {{"padding", std::string(1024, 'x')}});
            Expect(std::filesystem::exists(path.string() + ".previous") && std::filesystem::file_size(path) <= 2 * 1024 * 1024,
                "Diagnostics must rotate and retain the preceding segment.");
            std::filesystem::remove_all(directory);
        }

        IntrospectionData MakeIntrospection()
        {
            IntrospectionData intro;
            intro.SystemDeviceId = 65536;
            intro.Adapters = { { 7, "Test", "Second GPU" }, { 2, "Test", "First GPU" } };
            intro.Metrics = {
                { 8, "Frame time", {}, { 1, 5 }, 3, MetricDeviceType::Independent, { { 0, 1, 0 } }, true },
                { 20, "GPU Power", {}, { 1 }, 6, MetricDeviceType::GraphicsAdapter, { { 2, 2, 0 }, { 7, 4, 0 } }, true },
                { 57, "CPU Power Limit", {}, { 0 }, 6, MetricDeviceType::System, { { 65536, 1, 0 } }, true },
            };
            return intro;
        }

        Preferences MakePreferences()
        {
            return Preferences::CreateDefault({ 7, 2 });
        }

        WidgetMetric MakeMetric(QualifiedMetric metric)
        {
            WidgetMetric line;
            line.Metric = std::move(metric);
            return line;
        }

        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream input{ path, std::ios::binary };
            return { std::istreambuf_iterator<char>{ input }, {} };
        }

        void WriteText(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream output{ path, std::ios::binary | std::ios::trunc };
            output << text;
        }

        std::filesystem::path PresetDirectory()
        {
            return std::filesystem::path{ __FILE__ }.parent_path().parent_path() / "Assets" / "Presets";
        }

        void PreferenceDefaultsUseMinimumAdapter()
        {
            const auto preferences = MakePreferences();
            Expect(preferences.SelectedPreset == Preset::Basic && preferences.AdapterId == 2,
                "Preference defaults must use the minimum reported adapter.");
        }

        void DeviceNormalizationPreservesSelectionAndClamps()
        {
            const auto intro = MakeIntrospection();
            const auto& gpu = intro.Metrics[1];
            auto preferences = MakePreferences();
            QualifiedMetric selected{ 20, 3, 7, 0, std::nullopt };

            MetricResolver::Normalize(gpu, selected, intro, preferences);
            Expect(selected.DeviceId == 7 && selected.ArrayIndex == 1,
                "Disabled per-device selection must use the default adapter array size.");

            preferences.EnablePerMetricDeviceSelection = true;
            selected.ArrayIndex = 9;
            MetricResolver::Normalize(gpu, selected, intro, preferences);
            Expect(selected.ArrayIndex == 3, "Per-device array index was not clamped.");

            selected.DeviceId = 0;
            selected.ArrayIndex = -1;
            MetricResolver::Normalize(gpu, selected, intro, preferences);
            Expect(!selected.DeviceId && selected.ArrayIndex == 0,
                "Default device or negative array index was not normalized.");

            selected.ArrayIndex = -1;
            Expect(!MetricResolver::IsAvailable(gpu, selected, intro, preferences),
                "Negative array indices cannot be available.");
        }

        void SpecificationBuildResolvesRuntimeValuesWithoutMutatingSource()
        {
            const auto intro = MakeIntrospection();
            auto preferences = MakePreferences();
            auto graph = std::make_shared<Graph>();
            graph->LabelIncludeDeviceName = true;
            graph->Metrics.push_back(MakeMetric({ 20, 3, 7, 1, std::nullopt }));
            graph->Metrics.push_back(MakeMetric({ 999, 0, std::nullopt, 0, std::nullopt }));
            auto unknown = std::make_shared<Readout>();
            unknown->Metrics.push_back(MakeMetric({ 999, 0, std::nullopt, 0, std::nullopt }));

            const auto specification = SpecificationBuilder::Build(123, preferences, { graph, unknown }, intro);
            Expect(specification.Pid == 123 && specification.Widgets.size() == 1
                && specification.Widgets[0]->Metrics.size() == 1, "Unknown metrics must be removed from specifications.");
            const auto& runtimeMetric = specification.Widgets[0]->Metrics[0].Metric;
            Expect(runtimeMetric.DeviceId == 2 && runtimeMetric.DesiredUnitId == 6,
                "Specification must resolve runtime device and unit values.");
            Expect(!specification.Widgets[0]->LabelIncludeDeviceName,
                "Per-device label suffixes must be suppressed when selection is disabled.");
            Expect(graph->Metrics.size() == 2 && graph->Metrics[0].Metric.DeviceId == 7 && graph->LabelIncludeDeviceName,
                "Specification building must not mutate the saved loadout.");

            preferences.AdapterId = 0;
            Expect(SpecificationBuilder::Build(std::nullopt, preferences, { graph }, intro).Widgets.empty(),
                "GPU metrics must be omitted when no effective adapter is selected.");
        }

        void UnsafeModelValuesAreRejected()
        {
            auto preferences = MakePreferences();
            preferences.MetricPollRate = 0;
            ExpectThrows<std::invalid_argument>([&] { ConfigurationValidation::ValidatePreferences(preferences); },
                "Zero poll rate must be rejected.");

            auto graph = std::make_shared<Graph>();
            graph->GraphType.Range = { 5.0, 1.0 };
            ExpectThrows<std::invalid_argument>([&] { ConfigurationValidation::ValidateWidgets({ graph }); },
                "Descending graph ranges must be rejected.");
        }

        void BuiltInPresetsRoundTripWithoutLosingWidgetsOrRuntimeFields()
        {
            const auto intro = MakeIntrospection();
            const auto preferences = MakePreferences();
            ConfigurationStore store{ std::filesystem::temp_directory_path() / "PresentMonCorePresetTest", PresetDirectory() };
            const std::vector<size_t> expectedWidgets{ 4, 7, 8, 5 };
            const std::vector<size_t> expectedMetrics{ 4, 10, 14, 7 };
            for (int slot = 0; slot != 4; ++slot) {
                auto file = store.LoadPreset(slot, intro, preferences);
                Expect(file.Widgets.size() == expectedWidgets[slot], "Preset widget count changed.");
                size_t metricCount = 0;
                std::unordered_set<int> keys;
                for (const auto& widget : file.Widgets) {
                    Expect(!widget->LabelIncludeDeviceId && !widget->LabelIncludeDeviceName,
                        "Missing label flags must have false defaults.");
                    metricCount += widget->Metrics.size();
                    for (const auto& line : widget->Metrics) keys.insert(line.Key);
                }
                Expect(metricCount == expectedMetrics[slot] && keys.size() == metricCount,
                    "Imported metrics must have unique keys.");
                const auto serialized = LoadoutDocument::Serialize(file);
                Expect(serialized.find("desiredUnitId") == std::string::npos, "Runtime units leaked to disk.");
                const auto reloaded = LoadoutDocument::Parse(serialized, intro, preferences);
                Expect(reloaded.Widgets.size() == file.Widgets.size(), "Roundtrip changed widget count.");
                for (size_t index = 0; index < file.Widgets.size(); ++index) {
                    Expect(reloaded.Widgets[index]->GetWidgetType() == file.Widgets[index]->GetWidgetType(),
                        "Roundtrip changed widget type.");
                }
                Expect(LoadoutDocument::Serialize(reloaded).find("\"widgetType\": 0") != std::string::npos,
                    "Widget type must remain numeric.");
            }
        }

        void PreferenceMigrationsUseSafeTimingAndAdapterDefaults()
        {
            const auto intro = MakeIntrospection();
            for (const auto adapter : { std::optional<int>{}, std::optional<int>{ 0 }, std::optional<int>{ 99 }, std::optional<int>{ 7 } }) {
                PreferenceFile source;
                source.Preferences = MakePreferences();
                auto root = Json::parse(PreferenceDocument::Serialize(source));
                root["signature"]["version"] = "1.0.0";
                root["preferences"]["adapterId"] = adapter ? Json(*adapter) : Json(nullptr);
                const auto loaded = PreferenceDocument::Parse(root.dump(), intro);
                Expect(loaded.Preferences.AdapterId == (adapter == 7 ? 7 : 2), "Legacy adapter migration changed.");
            }

            PreferenceFile noGpuSource;
            noGpuSource.Preferences.AdapterId = 7;
            auto noGpuRoot = Json::parse(PreferenceDocument::Serialize(noGpuSource));
            noGpuRoot["signature"]["version"] = "1.0.0";
            Expect(PreferenceDocument::Parse(noGpuRoot.dump(), {}).Preferences.AdapterId == 7,
                "No-adapter migration lost a saved adapter.");

            PreferenceFile timingSource;
            auto timingRoot = Json::parse(PreferenceDocument::Serialize(timingSource));
            timingRoot["signature"]["version"] = "0.16.0";
            timingRoot["preferences"]["samplingPeriodMs"] = 20;
            timingRoot["preferences"]["samplesPerFrame"] = 5;
            timingRoot["preferences"]["enableFlashInjection"] = true;
            timingRoot["preferences"]["flashInjectionSize"] = 100;
            timingRoot["preferences"]["captureDuration"] = "12.5";
            const auto timing = PreferenceDocument::Parse(timingRoot.dump(), intro);
            Expect(timing.Preferences.MetricPollRate == 50.0 && timing.Preferences.OverlayDrawRate == 10.0,
                "Legacy timing migration failed.");
            Expect(timing.Preferences.MetricsOffset == 150.0 && timing.Preferences.ManualEtwFlush,
                "Legacy ETW defaults failed.");
            Expect(timing.Preferences.CaptureDuration == 12.5,
                "Legacy numeric strings must migrate.");
            Expect(PreferenceDocument::Serialize(timing).find("flashInjection") == std::string::npos,
                "Removed flash injection settings survived persistence.");
        }

        void LoadoutMigrationsPreserveExtensionsAndRemoveRuntimeUnits()
        {
            const auto intro = MakeIntrospection();
            const auto preferences = MakePreferences();
            auto graph = std::make_shared<Graph>();
            graph->Metrics.push_back(MakeMetric({ 57, 0, 0, 10, std::nullopt }));
            auto source = LoadoutDocument::FromWidgets({ graph });
            auto root = Json::parse(LoadoutDocument::Serialize(source));
            root["signature"]["version"] = "0.13.0";
            root["widgets"][0]["metrics"][0]["labelIncludeDeviceId"] = true;
            root["widgets"][0]["metrics"][0]["metric"]["desiredUnitId"] = 999;
            const auto migrated = LoadoutDocument::Parse(root.dump(), intro, preferences);
            const auto& line = migrated.Widgets[0]->Metrics[0];
            Expect(line.Metric.DeviceId == 65536 && line.Metric.StatId == 0,
                "CPU loadout migration failed.");
            Expect(line.Metric.DesiredUnitId == 6 && migrated.Widgets[0]->LabelIncludeDeviceId,
                "Runtime unit or label migration failed.");
            Expect(!line.AdditionalProperties.contains("labelIncludeDeviceId"),
                "Per-line label flag survived migration.");

            auto unknown = std::make_shared<Readout>();
            unknown->Metrics.push_back(MakeMetric({ 999, 0, std::nullopt, 0, std::nullopt }));
            auto extensionRoot = Json::parse(LoadoutDocument::Serialize(LoadoutDocument::FromWidgets({ unknown })));
            extensionRoot["widgets"][0]["futureStyle"] = "retained";
            extensionRoot["widgets"][0]["metrics"][0]["metric"]["desiredUnitId"] = 999;
            const auto persisted = LoadoutDocument::Serialize(LoadoutDocument::Parse(extensionRoot.dump(), intro, preferences));
            Expect(persisted.find("futureStyle") != std::string::npos && persisted.find("desiredUnitId") == std::string::npos,
                "Unknown property persistence or runtime-unit removal failed.");
        }

        void InvalidDocumentsAreRejected()
        {
            const auto intro = MakeIntrospection();
            PreferenceFile preferences;
            auto preferenceRoot = Json::parse(PreferenceDocument::Serialize(preferences));
            for (const auto& version : { "0.15.0", "9.0.0", "invalid" }) {
                preferenceRoot["signature"]["version"] = version;
                ExpectThrows<std::runtime_error>([&] { PreferenceDocument::Parse(preferenceRoot.dump(), intro); },
                    "Unsupported preference versions must be rejected.");
            }
            LoadoutFile loadout;
            auto loadoutRoot = Json::parse(LoadoutDocument::Serialize(loadout));
            loadoutRoot["signature"]["version"] = "0.12.0";
            ExpectThrows<std::runtime_error>([&] { LoadoutDocument::Parse(loadoutRoot.dump(), intro, MakePreferences()); },
                "Unsupported loadout versions must be rejected.");
            loadoutRoot["signature"]["code"] = "wrong";
            ExpectThrows<std::runtime_error>([&] { LoadoutDocument::Parse(loadoutRoot.dump(), intro, MakePreferences()); },
                "Loadouts with the wrong code must be rejected.");
        }

        void AtomicPersistenceRetainsCorruptOriginalsAndLastValidBackup()
        {
            const auto intro = MakeIntrospection();
            const auto preferences = MakePreferences();
            const auto directory = std::filesystem::temp_directory_path()
                / ("PresentMonCoreTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(directory);
            try {
                ConfigurationStore store{ directory, PresetDirectory() };
                WriteText(store.PreferencesPath(), "invalid original");
                auto defaults = store.LoadPreferences(intro);
                Expect(defaults.Preferences.AdapterId == 2 && store.LastWarning().has_value(),
                    "Configuration corruption recovery must be reported.");
                Expect(ReadText(store.PreferencesPath()) == "invalid original", "Loading must retain the original file.");
                store.SavePreferences(defaults);
                bool recoveryFound = false;
                for (const auto& entry : std::filesystem::directory_iterator{ directory }) {
                    if (entry.path().filename().string().find("preferences.json.recovery-") == 0
                        && ReadText(entry.path()) == "invalid original") {
                        recoveryFound = true;
                    }
                }
                Expect(recoveryFound, "Recovery copy of corrupt preferences is missing.");
                defaults.Preferences.OverlayWidth = 500.0;
                store.SavePreferences(defaults);
                const auto backupPath = std::filesystem::path{ store.PreferencesPath().string() + ".bak" };
                Expect(PreferenceDocument::Parse(ReadText(backupPath), intro).Preferences.OverlayWidth == 400.0,
                    "The previous valid backup was lost.");
                Expect(store.LoadPreferences(intro).Preferences.OverlayWidth == 500.0,
                    "Saved preferences were not read back.");
                for (const auto& entry : std::filesystem::directory_iterator{ directory }) {
                    Expect(entry.path().extension() != ".tmp", "Atomic write left a temporary file.");
                }
                auto graph = std::make_shared<Graph>();
                graph->Metrics.push_back(MakeMetric(MetricResolver::CreateDefault(intro, preferences)));
                store.SaveCustom(LoadoutDocument::FromWidgets({ graph }));
                Expect(store.LoadCustom(intro, preferences).Widgets.size() == 1, "Custom loadout autosave failed.");
            }
            catch (...) {
                std::filesystem::remove_all(directory);
                throw;
            }
            std::filesystem::remove_all(directory);
        }

        void NativeLaunchArgumentsAreParsedAndUnsupportedArgumentsRejected()
        {
            const auto defaults = services::StartupOptions::Parse({});
            Expect(std::filesystem::path(defaults.DataDirectory).filename() == "FluentPresentMon"
                && std::filesystem::path(defaults.AppDataDirectory).filename() == "FluentPresentMon"
                && std::filesystem::path(ConfigurationStore::DefaultDataDirectory()).filename() == "FluentPresentMon",
                "Default data paths must remain separate from upstream PresentMon.");
            const auto workingDirectory = std::filesystem::current_path().string();
            const auto options = services::StartupOptions::Parse({
                "--p2c-act-name", "presentmon-test-pipe",
                "--p2c-files-working",
                "--p2c-ui-mutex-name", "PresentMonTestUI",
                "--p2c-log-folder", workingDirectory,
                "--p2c-enable-ui-dev-options",
            });
            Expect(options.PipeName == "presentmon-test-pipe" && options.FilesWorking
                && options.MutexSuffix == "PresentMonTestUI" && options.LogDirectory == workingDirectory
                && options.EnableDevOptions && options.DataDirectory == workingDirectory,
                "Native launch arguments were not parsed correctly.");

            for (const auto& argument : { "--disable-gpu", "--p2c-ui-url=about:blank", "--p2c-log-level=debug" }) {
                ExpectThrows<std::invalid_argument>([&] { services::StartupOptions::Parse({ argument }); },
                    "Unsupported UI launch arguments must be rejected.");
            }
        }

        void AutomaticTargetingFollowsEligibleGames()
        {
            constexpr int gameA = 10;
            constexpr int appB = 20;
            constexpr int gameC = 30;
            constexpr int restartedA = 40;
            std::optional<int> selected = appB;
            std::vector<int> changes;
            std::vector<core::GpuProcessSample> samples;
            std::vector<int> measurable;
            const auto scan = [&] {
                AutomaticTargeting::Update([&] { return samples; }, [&](const auto& pids) {
                    auto expected = samples;
                    expected.erase(std::remove_if(expected.begin(), expected.end(), [](const auto& sample) {
                        return !std::isfinite(sample.RunningTime) || sample.RunningTime <= 0.0;
                    }), expected.end());
                    std::vector<int> expectedPids;
                    for (const auto& sample : expected) {
                        if (std::find(expectedPids.begin(), expectedPids.end(), sample.Pid) == expectedPids.end()) {
                            expectedPids.push_back(sample.Pid);
                        }
                    }
                    Expect(pids == expectedPids, "Every active candidate must be probed.");
                    return measurable;
                }, [] { return true; }, [&](std::optional<int> pid) { selected = pid; changes.push_back(*pid); });
            };

            samples = { { appB, 100.0 }, { gameA, 50.0 } };
            measurable = { gameA };
            scan();
            Expect(selected == gameA, "A measurable game must replace an unmeasurable foreground process.");

            selected.reset();
            samples = { { appB, 100.0 } };
            measurable.clear();
            scan();
            Expect(!selected, "An unmeasurable application must not be selected.");

            samples.push_back({ restartedA, 50.0 });
            measurable.push_back(restartedA);
            scan();
            Expect(selected == restartedA, "Restarted games must be reacquired by their new PID.");

            samples.push_back({ gameC, 75.0 });
            measurable.push_back(gameC);
            scan();
            Expect(selected == gameC, "Higher-load eligible games must replace the target.");

            samples = { { restartedA, 90.0 }, { gameC, 75.0 } };
            scan();
            Expect(selected == restartedA, "Load changes must be reevaluated on each scan.");

            measurable = { gameC };
            scan();
            Expect(selected == gameC && changes.size() == 5,
                "Only scans with an eligible candidate may request selection.");
        }

        void AutomaticTargetingDiscardsSupersededScans()
        {
            for (const auto& stage : { std::string{ "gpu" }, std::string{ "raw" } }) {
                int revision = 1;
                int probeCalls = 0;
                int selectionCalls = 0;
                AutomaticTargeting::Update([&] {
                    if (stage == "gpu") {
                        ++revision;
                    }
                    return std::vector<core::GpuProcessSample>{ { 10, 20.0 } };
                }, [&](const auto&) {
                    ++probeCalls;
                    if (stage == "raw") {
                        ++revision;
                    }
                    return std::vector<int>{ 10 };
                }, [&] { return revision == 1; }, [&](std::optional<int>) { ++selectionCalls; });
                Expect(selectionCalls == 0, "A stale scan must not select a target.");
                if (stage == "gpu") {
                    Expect(probeCalls == 0, "A stale GPU scan must not start raw GPU Busy probing.");
                }
            }
        }

        void AutomaticTargetingRejectsInvalidLoadsAndBreaksTiesByPid()
        {
            std::optional<int> selected;
            AutomaticTargeting::Update([] {
                return std::vector<core::GpuProcessSample>{
                    { 1, std::numeric_limits<double>::quiet_NaN() }, { 2, std::numeric_limits<double>::infinity() },
                    { 3, -1.0 }, { 4, 0.0 }, { 6, 50.0 }, { 5, 50.0 },
                };
            }, [](const auto& pids) {
                Expect(pids == std::vector<int>({ 6, 5 }), "Invalid and inactive loads must not be probed.");
                return pids;
            }, [] { return true; }, [&](std::optional<int> pid) { selected = pid; });
            Expect(selected == 5, "Equal loads must choose the lower PID deterministically.");
        }

        void AutomaticTargetingReleasesIdleCandidates()
        {
            std::optional<int> selected;
            std::vector<core::GpuProcessSample> samples;
            for (int pid = 100; pid < 1100; ++pid) {
                samples.push_back({ pid, 0.0 });
            }
            samples.insert(samples.end(), { { 10, 50.0 }, { 20, 100.0 } });
            std::vector<std::vector<int>> requests;
            const auto scan = [&] {
                AutomaticTargeting::Update([&] { return samples; }, [&](const auto& pids) {
                    requests.push_back(pids);
                    return pids;
                }, [] { return true; }, [&](std::optional<int> pid) { selected = pid; });
            };

            scan();
            Expect(requests[0] == std::vector<int>({ 10, 20 }) && selected == 20,
                "Idle applications must not increase the number of frame trackers.");
            samples = { { 10, 0.0 }, { 20, 0.0 } };
            scan();
            Expect(requests[1].empty() && selected == 20,
                "Idle scans must release probing while retaining the existing target.");
            samples = { { 10, 50.0 }, { 20, 0.0 } };
            scan();
            Expect(requests[2] == std::vector<int>({ 10 }) && selected == 10,
                "Resumed applications must be offered to the probe again.");
        }

        void AutomaticTargetingPreservesSelectionWithoutEligibleCandidates()
        {
            for (const auto initial : { std::optional<int>{ 10 }, std::optional<int>{} }) {
                auto selected = initial;
                int selectionCalls = 0;
                const std::vector<std::vector<core::GpuProcessSample>> scans = {
                    { { 10, 50.0 }, { 20, 100.0 } }, { { 10, 50.0 }, { 20, 100.0 } },
                    { { 10, 0.0 }, { 20, 0.0 } }, {},
                };
                for (const auto& samples : scans) {
                    AutomaticTargeting::Update([&] { return samples; }, [](const auto&) { return std::vector<int>{}; },
                        [] { return true; }, [&](std::optional<int> pid) { selected = pid; ++selectionCalls; });
                    Expect(selected == initial && selectionCalls == 0,
                        "No eligible candidate must preserve selection without invoking capture stop.");
                }
                AutomaticTargeting::Update([] { return std::vector<core::GpuProcessSample>{ { 10, 100.0 }, { 20, 50.0 } }; },
                    [](const auto&) { return std::vector<int>{ 20 }; }, [] { return true; },
                    [&](std::optional<int> pid) { selected = pid; ++selectionCalls; });
                Expect(selected == 20 && selectionCalls == 1,
                    "A newly eligible process must replace an ineligible retained target or fill an empty selection.");
            }
        }
    }
}

int RunCoreTests()
{
    using namespace pmon::ui::tests;
    return RunTest(RetiredEventOwnersRejectLateCallbacks)
        + RunTest(DiagnosticsPreserveExceptionsAndRotate)
        + RunTest(PreferenceDefaultsUseMinimumAdapter)
        + RunTest(DeviceNormalizationPreservesSelectionAndClamps)
        + RunTest(SpecificationBuildResolvesRuntimeValuesWithoutMutatingSource)
        + RunTest(UnsafeModelValuesAreRejected)
        + RunTest(BuiltInPresetsRoundTripWithoutLosingWidgetsOrRuntimeFields)
        + RunTest(PreferenceMigrationsUseSafeTimingAndAdapterDefaults)
        + RunTest(LoadoutMigrationsPreserveExtensionsAndRemoveRuntimeUnits)
        + RunTest(InvalidDocumentsAreRejected)
        + RunTest(AtomicPersistenceRetainsCorruptOriginalsAndLastValidBackup)
        + RunTest(NativeLaunchArgumentsAreParsedAndUnsupportedArgumentsRejected)
        + RunTest(AutomaticTargetingFollowsEligibleGames)
        + RunTest(AutomaticTargetingDiscardsSupersededScans)
        + RunTest(AutomaticTargetingRejectsInvalidLoadsAndBreaksTiesByPid)
        + RunTest(AutomaticTargetingReleasesIdleCandidates)
        + RunTest(AutomaticTargetingPreservesSelectionWithoutEligibleCandidates);
}
