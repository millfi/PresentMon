// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include "GpuBusySampleWindow.h"
#include <PresentMonAPIWrapper/Session.h>
#include <PresentMonAPIWrapperCommon/Exception.h>
#include <CommonUtilities/Qpc.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kproc
{
    // Uses a separate API session so probing cannot stop or consume the overlay's tracking.
    class GpuBusyProbe
    {
    public:
        explicit GpuBusyProbe(pmapi::Session session) : session_{ std::move(session) }
        {
            std::array elements{
                PM_QUERY_ELEMENT{ .metric = PM_METRIC_PRESENT_START_QPC, .stat = PM_STAT_NONE },
                PM_QUERY_ELEMENT{ .metric = PM_METRIC_GPU_BUSY, .stat = PM_STAT_NONE },
            };
            frames_ = session_.RegisterFrameQuery(elements);
            timestampOffset_ = elements[0].dataOffset;
            gpuBusyOffset_ = elements[1].dataOffset;
            frameBlobs_ = frames_.MakeBlobContainer(256);
        }

        std::vector<uint32_t> Poll(const std::vector<uint32_t>& pids)
        {
            const std::unordered_set<uint32_t> wanted{ pids.begin(), pids.end() };
            std::erase_if(entries_, [&](const auto& entry) { return !wanted.contains(entry.first); });
            std::vector<uint32_t> eligible;
            const auto maxAge = 2 * pmon::util::GetTimestampFrequencyUint64();
            for (const auto pid : wanted) {
                try {
                    auto it = entries_.find(pid);
                    if (it == entries_.end()) {
                        entries_.emplace(pid, std::make_unique<Entry>(session_, pid));
                        continue; // New trackers need actual frames before they can qualify.
                    }
                    auto& entry = *it->second;
                    // Bound work even if a producer continuously fills the queue.
                    for (int batch = 0; batch < 64; ++batch) {
                        frames_.Consume(entry.tracker, frameBlobs_);
                        for (const auto blob : frameBlobs_) {
                            uint64_t timestamp = 0;
                            double rawGpuBusy = 0;
                            std::memcpy(&timestamp, blob + timestampOffset_, sizeof(timestamp));
                            std::memcpy(&rawGpuBusy, blob + gpuBusyOffset_, sizeof(rawGpuBusy));
                            if (timestamp == 0) {
                                entry.samples.Reset();
                                continue;
                            }
                            // Require a new set of samples after a gap in frame production.
                            if (timestamp > entry.latestFrame && timestamp - entry.latestFrame > maxAge) {
                                entry.samples.Reset();
                            }
                            entry.latestFrame = std::max(entry.latestFrame, timestamp);
                            entry.samples.Add(rawGpuBusy);
                        }
                        if (!frameBlobs_.AllBlobsPopulated()) break;
                    }
                    const auto now = (uint64_t)pmon::util::GetCurrentTimestamp();
                    if (entry.latestFrame == 0 || entry.latestFrame > now || now - entry.latestFrame > maxAge) continue;

                    if (entry.samples.HasVariation()) eligible.push_back(pid);
                }
                catch (const pmapi::ApiErrorException& ex) {
                    entries_.erase(pid);
                    // An exited/inaccessible process must not prevent other candidates from qualifying.
                    if (ex.GetCode() != PM_STATUS_INVALID_PID && ex.GetCode() != PM_STATUS_FAILURE) throw;
                }
            }
            return eligible;
        }

    private:
        struct Entry
        {
            Entry(pmapi::Session& session, uint32_t pid) : tracker{ session.TrackProcess(pid) } {}
            pmapi::ProcessTracker tracker;
            GpuBusySampleWindow samples;
            uint64_t latestFrame = 0;
        };
        pmapi::Session session_;
        pmapi::FrameQuery frames_;
        pmapi::BlobContainer frameBlobs_;
        uint64_t timestampOffset_ = 0;
        uint64_t gpuBusyOffset_ = 0;
        std::unordered_map<uint32_t, std::unique_ptr<Entry>> entries_;
    };
}
