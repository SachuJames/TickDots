// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Deterministic benchmark workloads implementation. See
// workloads.hpp for the construction contract.

#include "bench/workloads.hpp"

#include "tests/support/event_generator.hpp"
#include "tickforge/book/order_book.hpp"
#include "tickforge/core/splitmix64.hpp"
#include "tickforge/event/event.hpp"
#include "tickforge/market_data/csv_parser.hpp"
#include "tickforge/matching/matching_engine.hpp"
#include "tickforge/model/latency_model.hpp"
#include "tickforge/model/latency_observer.hpp"
#include "tickforge/replay/replay.hpp"
#include "tickforge/replay/session_config.hpp"

#include <sstream>
#include <stdexcept>

namespace tickforge::bench {
namespace {

const std::vector<std::string>& names() {
  static const std::vector<std::string> all = {"add-heavy",
                                               "cancel-heavy",
                                               "modify-heavy",
                                               "match-heavy",
                                               "mixed",
                                               "parse-only",
                                               "parse-replay",
                                               "latency-overhead"};
  return all;
}

// Fills the fields every benchmark event shares. Sequences are dense
// from 0 across set-up and measured events; timestamps start at
// 1,000,000 and increase by one per event.
struct EventStamp {
  std::int64_t timestamp = 1000000;
  std::uint64_t sequence = 0;

  void applyTo(Event& event) {
    event.timestamp = Timestamp{timestamp++};
    event.sequence = Sequence{sequence++};
    event.instrument = Instrument{"AAPL"};
  }
};

Event newOrder(
    EventStamp& stamp, std::uint64_t id, Side side, std::int64_t price, std::int64_t quantity) {
  Event event;
  stamp.applyTo(event);
  event.type = EventType::NewOrder;
  event.orderId = OrderId{id};
  event.side = side;
  event.price = Price{price};
  event.quantity = Quantity{quantity};
  return event;
}

// Non-crossing resting orders: bids in [80, 99], asks in [101, 120],
// so the book only grows. Returns the orders for later reference.
std::vector<Event>
restingOrders(EventStamp& stamp, SplitMix64& rng, std::size_t count, std::uint64_t firstId) {
  std::vector<Event> orders;
  orders.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const bool bid = rng.nextBool();
    const std::int64_t price = bid ? rng.nextRange(80, 99) : rng.nextRange(101, 120);
    orders.push_back(
        newOrder(stamp, firstId + i, bid ? Side::Bid : Side::Ask, price, rng.nextRange(1, 50)));
  }
  return orders;
}

std::string identityFor(const std::string& name,
                        std::size_t size,
                        std::uint64_t seed,
                        const std::string& extra = "") {
  std::string identity = name + "(size=" + std::to_string(size) + ",seed=" + std::to_string(seed);
  if (!extra.empty()) {
    identity += "," + extra;
  }
  identity += ")";
  return identity;
}

void verifyEvents(const Workload& workload) {
  auto check = [&workload](const Event& event) {
    if (const auto error = validateEvent(event); error != EventValidationError::Ok) {
      throw std::invalid_argument(
          "workload '" + workload.name +
          "': constructed event failed validation: " + std::string(toString(error)));
    }
  };
  for (const Event& event : workload.setup) {
    check(event);
  }
  for (const Event& event : workload.events) {
    check(event);
  }
}

Workload addHeavy(std::size_t size, std::uint64_t seed) {
  Workload workload;
  workload.name = "add-heavy";
  workload.seed = seed;
  EventStamp stamp;
  SplitMix64 rng(seed);
  workload.events = restingOrders(stamp, rng, size, 1);
  workload.identity = identityFor(workload.name, size, seed, "prices=80-99/101-120,qty=1-50");
  workload.expectedFills = 0;
  workload.expectedResting = size;
  return workload;
}

Workload cancelHeavy(std::size_t size, std::uint64_t seed) {
  Workload workload;
  workload.name = "cancel-heavy";
  workload.seed = seed;
  EventStamp stamp;
  SplitMix64 rng(seed);
  workload.setup = restingOrders(stamp, rng, size, 1);
  // Cancel every order except each tenth id: 90% of the population.
  for (std::size_t i = 0; i < size; ++i) {
    const std::uint64_t id = i + 1;
    if (id % 10 == 0) {
      continue;
    }
    Event event;
    stamp.applyTo(event);
    event.type = EventType::CancelOrder;
    event.orderId = OrderId{id};
    event.side = workload.setup[i].side;
    workload.events.push_back(event);
  }
  workload.identity = identityFor(workload.name, size, seed, "cancel=90%");
  workload.expectedFills = 0;
  workload.expectedResting = size / 10;
  return workload;
}

Workload modifyHeavy(std::size_t size, std::uint64_t seed) {
  Workload workload;
  workload.name = "modify-heavy";
  workload.seed = seed;
  EventStamp stamp;
  SplitMix64 rng(seed);
  workload.setup = restingOrders(stamp, rng, size, 1);
  for (std::size_t i = 0; i < size; ++i) {
    const Event& order = workload.setup[i];
    Event event;
    stamp.applyTo(event);
    event.type = EventType::ModifyOrder;
    event.orderId = order.orderId;
    event.side = order.side;
    if (i % 2 == 0 && order.quantity.lots() > 1) {
      // Quantity decrease at the same price: priority is kept.
      event.price = order.price;
      event.quantity = Quantity{order.quantity.lots() / 2};
    } else {
      // Price change by one tick away from the spread: cancel/replace.
      // Stepping away keeps every order non-crossing by construction
      // (bids stay <= 99, asks stay >= 101), so modifies never fill.
      const std::int64_t step = order.side == Side::Bid ? -1 : 1;
      event.price = Price{order.price.ticks() + step};
      event.quantity = order.quantity;
    }
    workload.events.push_back(event);
  }
  workload.identity = identityFor(workload.name, size, seed, "half qty-down, half price-step");
  workload.expectedFills = 0;
  workload.expectedResting = size;
  return workload;
}

Workload matchHeavy(std::size_t size, std::uint64_t seed) {
  Workload workload;
  workload.name = "match-heavy";
  workload.seed = seed;
  EventStamp stamp;
  SplitMix64 rng(seed);
  std::int64_t totalQuantity = 0;
  for (std::size_t i = 0; i < size; ++i) {
    const std::int64_t quantity = rng.nextRange(5, 15);
    totalQuantity += quantity;
    workload.setup.push_back(
        newOrder(stamp, i + 1, Side::Ask, 100 + static_cast<std::int64_t>(i % 5), quantity));
  }
  // A small bid partially fills the best ask; a second bid priced
  // through every level sweeps the remainder of the book.
  workload.events.push_back(newOrder(stamp, size + 1, Side::Bid, 104, 3));
  workload.events.push_back(newOrder(stamp, size + 2, Side::Bid, 104, totalQuantity - 3));
  workload.identity = identityFor(workload.name, size, seed, "levels=100-104,qty=5-15");
  workload.expectedFills = static_cast<std::uint64_t>(size) + 1;
  workload.expectedResting = 0;
  return workload;
}

Workload mixed(std::size_t size, std::uint64_t seed) {
  test::GeneratorConfig config;
  config.seed = seed;
  config.eventCount = size;
  config.minPrice = 80;
  config.maxPrice = 120;
  config.minQuantity = 1;
  config.maxQuantity = 50;
  config.newWeight = 55;
  config.cancelWeight = 20;
  config.modifyWeight = 25;
  Workload workload;
  workload.name = "mixed";
  workload.seed = seed;
  workload.events = test::EventGenerator(config).generate();
  workload.identity =
      identityFor(workload.name, size, seed, "gen=1,prices=80-120,qty=1-50,weights=55/20/25");
  return workload;
}

} // namespace

const std::vector<std::string>& workloadNames() {
  return names();
}

Workload makeWorkload(const std::string& name, std::size_t size, std::uint64_t seed) {
  if (size == 0) {
    throw std::invalid_argument("workload size must be positive");
  }
  Workload workload;
  if (name == "add-heavy") {
    workload = addHeavy(size, seed);
  } else if (name == "cancel-heavy") {
    workload = cancelHeavy(size, seed);
  } else if (name == "modify-heavy") {
    workload = modifyHeavy(size, seed);
  } else if (name == "match-heavy") {
    workload = matchHeavy(size, seed);
  } else if (name == "mixed" || name == "parse-only" || name == "parse-replay" ||
             name == "latency-overhead") {
    workload = mixed(size, seed);
    workload.name = name;
    workload.identity =
        identityFor(name, size, seed, "gen=1,prices=80-120,qty=1-50,weights=55/20/25");
    if (name == "latency-overhead") {
      workload.identity = identityFor(
          name, size, seed, "gen=1,prices=80-120,qty=1-50,weights=55/20/25,latency=1000-1999");
    }
  } else {
    throw std::invalid_argument("unknown workload: " + name);
  }
  verifyEvents(workload);
  return workload;
}

std::string toCsvText(const std::vector<Event>& events) {
  std::ostringstream out;
  out << "timestamp,event_type,order_id,side,price,quantity\n";
  for (const Event& event : events) {
    out << event.timestamp.count() << ',';
    switch (event.type) {
    case EventType::NewOrder:
      out << 'N';
      break;
    case EventType::ModifyOrder:
      out << 'M';
      break;
    case EventType::CancelOrder:
      out << 'C';
      break;
    }
    out << ',' << event.orderId.value() << ',' << (event.side == Side::Bid ? 'B' : 'A') << ',';
    if (event.type != EventType::CancelOrder && event.price.ticks() != 0) {
      out << event.price.ticks();
    }
    out << ',';
    if (event.type != EventType::CancelOrder && event.quantity.lots() != 0) {
      out << event.quantity.lots();
    }
    out << '\n';
  }
  return out.str();
}

namespace {

class EngineRun final : public PreparedRun {
public:
  EngineRun(const Workload& workload, bool latency) : workload_(workload), engine_(book_) {
    EventProcessor* processor = &engine_;
    if (latency) {
      SessionConfig config;
      config.instrument = "AAPL";
      config.prngSeed = workload.seed;
      config.latencyMinNs = 1000;
      config.latencyMaxNs = 1999;
      auto model = LatencyModel::fromConfig(config);
      if (!model.has_value()) {
        throw std::runtime_error("latency model not enabled by benchmark configuration");
      }
      observer_.emplace(engine_, *model);
      processor = &*observer_;
    }
    processor_ = processor;
    for (const Event& event : workload_.setup) {
      if (!processor_->onEvent(event)) {
        throw std::runtime_error("benchmark set-up event was rejected");
      }
    }
  }

  BenchOutcome runMeasured() override {
    std::uint64_t fills = 0;
    for (const Event& event : workload_.events) {
      if (!processor_->onEvent(event)) {
        throw std::runtime_error("benchmark event was rejected");
      }
      fills += engine_.fills().size();
    }
    BenchOutcome outcome;
    outcome.eventsProcessed = workload_.events.size();
    outcome.fills = fills;
    outcome.samples = observer_.has_value() ? observer_->model().sampleCount() : 0;
    outcome.resting = book_.orderCount();
    return outcome;
  }

private:
  const Workload& workload_;
  OrderBook book_;
  MatchingEngine engine_;
  std::optional<LatencyObserver> observer_;
  EventProcessor* processor_ = nullptr;
};

class ParseRun final : public PreparedRun {
public:
  ParseRun(std::string csvText, std::string instrument)
      : csvText_(std::move(csvText)), instrument_(std::move(instrument)) {}

  BenchOutcome runMeasured() override {
    std::istringstream input(csvText_);
    const CsvParser parser{instrument_};
    const StreamResult parsed = parser.parseStream(input);
    if (parsed.error != ParseError::Ok) {
      throw std::runtime_error("pipeline benchmark: CSV parse failed");
    }
    BenchOutcome outcome;
    outcome.eventsProcessed = parsed.events.size();
    return outcome;
  }

private:
  std::string csvText_;
  std::string instrument_;
};

class FillCounter final : public EventProcessor {
public:
  explicit FillCounter(MatchingEngine& engine) : engine_(engine) {}

  bool onEvent(const Event& event) override {
    const bool applied = engine_.onEvent(event);
    fills_ += engine_.fills().size();
    return applied;
  }

  [[nodiscard]] std::uint64_t fills() const noexcept {
    return fills_;
  }

private:
  MatchingEngine& engine_;
  std::uint64_t fills_ = 0;
};

class ParseReplayRun final : public PreparedRun {
public:
  ParseReplayRun(std::string csvText, std::string instrument)
      : csvText_(std::move(csvText)), instrument_(std::move(instrument)) {}

  BenchOutcome runMeasured() override {
    std::istringstream input(csvText_);
    const CsvParser parser{instrument_};
    const StreamResult parsed = parser.parseStream(input);
    if (parsed.error != ParseError::Ok) {
      throw std::runtime_error("pipeline benchmark: CSV parse failed");
    }
    SessionConfig config;
    config.instrument = instrument_;
    OrderBook book;
    MatchingEngine engine(book);
    FillCounter counter(engine);
    const ReplayResult result = replayEvents(parsed.events, counter, config);
    if (!result.ok()) {
      throw std::runtime_error("pipeline benchmark: replay failed");
    }
    BenchOutcome outcome;
    outcome.eventsProcessed = parsed.events.size() - result.skippedCount;
    outcome.fills = counter.fills();
    outcome.resting = book.orderCount();
    return outcome;
  }

private:
  std::string csvText_;
  std::string instrument_;
};

} // namespace

PrepareFn enginePrepare(const Workload& workload, bool latency) {
  return [&workload, latency] { return std::make_unique<EngineRun>(workload, latency); };
}

PrepareFn parsePrepare(std::string csvText, std::string instrument) {
  return [csvText = std::move(csvText), instrument = std::move(instrument)] {
    return std::make_unique<ParseRun>(csvText, instrument);
  };
}

PrepareFn parseReplayPrepare(std::string csvText, std::string instrument) {
  return [csvText = std::move(csvText), instrument = std::move(instrument)] {
    return std::make_unique<ParseReplayRun>(csvText, instrument);
  };
}

} // namespace tickforge::bench
