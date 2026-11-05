// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Benchmark measurement harness: warm-up and repetition protocol,
// duration statistics, argument parsing, and result formatting.
// Timing uses std::chrono::steady_clock; workloads are deterministic
// but durations are observed quantities and vary between runs.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace tickdots::bench {

// The observable result of one benchmark repetition. Repetitions of
// the same benchmark must produce identical outcomes; the runner
// treats a mismatch as a failed benchmark, not a timing result.
struct BenchOutcome {
  std::uint64_t eventsProcessed = 0;
  std::uint64_t fills = 0;
  std::uint64_t samples = 0; // latency samples, for the observer benchmark
  std::size_t resting = 0;

  bool operator==(const BenchOutcome&) const = default;
};

// Summary statistics over the measured durations of one benchmark,
// in nanoseconds. stddev is the sample standard deviation (n - 1),
// defined as 0 for a single repetition.
struct Stats {
  double minNs = 0;
  double medianNs = 0;
  double meanNs = 0;
  double maxNs = 0;
  double stddevNs = 0;
};

// Computes the statistics above. Throws std::invalid_argument on an
// empty input: there is no meaningful statistic of zero durations.
[[nodiscard]] Stats computeStats(std::vector<double> durationsNs);

// One prepared benchmark repetition. prepare() runs outside the
// timed region (state construction, workload set-up events); the
// runner times only runMeasured().
struct PreparedRun {
  virtual ~PreparedRun() = default;
  virtual BenchOutcome runMeasured() = 0;
};

using PrepareFn = std::function<std::unique_ptr<PreparedRun>()>;

struct BenchConfig {
  int warmups = 2;
  int repetitions = 7;
};

struct BenchResult {
  std::string name;
  std::string identity;
  std::uint64_t eventsPerRep = 0;
  BenchOutcome outcome;
  Stats stats;
  int warmups = 0;
  int repetitions = 0;
};

// Runs the warm-up and measurement protocol. Every repetition's
// outcome is checked against the first; a mismatch throws
// std::runtime_error. Warm-up outcomes are checked too, but their
// durations are discarded.
[[nodiscard]] BenchResult runBenchmark(const std::string& name,
                                       const std::string& identity,
                                       std::uint64_t eventsPerRep,
                                       const BenchConfig& config,
                                       const PrepareFn& prepare);

// Build and environment metadata, captured at configure/build time
// where available. Missing optional values are reported as
// "unknown"; the benchmark never fails for lack of metadata.
struct Metadata {
  std::string version;
  std::string commit;
  std::string compiler;
  std::string buildType;
};

[[nodiscard]] Metadata benchmarkMetadata();

struct BenchOptions {
  std::string workload = "all";
  std::size_t size = 100000;
  std::uint64_t seed = 1901;
  int warmups = 2;
  int repetitions = 7;
  bool csv = false;
  bool help = false;
};

// Parses bench arguments (excluding argv[0]). On error, prints the
// diagnostic and usage to stderr and returns std::nullopt.
[[nodiscard]] std::optional<BenchOptions> parseBenchArgs(const std::vector<std::string>& args);

// Prints the bench usage text (the same text parse errors show).
void printBenchUsage(std::ostream& out);

void printMetadata(std::ostream& out, const Metadata& metadata);
void printTextResult(std::ostream& out, const BenchResult& result);
[[nodiscard]] std::string csvHeader();
[[nodiscard]] std::string csvRow(const BenchResult& result, const Metadata& metadata);

} // namespace tickdots::bench
