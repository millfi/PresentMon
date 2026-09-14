// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "WindowsServices.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace pmon::ui::services {
namespace {

constexpr wchar_t WindowIdentityProperty[] = L"FluentPresentMon.UiMutexSuffixAtom";
constexpr uint32_t PdhMoreData = 0x800007d2u;
constexpr uint32_t PdhNoData = 0x800007d5u;
constexpr uint32_t PdhNoInstance = 0x800007d1u;
constexpr uint32_t PdhNewData = 1u;

struct HandleCloser {
    void operator()(void* handle) const noexcept
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

void ThrowIfCancelled(std::stop_token cancellation)
{
    if (cancellation.stop_requested()) {
        throw std::runtime_error("Operation canceled.");
    }
}

std::string Utf8FromWide(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    auto const length = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        throw std::runtime_error("Could not convert a Windows string to UTF-8.");
    }
    std::string converted((size_t)length, '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), converted.data(), length, nullptr, nullptr) == 0) {
        throw std::runtime_error("Could not convert a Windows string to UTF-8.");
    }
    return converted;
}

std::wstring WideFromUtf8(std::string const& value)
{
    if (value.empty()) {
        return {};
    }
    auto const length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
    if (length == 0) {
        throw std::runtime_error("Could not convert a UTF-8 string to a Windows string.");
    }
    std::wstring converted((size_t)length, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), converted.data(), length) == 0) {
        throw std::runtime_error("Could not convert a UTF-8 string to a Windows string.");
    }
    return converted;
}

std::string ToLowerAscii(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return (char)std::tolower(character);
    });
    return value;
}

bool IsBlocked(std::unordered_set<std::string> const* blocklist, std::string const& executable)
{
    return blocklist != nullptr && (blocklist->contains(executable) || blocklist->contains(ToLowerAscii(executable)));
}

void ThrowIfPdhFailed(DWORD status, char const* operation)
{
    if (status != ERROR_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (PDH status " + std::to_string(status) + ").");
    }
}

bool CollectGpuData(PDH_HQUERY query)
{
    auto const status = (uint32_t)PdhCollectQueryData(query);
    if (status == PdhNoData) {
        return false;
    }
    ThrowIfPdhFailed(status, "Collecting GPU performance data");
    return true;
}

bool TryGet3DProcessId(std::wstring_view instanceName, int& pid)
{
    constexpr std::wstring_view prefix = L"pid_";
    constexpr std::wstring_view suffix = L"_engtype_3D";
    if (!instanceName.starts_with(prefix) || !instanceName.ends_with(suffix)) {
        return false;
    }
    auto const end = instanceName.find(L'_', prefix.size());
    if (end == std::wstring_view::npos || end == prefix.size()) {
        return false;
    }
    int parsed = 0;
    for (size_t index = prefix.size(); index < end; ++index) {
        auto const character = instanceName[index];
        if (character < L'0' || character > L'9' || parsed > (INT_MAX - (character - L'0')) / 10) {
            return false;
        }
        parsed = parsed * 10 + (character - L'0');
    }
    pid = parsed;
    return true;
}

struct GpuRunningTime {
    int Pid = 0;
    double RunningTime = 0.0;
};

std::unordered_map<std::wstring, GpuRunningTime> ReadGpuRunningTimes(
    PDH_HCOUNTER counter,
    std::unordered_map<int, ProcessEntry> const& candidates,
    std::stop_token cancellation)
{
    std::unordered_map<std::wstring, GpuRunningTime> results;
    for (int attempt = 0; attempt < 3; ++attempt) {
        ThrowIfCancelled(cancellation);
        DWORD bufferSize = 0;
        DWORD count = 0;
        auto status = (uint32_t)PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &count, nullptr);
        if (status == PdhNoData || status == PdhNoInstance || (status == ERROR_SUCCESS && bufferSize == 0)) {
            return results;
        }
        if (status != PdhMoreData) {
            ThrowIfPdhFailed(status, "Sizing GPU performance data");
        }
        std::vector<std::byte> buffer(bufferSize);
        status = (uint32_t)PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &count,
            reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data()));
        if (status == PdhMoreData) {
            continue;
        }
        if (status == PdhNoData || status == PdhNoInstance) {
            return results;
        }
        ThrowIfPdhFailed(status, "Reading GPU performance data");
        auto const* items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
        for (DWORD index = 0; index < count; ++index) {
            ThrowIfCancelled(cancellation);
            auto const& item = items[index];
            if ((uint32_t)item.FmtValue.CStatus > PdhNewData || !std::isfinite(item.FmtValue.doubleValue)) {
                continue;
            }
            int pid = 0;
            std::wstring_view const name = item.szName == nullptr ? L"" : item.szName;
            if (TryGet3DProcessId(name, pid) && candidates.contains(pid)) {
                results.insert_or_assign(std::wstring(name), GpuRunningTime{ pid, item.FmtValue.doubleValue });
            }
        }
        return results;
    }
    throw std::runtime_error("GPU performance counters changed repeatedly during enumeration. Please retry.");
}

struct WindowEnumeration {
    std::unordered_map<int, std::string> const* ProcessNames = nullptr;
    std::unordered_map<int, HWND>* Windows = nullptr;
};

BOOL CALLBACK EnumerateWindow(HWND hwnd, LPARAM parameter)
{
    auto const context = reinterpret_cast<WindowEnumeration*>(parameter);
    if (GetWindow(hwnd, GW_OWNER) != nullptr || !IsWindowVisible(hwnd)) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (context->ProcessNames->contains((int)pid)) {
        context->Windows->try_emplace((int)pid, hwnd);
    }
    return TRUE;
}

}

std::string ProcessEntry::DisplayName() const
{
    if (WindowName.empty()) {
        return Name + " (" + std::to_string(Pid) + ")";
    }
    return Name + " (" + std::to_string(Pid) + ") - " + WindowName;
}

std::vector<ProcessEntry> WindowsServices::EnumerateProcesses(
    std::unordered_set<std::string> const* blocklist,
    std::stop_token cancellation)
{
    ThrowIfCancelled(cancellation);
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not enumerate running processes.");
    }

    std::unordered_map<int, std::string> processNames;
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (!Process32FirstW(snapshot.get(), &process)) {
        throw std::runtime_error("Could not read the process snapshot.");
    }
    auto const currentPid = GetCurrentProcessId();
    do {
        ThrowIfCancelled(cancellation);
        auto const name = Utf8FromWide(process.szExeFile);
        if (process.th32ProcessID != currentPid && !IsBlocked(blocklist, name)) {
            processNames.insert_or_assign((int)process.th32ProcessID, name);
        }
    } while (Process32NextW(snapshot.get(), &process));
    auto const enumerationError = GetLastError();
    if (enumerationError != ERROR_NO_MORE_FILES) {
        throw std::runtime_error("Could not finish reading the process snapshot.");
    }

    std::unordered_map<int, HWND> windows;
    WindowEnumeration context{ &processNames, &windows };
    SetLastError(ERROR_SUCCESS);
    if (!EnumWindows(EnumerateWindow, reinterpret_cast<LPARAM>(&context))) {
        throw std::runtime_error("Could not enumerate application windows.");
    }

    std::vector<ProcessEntry> results;
    results.reserve(windows.size());
    for (auto const& entry : windows) {
        ThrowIfCancelled(cancellation);
        auto const length = GetWindowTextLengthW(entry.second);
        std::wstring title((size_t)std::max(0, length) + 1, L'\0');
        if (length > 0) {
            GetWindowTextW(entry.second, title.data(), length + 1);
            title.resize((size_t)length);
        } else {
            title.clear();
        }
        results.push_back(ProcessEntry{ entry.first, processNames.at(entry.first), Utf8FromWide(title) });
    }
    std::ranges::sort(results, [](ProcessEntry const& left, ProcessEntry const& right) {
        auto const leftName = ToLowerAscii(left.Name);
        auto const rightName = ToLowerAscii(right.Name);
        return leftName == rightName ? left.Pid < right.Pid : leftName < rightName;
    });
    return results;
}

std::vector<GpuProcessSample> WindowsServices::GetGpuProcessSamples(
    std::unordered_set<std::string> const* blocklist,
    std::stop_token cancellation)
{
    auto processes = EnumerateProcesses(blocklist, cancellation);
    std::unordered_map<int, ProcessEntry> candidates;
    for (auto const& process : processes) {
        candidates.insert_or_assign(process.Pid, process);
    }
    if (candidates.empty()) {
        return {};
    }

    PDH_HQUERY query = nullptr;
    ThrowIfPdhFailed(PdhOpenQueryW(nullptr, 0, &query), "Opening the GPU performance query");
    struct QueryCloser {
        PDH_HQUERY Query = nullptr;
        ~QueryCloser() { if (Query != nullptr) PdhCloseQuery(Query); }
    } closer{ query };
    PDH_HCOUNTER counter = nullptr;
    ThrowIfPdhFailed(PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Running time", 0, &counter),
        "Adding the GPU running-time counter");
    auto emptySamples = [&] {
        std::vector<GpuProcessSample> samples;
        samples.reserve(candidates.size());
        for (auto const& candidate : candidates) {
            samples.push_back(GpuProcessSample{ candidate.first, 0.0 });
        }
        return samples;
    };
    if (!CollectGpuData(query) || !CollectGpuData(query)) {
        return emptySamples();
    }
    auto const first = ReadGpuRunningTimes(counter, candidates, cancellation);
    for (int elapsed = 0; elapsed < 100; elapsed += 10) {
        ThrowIfCancelled(cancellation);
        Sleep(10);
    }
    if (!CollectGpuData(query)) {
        return emptySamples();
    }
    auto const second = ReadGpuRunningTimes(counter, candidates, cancellation);
    std::unordered_map<int, double> totals;
    for (auto const& entry : second) {
        ThrowIfCancelled(cancellation);
        auto const previous = first.find(entry.first);
        if (previous == first.end()) {
            continue;
        }
        auto const delta = entry.second.RunningTime - previous->second.RunningTime;
        if (std::isfinite(delta) && delta > 0.0) {
            totals[entry.second.Pid] += delta;
        }
    }
    std::vector<GpuProcessSample> samples;
    samples.reserve(candidates.size());
    for (auto const& candidate : candidates) {
        samples.push_back(GpuProcessSample{ candidate.first, totals[candidate.first] });
    }
    return samples;
}

void WindowsServices::SetWindowIdentity(void* hwnd, std::string const& mutexSuffix)
{
    if (hwnd == nullptr) {
        throw std::invalid_argument("A window handle is required.");
    }
    ClearWindowIdentity(hwnd);
    auto const identity = WideFromUtf8(mutexSuffix.empty() ? StartupOptions::DefaultMutexSuffix : mutexSuffix);
    auto const atom = GlobalAddAtomW(identity.c_str());
    if (atom == 0) {
        throw std::runtime_error("Could not create the PresentMon window identity.");
    }
    if (!SetPropW(static_cast<HWND>(hwnd), WindowIdentityProperty,
        reinterpret_cast<HANDLE>((UINT_PTR)atom))) {
        GlobalDeleteAtom(atom);
        throw std::runtime_error("Could not set the PresentMon window identity.");
    }
}

void WindowsServices::ClearWindowIdentity(void* hwnd) noexcept
{
    if (hwnd == nullptr) {
        return;
    }
    auto const atom = RemovePropW(static_cast<HWND>(hwnd), WindowIdentityProperty);
    if (atom != nullptr) {
        GlobalDeleteAtom((ATOM)reinterpret_cast<UINT_PTR>(atom));
    }
}

}
