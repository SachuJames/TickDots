// Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
//
// Tests for the Side and EventType enums.

#include "tickdots/event/event_type.hpp"
#include "tickdots/event/side.hpp"

#include <gtest/gtest.h>
#include <string_view>

namespace {

using tickdots::EventType;
using tickdots::Side;
using tickdots::toString;

TEST(SideTest, BidAndAskAreDistinct) {
  EXPECT_NE(Side::Bid, Side::Ask);
  EXPECT_EQ(Side::Bid, Side::Bid);
  EXPECT_EQ(Side::Ask, Side::Ask);
}

TEST(SideTest, ToString) {
  EXPECT_EQ(toString(Side::Bid), std::string_view{"Bid"});
  EXPECT_EQ(toString(Side::Ask), std::string_view{"Ask"});
}

TEST(SideTest, Opposite) {
  EXPECT_EQ(tickdots::opposite(Side::Bid), Side::Ask);
  EXPECT_EQ(tickdots::opposite(Side::Ask), Side::Bid);
  EXPECT_EQ(tickdots::opposite(tickdots::opposite(Side::Bid)), Side::Bid);
}

TEST(EventTypeTest, ExactlyThreeDistinctTypes) {
  EXPECT_NE(EventType::NewOrder, EventType::ModifyOrder);
  EXPECT_NE(EventType::NewOrder, EventType::CancelOrder);
  EXPECT_NE(EventType::ModifyOrder, EventType::CancelOrder);
  EXPECT_EQ(EventType::NewOrder, EventType::NewOrder);
}

TEST(EventTypeTest, ToString) {
  EXPECT_EQ(toString(EventType::NewOrder), std::string_view{"NewOrder"});
  EXPECT_EQ(toString(EventType::ModifyOrder), std::string_view{"ModifyOrder"});
  EXPECT_EQ(toString(EventType::CancelOrder), std::string_view{"CancelOrder"});
}

} // namespace
