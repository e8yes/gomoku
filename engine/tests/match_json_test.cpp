#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

#include "match_json.h"

TEST(MatchJsonTest, ParsesJsonRpcStateEvent) {
  const JsonValue message = ParseJson(
      R"({"jsonrpc":"2.0","method":"your_turn","params":{"game_id":"g123","deadline_ms":15000,"state":{"board_size":15,"moves":[0,113,225],"legal_actions":[1,2,3],"cells":[0,1,2]}}})");

  EXPECT_EQ(message.At("jsonrpc").AsString(), "2.0");
  EXPECT_EQ(message.At("method").AsString(), "your_turn");
  const auto& params = message.At("params").AsObject();
  EXPECT_EQ(params.at("game_id").AsString(), "g123");
  EXPECT_EQ(params.at("deadline_ms").AsInt(), 15000);
  const auto& moves = params.at("state").At("moves").AsArray();
  ASSERT_EQ(moves.size(), 3u);
  EXPECT_EQ(moves[0].AsInt(), 0);
  EXPECT_EQ(moves[2].AsInt(), 225);
}

TEST(MatchJsonTest, RoundTripsEscapesAndNestedValues) {
  JsonValue::Object object;
  object.emplace("name", JsonValue("A\"B\nC"));
  object.emplace("ok", JsonValue(true));
  object.emplace("count", JsonValue(static_cast<std::int64_t>(42)));
  object.emplace("empty", JsonValue(JsonValue::Array{}));

  const JsonValue decoded = ParseJson(SerializeJson(JsonValue(object)));
  EXPECT_EQ(decoded.At("name").AsString(), "A\"B\nC");
  EXPECT_TRUE(decoded.At("ok").AsBool());
  EXPECT_EQ(decoded.At("count").AsInt(), 42);
  EXPECT_TRUE(decoded.At("empty").AsArray().empty());
}

TEST(MatchJsonTest, RejectsDuplicateKeysAndTrailingData) {
  EXPECT_THROW(ParseJson(R"({"x":1,"x":2})"), std::invalid_argument);
  EXPECT_THROW(ParseJson(R"({"x":1} trailing)"), std::invalid_argument);
}

