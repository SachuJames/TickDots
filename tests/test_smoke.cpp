// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Smoke test: proves the test binary builds, links against the
// TickDots core headers, runs under CTest, and sees the project version.

#include "tickdots/version.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

TEST(SmokeTest, EngineInitializes) {
  // Reaching this point proves the test executable links and runs
  // against the TickDots core headers.
  SUCCEED();
}

TEST(VersionTest, VersionStringIsWellFormed) {
  const std::string version{tickdots::kVersion};
  EXPECT_FALSE(version.empty());
  EXPECT_EQ(version, "0.2.0");
}

TEST(VersionTest, VersionComponentsMatchString) {
  EXPECT_EQ(tickdots::kVersionMajor, 0);
  EXPECT_EQ(tickdots::kVersionMinor, 2);
  EXPECT_EQ(tickdots::kVersionPatch, 0);
}

TEST(VersionTest, ProjectNameIsTickDots) {
  EXPECT_EQ(tickdots::kProjectName, "TickDots");
}

} // namespace
