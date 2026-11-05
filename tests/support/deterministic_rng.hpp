// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Deterministic test RNG (test infrastructure only, never production).
//
// This is the production SplitMix64 (SPEC.md 14.1) under its test
// name: one implementation, shared by both. The generation contract
// of SPEC.md 14 (generation version 1) is defined over this exact
// algorithm and mapping, and the known-answer tests pin it.

#pragma once

#include "tickdots/core/splitmix64.hpp"

namespace tickdots::test {

using DeterministicRng = tickdots::SplitMix64;

} // namespace tickdots::test
