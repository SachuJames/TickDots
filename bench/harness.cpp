// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Benchmark measurement harness implementation. See harness.hpp.

#include "bench/harness.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <utility>

// Build metadata injected by CMake (see the tickforge_bench_lib
// target). Fallbacks keep the harness buildable without them.
#ifndef TICKFORGE_BENCH_COMMIT
#define TICKFORGE_BENCH_COMMIT "unknown"
#endif
#ifndef TICKFORGE_BENCH_COMPILER
#define TICKFORGE_BENCH_COMPILER "unknown"
#endif
#ifndef TICKFORGE_BENCH_BUILD_TYPE
#define TICKFORGE_BENCH_BUILD_TYPE "unknown"
#endif

#include "tickforge/version.hpp"

namespace tickforge::bench {

Stats computeStats(std::vector<double> durationsNs) {
  if (durationsNs.empty()) {
    throw std::invalid_argument("computeStats: no durations");
  }
  std::sort(durationsNs.begin(), durationsNs.end());
  Stats stats;
  stats.minNs = durationsNs.front();
  stats.maxNs = durationsNs.back();
  const std::size_t n = durationsNs.size();
  stats.medianNs =
      (n % 2 == 1) ? durationsNs[n / 2] : (durationsNs[n / 2 - 1] + durationsNs[n / 2]) / 2.0;
  const double sum = std::accumulate(durationsNs.begin(), durationsNs.end(), 0.0);
  stats.meanNs = sum / static_cast<double>(n);
  if (n > 1) {
    double squared = 0.0;
    for (const double d : durationsNs) {
      const double delta = d - stats.meanNs;
      squared += delta * delta;
    }
    stats.stddevNs = std::sqrt(squared / static_cast<double>(n - 1));
  }
  return stats;
}

BenchResult runBenchmark(const std::string& name,
                         const std::string& identity,
                         std::uint64_t eventsPerRep,
                         const BenchConfig& config,
                         const PrepareFn& prepare) {
  if (config.warmups < 0 || config.repetitions < 1) {
    throw std::invalid_argument("runBenchmark: warmups >= 0 and repetitions >= 1 required");
  }
  std::optional<BenchOutcome> expected;
  auto checkOutcome = [&expected, &name](const BenchOutcome& outcome) {
    if (!expected.has_value()) {
      expected = outcome;
    } else if (!(outcome == *expected)) {
      throw std::runtime_error("benchmark '" + name + "': repetition outcome mismatch");
    }
  };

  for (int i = 0; i < config.warmups; ++i) {
    auto run = prepare();
    checkOutcome(run->runMeasured());
  }
  std::vector<double> durations;
  durations.reserve(static_cast<std::size_t>(config.repetitions));
  for (int i = 0; i < config.repetitions; ++i) {
    auto run = prepare();
    const auto start = std::chrono::steady_clock::now();
    const BenchOutcome outcome = run->runMeasured();
    const auto stop = std::chrono::steady_clock::now();
    checkOutcome(outcome);
    durations.push_back(static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()));
  }

  BenchResult result;
  result.name = name;
  result.identity = identity;
  result.eventsPerRep = eventsPerRep;
  result.outcome = *expected;
  result.stats = computeStats(std::move(durations));
  result.warmups = config.warmups;
  result.repetitions = config.repetitions;
  return result;
}

Metadata benchmarkMetadata() {
  Metadata metadata;
  metadata.version = std::string(kVersion);
  metadata.commit = TICKFORGE_BENCH_COMMIT;
  metadata.compiler = TICKFORGE_BENCH_COMPILER;
  metadata.buildType = TICKFORGE_BENCH_BUILD_TYPE;
  return metadata;
}

void printBenchUsage(std::ostream& out) {
  out << "Usage: tickforge_bench [--workload NAME] [--size N] [--seed S]\n"
         "                       [--warmups W] [--repetitions R] [--format text|csv]\n"
         "       tickforge_bench --help\n"
         "\n"
         "Workloads: all, add-heavy, cancel-heavy, modify-heavy, match-heavy,\n"
         "           mixed, parse-only, parse-replay, latency-overhead\n"
         "Options:\n"
         "  --size N         Workload size parameter (default 100000)\n"
         "  --seed S         Workload seed (default 1901)\n"
         "  --warmups W      Untimed warm-up repetitions (default 2)\n"
         "  --repetitions R  Measured repetitions (default 7)\n"
         "  --format FMT     text (default) or csv\n";
}

namespace {

bool parseUnsigned(const std::string& text, std::uint64_t& value) {
  const auto* first = text.data();
  const auto* last = first + text.size();
  const auto parsed = std::from_chars(first, last, value);
  return parsed.ec == std::errc{} && parsed.ptr == last;
}

} // namespace

std::optional<BenchOptions> parseBenchArgs(const std::vector<std::string>& args) {
  BenchOptions options;
  bool workloadSeen = false;
  bool sizeSeen = false;
  bool seedSeen = false;
  bool warmupsSeen = false;
  bool repetitionsSeen = false;
  bool formatSeen = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    auto fail = [&arg](const std::string& message) {
      std::cerr << "error: " << message << ": '" << arg << "'\n";
      printBenchUsage(std::cerr);
      return std::nullopt;
    };
    auto takeValue = [&](std::string& value) -> bool {
      if (i + 1 >= args.size() || args[i + 1].starts_with("--")) {
        return false;
      }
      value = args[++i];
      return !value.empty();
    };
    auto takeCount = [&](const char* what, int& target, bool& seen) -> std::optional<bool> {
      if (seen) {
        return fail("duplicate option");
      }
      seen = true;
      std::string value;
      if (!takeValue(value)) {
        return fail(std::string("missing or empty value for ") + what);
      }
      std::uint64_t parsed = 0;
      if (!parseUnsigned(value, parsed) || parsed > 1000000) {
        return fail(std::string("invalid value for ") + what);
      }
      target = static_cast<int>(parsed);
      return true;
    };
    if (arg == "--help") {
      options.help = true;
    } else if (arg == "--workload") {
      if (workloadSeen) {
        return fail("duplicate option");
      }
      workloadSeen = true;
      if (!takeValue(options.workload)) {
        return fail("missing or empty value for --workload");
      }
    } else if (arg == "--size") {
      if (sizeSeen) {
        return fail("duplicate option");
      }
      sizeSeen = true;
      std::string value;
      if (!takeValue(value)) {
        return fail("missing or empty value for --size");
      }
      std::uint64_t parsed = 0;
      if (!parseUnsigned(value, parsed) || parsed == 0) {
        return fail("invalid value for --size (expected a positive integer)");
      }
      options.size = static_cast<std::size_t>(parsed);
    } else if (arg == "--seed") {
      if (seedSeen) {
        return fail("duplicate option");
      }
      seedSeen = true;
      std::string value;
      if (!takeValue(value)) {
        return fail("missing or empty value for --seed");
      }
      if (!parseUnsigned(value, options.seed)) {
        return fail("invalid value for --seed");
      }
    } else if (arg == "--warmups") {
      const auto ok = takeCount("--warmups", options.warmups, warmupsSeen);
      if (!ok.has_value()) {
        return std::nullopt;
      }
    } else if (arg == "--repetitions") {
      const auto ok = takeCount("--repetitions", options.repetitions, repetitionsSeen);
      if (!ok.has_value()) {
        return std::nullopt;
      }
      if (options.repetitions < 1) {
        return fail("--repetitions must be at least 1");
      }
    } else if (arg == "--format") {
      if (formatSeen) {
        return fail("duplicate option");
      }
      formatSeen = true;
      std::string value;
      if (!takeValue(value)) {
        return fail("missing or empty value for --format");
      }
      if (value == "csv") {
        options.csv = true;
      } else if (value != "text") {
        return fail("invalid format (expected text or csv)");
      }
    } else {
      return fail("unknown argument");
    }
  }
  return options;
}

void printMetadata(std::ostream& out, const Metadata& metadata) {
  out << "TickForge benchmarks v" << metadata.version << "\n";
  out << "Commit: " << metadata.commit << "\n";
  out << "Compiler: " << metadata.compiler << "\n";
  out << "Build type: " << metadata.buildType << "\n";
}

void printTextResult(std::ostream& out, const BenchResult& result) {
  const double medianSec = result.stats.medianNs / 1e9;
  const double eventsPerSec =
      medianSec > 0 ? static_cast<double>(result.eventsPerRep) / medianSec : 0;
  const double nsPerEvent = result.eventsPerRep > 0
                                ? result.stats.medianNs / static_cast<double>(result.eventsPerRep)
                                : 0;
  out << "\n[" << result.name << "] " << result.identity << "\n";
  out << "  events/rep: " << result.eventsPerRep << "  fills: " << result.outcome.fills
      << "  resting: " << result.outcome.resting << "\n";
  out << "  warmups: " << result.warmups << "  repetitions: " << result.repetitions << "\n";
  out << "  elapsed ms (min/median/mean/max/stddev): " << result.stats.minNs / 1e6 << " / "
      << result.stats.medianNs / 1e6 << " / " << result.stats.meanNs / 1e6 << " / "
      << result.stats.maxNs / 1e6 << " / " << result.stats.stddevNs / 1e6 << "\n";
  out << "  throughput (median): " << eventsPerSec << " events/s";
  if (result.outcome.fills > 0 && medianSec > 0) {
    out << "  fills/s: " << static_cast<double>(result.outcome.fills) / medianSec;
  }
  out << "\n";
  out << "  avg time/event (median): " << nsPerEvent << " ns\n";
}

namespace {

std::string csvEscape(const std::string& value) {
  if (value.find_first_of(",\"\n") == std::string::npos) {
    return value;
  }
  std::string out = "\"";
  for (const char c : value) {
    if (c == '"') {
      out += "\"\"";
    } else {
      out += c;
    }
  }
  out += "\"";
  return out;
}

} // namespace

std::string csvHeader() {
  return "name,identity,events,fills,resting,warmups,repetitions,median_ns,mean_ns,min_ns,max_ns,"
         "stddev_ns,median_events_per_sec,version,commit,compiler,build_type";
}

std::string csvRow(const BenchResult& result, const Metadata& metadata) {
  const double medianSec = result.stats.medianNs / 1e9;
  const double eventsPerSec =
      medianSec > 0 ? static_cast<double>(result.eventsPerRep) / medianSec : 0;
  std::string row = csvEscape(result.name) + "," + csvEscape(result.identity) + "," +
                    std::to_string(result.eventsPerRep) + "," +
                    std::to_string(result.outcome.fills) + "," +
                    std::to_string(result.outcome.resting) + "," + std::to_string(result.warmups) +
                    "," + std::to_string(result.repetitions) + "," +
                    std::to_string(static_cast<std::int64_t>(result.stats.medianNs)) + "," +
                    std::to_string(static_cast<std::int64_t>(result.stats.meanNs)) + "," +
                    std::to_string(static_cast<std::int64_t>(result.stats.minNs)) + "," +
                    std::to_string(static_cast<std::int64_t>(result.stats.maxNs)) + "," +
                    std::to_string(static_cast<std::int64_t>(result.stats.stddevNs)) + "," +
                    std::to_string(static_cast<std::int64_t>(eventsPerSec)) + "," +
                    csvEscape(metadata.version) + "," + csvEscape(metadata.commit) + "," +
                    csvEscape(metadata.compiler) + "," + csvEscape(metadata.buildType);
  return row;
}

} // namespace tickforge::bench
