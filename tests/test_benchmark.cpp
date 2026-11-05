// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Benchmark infrastructure tests: statistics known answers, the
// warm-up/repetition protocol, argument parsing, deterministic
// workload construction and verification, and pipeline consistency.
// No test asserts a wall-clock duration or a throughput figure.

#include "bench/harness.hpp"
#include "bench/workloads.hpp"
#include "tickdots/event/event.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace tickdots::bench {
namespace {

// ---------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------

TEST(BenchStats, SingleValue) {
  const Stats stats = computeStats({42.0});
  EXPECT_DOUBLE_EQ(stats.minNs, 42.0);
  EXPECT_DOUBLE_EQ(stats.medianNs, 42.0);
  EXPECT_DOUBLE_EQ(stats.meanNs, 42.0);
  EXPECT_DOUBLE_EQ(stats.maxNs, 42.0);
  EXPECT_DOUBLE_EQ(stats.stddevNs, 0.0);
}

TEST(BenchStats, KnownAnswersEvenCount) {
  const Stats stats = computeStats({4.0, 1.0, 3.0, 2.0});
  EXPECT_DOUBLE_EQ(stats.minNs, 1.0);
  EXPECT_DOUBLE_EQ(stats.maxNs, 4.0);
  EXPECT_DOUBLE_EQ(stats.medianNs, 2.5);
  EXPECT_DOUBLE_EQ(stats.meanNs, 2.5);
  EXPECT_NEAR(stats.stddevNs, 1.2909944487358056, 1e-12); // sqrt(5/3), sample stddev
}

TEST(BenchStats, KnownAnswersOddCount) {
  const Stats stats = computeStats({30.0, 10.0, 20.0});
  EXPECT_DOUBLE_EQ(stats.medianNs, 20.0);
  EXPECT_DOUBLE_EQ(stats.meanNs, 20.0);
  EXPECT_DOUBLE_EQ(stats.stddevNs, 10.0);
}

TEST(BenchStats, EmptyInputThrows) {
  EXPECT_THROW((void)computeStats({}), std::invalid_argument);
}

// ---------------------------------------------------------------------
// Runner protocol
// ---------------------------------------------------------------------

class FixedRun final : public PreparedRun {
public:
  explicit FixedRun(BenchOutcome outcome) : outcome_(outcome) {}
  BenchOutcome runMeasured() override {
    return outcome_;
  }

private:
  BenchOutcome outcome_;
};

BenchOutcome sampleOutcome() {
  BenchOutcome outcome;
  outcome.eventsProcessed = 10;
  outcome.fills = 3;
  outcome.resting = 7;
  return outcome;
}

TEST(BenchRunner, WarmupsAreNotMeasured) {
  int prepared = 0;
  const PrepareFn prepare = [&prepared] {
    ++prepared;
    return std::make_unique<FixedRun>(sampleOutcome());
  };
  const BenchResult result = runBenchmark("fake", "fake(size=1)", 10, BenchConfig{3, 5}, prepare);
  EXPECT_EQ(prepared, 8); // every repetition prepares fresh state
  EXPECT_EQ(result.warmups, 3);
  EXPECT_EQ(result.repetitions, 5);
  EXPECT_EQ(result.outcome, sampleOutcome());
  EXPECT_GE(result.stats.medianNs, 0.0);
}

TEST(BenchRunner, OutcomeMismatchThrows) {
  int calls = 0;
  const PrepareFn prepare = [&calls] {
    ++calls;
    BenchOutcome outcome = sampleOutcome();
    outcome.fills = static_cast<std::uint64_t>(calls); // differs per repetition
    return std::make_unique<FixedRun>(outcome);
  };
  EXPECT_THROW(runBenchmark("fake", "fake(size=1)", 10, BenchConfig{0, 3}, prepare),
               std::runtime_error);
}

TEST(BenchRunner, InvalidConfigThrows) {
  const PrepareFn prepare = [] { return std::make_unique<FixedRun>(sampleOutcome()); };
  EXPECT_THROW(runBenchmark("fake", "id", 10, BenchConfig{0, 0}, prepare), std::invalid_argument);
  EXPECT_THROW(runBenchmark("fake", "id", 10, BenchConfig{-1, 1}, prepare), std::invalid_argument);
}

// ---------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------

TEST(BenchArgs, Defaults) {
  const auto options = parseBenchArgs({});
  ASSERT_TRUE(options.has_value());
  EXPECT_EQ(options->workload, "all");
  EXPECT_EQ(options->size, 100000U);
  EXPECT_EQ(options->seed, 1901U);
  EXPECT_EQ(options->warmups, 2);
  EXPECT_EQ(options->repetitions, 7);
  EXPECT_FALSE(options->csv);
}

TEST(BenchArgs, ValidCombinations) {
  const auto options = parseBenchArgs({"--workload",
                                       "mixed",
                                       "--size",
                                       "500",
                                       "--seed",
                                       "7",
                                       "--warmups",
                                       "0",
                                       "--repetitions",
                                       "1",
                                       "--format",
                                       "csv"});
  ASSERT_TRUE(options.has_value());
  EXPECT_EQ(options->workload, "mixed");
  EXPECT_EQ(options->size, 500U);
  EXPECT_EQ(options->repetitions, 1);
  EXPECT_TRUE(options->csv);
}

TEST(BenchArgs, InvalidInputsRejected) {
  for (const std::vector<std::string>& args :
       {std::vector<std::string>{"--repetitions", "0"},
        std::vector<std::string>{"--size", "0"},
        std::vector<std::string>{"--size", "-5"},
        std::vector<std::string>{"--warmups", "x"},
        std::vector<std::string>{"--seed", "99999999999999999999999"},
        std::vector<std::string>{"--format", "json"},
        std::vector<std::string>{"--size", "10", "--size", "11"},
        std::vector<std::string>{"--size"},
        std::vector<std::string>{"--bogus"}}) {
    EXPECT_FALSE(parseBenchArgs(args).has_value()) << "args should be rejected";
  }
}

// ---------------------------------------------------------------------
// Output format
// ---------------------------------------------------------------------

std::size_t countCsvFields(const std::string& line) {
  std::size_t fields = 1;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"') {
      quoted = !quoted;
    } else if (line[i] == ',' && !quoted) {
      ++fields;
    }
  }
  return fields;
}

TEST(BenchOutput, CsvHeaderAndRowHaveSameFieldCount) {
  BenchResult result;
  result.name = "mixed";
  result.identity = "mixed(size=10,seed=1,weights=55/20/25)"; // contains commas
  result.eventsPerRep = 10;
  const Metadata metadata{"0.2.0", "abc123", "GNU 13.3.0", "Release"};
  EXPECT_EQ(countCsvFields(csvHeader()), 17U);
  EXPECT_EQ(countCsvFields(csvRow(result, metadata)), countCsvFields(csvHeader()));
}

// ---------------------------------------------------------------------
// Workloads
// ---------------------------------------------------------------------

BenchOutcome runOnce(const Workload& workload, bool latency = false) {
  auto prepared = enginePrepare(workload, latency)();
  return prepared->runMeasured();
}

TEST(BenchWorkloads, AddHeavyShape) {
  const Workload workload = makeWorkload("add-heavy", 100, 7);
  EXPECT_TRUE(workload.setup.empty());
  ASSERT_EQ(workload.events.size(), 100U);
  for (const Event& event : workload.events) {
    EXPECT_EQ(event.type, EventType::NewOrder);
  }
  const BenchOutcome outcome = runOnce(workload);
  EXPECT_EQ(outcome.fills, 0U);
  EXPECT_EQ(outcome.resting, 100U);
}

TEST(BenchWorkloads, CancelHeavyShape) {
  const Workload workload = makeWorkload("cancel-heavy", 100, 7);
  EXPECT_EQ(workload.setup.size(), 100U);
  ASSERT_EQ(workload.events.size(), 90U);
  for (const Event& event : workload.events) {
    EXPECT_EQ(event.type, EventType::CancelOrder);
  }
  const BenchOutcome outcome = runOnce(workload);
  EXPECT_EQ(outcome.fills, 0U);
  EXPECT_EQ(outcome.resting, 10U);
}

TEST(BenchWorkloads, ModifyHeavyShape) {
  const Workload workload = makeWorkload("modify-heavy", 100, 7);
  ASSERT_EQ(workload.events.size(), 100U);
  for (const Event& event : workload.events) {
    EXPECT_EQ(event.type, EventType::ModifyOrder);
  }
  const BenchOutcome outcome = runOnce(workload);
  EXPECT_EQ(outcome.fills, 0U);
  EXPECT_EQ(outcome.resting, 100U);
}

TEST(BenchWorkloads, MatchHeavyShape) {
  const Workload workload = makeWorkload("match-heavy", 100, 7);
  EXPECT_EQ(workload.setup.size(), 100U);
  ASSERT_EQ(workload.events.size(), 2U);
  const BenchOutcome outcome = runOnce(workload);
  EXPECT_EQ(outcome.fills, 101U); // 1 partial + 100 sweeping fills
  EXPECT_EQ(outcome.resting, 0U);
}

TEST(BenchWorkloads, MixedIsDeterministicAndIdentified) {
  const Workload a = makeWorkload("mixed", 1000, 1901);
  const Workload b = makeWorkload("mixed", 1000, 1901);
  EXPECT_EQ(a.identity, b.identity);
  ASSERT_EQ(a.events.size(), 1000U);
  EXPECT_EQ(a.events, b.events);
  std::uint64_t sequence = 0;
  for (const Event& event : a.events) {
    EXPECT_EQ(event.sequence.value(), sequence++);
  }
  const BenchOutcome outcome = runOnce(a);
  EXPECT_EQ(outcome.eventsProcessed, 1000U);
}

TEST(BenchWorkloads, UnknownAndEmptyRejected) {
  EXPECT_THROW(makeWorkload("nope", 10, 1), std::invalid_argument);
  EXPECT_THROW(makeWorkload("mixed", 0, 1), std::invalid_argument);
}

TEST(BenchWorkloads, LatencyObserverSamplesEveryFill) {
  const Workload workload = makeWorkload("latency-overhead", 2000, 1901);
  const BenchOutcome plain = runOnce(workload, false);
  const BenchOutcome observed = runOnce(workload, true);
  EXPECT_EQ(observed.fills, plain.fills);
  EXPECT_EQ(observed.resting, plain.resting);
  EXPECT_EQ(observed.samples, observed.fills);
  EXPECT_GT(observed.samples, 0U);
}

TEST(BenchWorkloads, CsvRoundTripMatchesDirectReplay) {
  const Workload workload = makeWorkload("parse-replay", 2000, 1901);
  const std::string csvText = toCsvText(workload.events);
  auto parsed = parsePrepare(csvText, "AAPL")();
  EXPECT_EQ(parsed->runMeasured().eventsProcessed, 2000U);
  auto replayed = parseReplayPrepare(csvText, "AAPL")();
  const BenchOutcome viaPipeline = replayed->runMeasured();
  const BenchOutcome direct = runOnce(workload, false);
  EXPECT_EQ(viaPipeline.fills, direct.fills);
  EXPECT_EQ(viaPipeline.resting, direct.resting);
}

} // namespace
} // namespace tickdots::bench
