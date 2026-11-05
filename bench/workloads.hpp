// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Deterministic benchmark workloads. Construction uses the
// production SplitMix64 (the same stream as SPEC.md section 14)
// and, for the mixed session, the generation-version-1 event
// generator. Every workload is verified at construction: all events
// must pass validateEvent, and workloads with by-construction
// outcomes carry expectations the runner checks before timing.

#pragma once

#include "bench/harness.hpp"
#include "tickdots/event/event.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tickdots::bench {

struct Workload {
  std::string name;
  std::string identity;
  std::uint64_t seed = 0;
  std::vector<Event> setup;  // applied outside the timed region
  std::vector<Event> events; // measured
  std::optional<std::uint64_t> expectedFills;
  std::optional<std::size_t> expectedResting;
};

// The workload names accepted by --workload, in suite order.
[[nodiscard]] const std::vector<std::string>& workloadNames();

// Builds a workload by name. Throws std::invalid_argument for an
// unknown name, a zero size, or a constructed event that fails
// validation.
[[nodiscard]] Workload makeWorkload(const std::string& name, std::size_t size, std::uint64_t seed);

// Serializes events to the CSV input format (SPEC.md section 9), for
// the pipeline benchmarks. Modify fields that are zero serialize as
// empty (unchanged), as the format defines.
[[nodiscard]] std::string toCsvText(const std::vector<Event>& events);

// Preparation for an engine benchmark: a fresh book and engine (plus
// the latency observer when `latency` is set), with the workload's
// set-up events already applied.
[[nodiscard]] PrepareFn enginePrepare(const Workload& workload, bool latency);

// Preparation for the parse-only pipeline benchmark.
[[nodiscard]] PrepareFn parsePrepare(std::string csvText, std::string instrument);

// Preparation for the parse-and-replay pipeline benchmark.
[[nodiscard]] PrepareFn parseReplayPrepare(std::string csvText, std::string instrument);

} // namespace tickdots::bench
