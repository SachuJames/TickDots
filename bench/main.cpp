// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// tickdots_bench: reproducible benchmark driver. Builds the
// selected deterministic workloads, verifies their declared
// outcomes on an untimed pass, then runs the warm-up and repetition
// protocol. Exit codes: 0 success, 2 invalid arguments, 1 benchmark
// failure.

#include "bench/harness.hpp"
#include "bench/workloads.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int run(const tickdots::bench::BenchOptions& options) {
  using namespace tickdots::bench;
  const Metadata metadata = benchmarkMetadata();
  const BenchConfig config{options.warmups, options.repetitions};

  std::vector<std::string> selected;
  if (options.workload == "all") {
    selected = workloadNames();
  } else {
    const auto& known = workloadNames();
    if (std::find(known.begin(), known.end(), options.workload) == known.end()) {
      std::cerr << "error: unknown workload: '" << options.workload << "'\n";
      printBenchUsage(std::cerr);
      return 2;
    }
    selected.push_back(options.workload);
  }

  if (!options.csv) {
    printMetadata(std::cout, metadata);
  } else {
    std::cout << csvHeader() << "\n";
  }

  for (const std::string& name : selected) {
    const Workload workload = makeWorkload(name, options.size, options.seed);
    PrepareFn prepare;
    std::uint64_t eventsPerRep = 0;
    if (name == "parse-only" || name == "parse-replay") {
      const std::string csvText = toCsvText(workload.events);
      eventsPerRep = workload.events.size();
      prepare = name == "parse-only" ? parsePrepare(csvText, "AAPL")
                                     : parseReplayPrepare(csvText, "AAPL");
    } else {
      eventsPerRep = workload.events.size();
      prepare = enginePrepare(workload, name == "latency-overhead");
    }

    // Verification pass (untimed): the workload must produce the
    // outcome it declares before any timing is trusted.
    {
      auto verification = prepare();
      const BenchOutcome outcome = verification->runMeasured();
      if (workload.expectedFills.has_value() && outcome.fills != *workload.expectedFills) {
        throw std::runtime_error("workload '" + name + "': expected " +
                                 std::to_string(*workload.expectedFills) + " fills, got " +
                                 std::to_string(outcome.fills));
      }
      if (workload.expectedResting.has_value() && outcome.resting != *workload.expectedResting) {
        throw std::runtime_error("workload '" + name + "': expected " +
                                 std::to_string(*workload.expectedResting) + " resting, got " +
                                 std::to_string(outcome.resting));
      }
    }

    const BenchResult result = runBenchmark(name, workload.identity, eventsPerRep, config, prepare);
    if (options.csv) {
      std::cout << csvRow(result, metadata) << "\n";
    } else {
      printTextResult(std::cout, result);
    }
  }
  return 0;
}

} // namespace

int main(int argc, char* argv[]) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  const auto options = tickdots::bench::parseBenchArgs(args);
  if (!options.has_value()) {
    return 2;
  }
  if (options->help) {
    tickdots::bench::printBenchUsage(std::cout);
    return 0;
  }
  try {
    return run(*options);
  } catch (const std::exception& e) {
    std::cerr << "error: benchmark failed: " << e.what() << "\n";
    return 1;
  }
}
