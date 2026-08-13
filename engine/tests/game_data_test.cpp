#include "game_data.h"

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

#include "board.h"

namespace {

bool GetPackedBit(const std::array<std::uint8_t, kPackedStateBytes>& packed,
                  int bit) {
  return (packed[bit / 8] & (1u << (7 - bit % 8))) != 0;
}

int FeatureBitIndex(int channel, int x, int y) {
  return channel * Board::kNumCells + y * Board::kSize + x;
}

std::uint16_t ReadLittleEndian16(const std::string& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<unsigned char>(bytes[offset]) |
      (static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset + 1]))
       << 8));
}

}  // namespace

TEST(GameDataTest, PacksTheSameFeaturePlaneOrderAsTheModel) {
  Board board;
  const auto initial = PackBoard(board);

  for (int y = 0; y < Board::kSize; ++y) {
    for (int x = 0; x < Board::kSize; ++x) {
      EXPECT_FALSE(GetPackedBit(initial, FeatureBitIndex(0, x, y)));
      EXPECT_FALSE(GetPackedBit(initial, FeatureBitIndex(1, x, y)));
      EXPECT_TRUE(GetPackedBit(initial, FeatureBitIndex(2, x, y)));
      EXPECT_FALSE(GetPackedBit(initial, FeatureBitIndex(3, x, y)));
      EXPECT_TRUE(GetPackedBit(initial, FeatureBitIndex(4, x, y)));
      EXPECT_FALSE(GetPackedBit(initial, FeatureBitIndex(5, x, y)));
      EXPECT_FALSE(GetPackedBit(initial, FeatureBitIndex(6, x, y)));
      EXPECT_FALSE(GetPackedBit(initial, FeatureBitIndex(7, x, y)));
      EXPECT_FALSE(GetPackedBit(initial, FeatureBitIndex(8, x, y)));
    }
  }

  board.Apply(Action::FromXY(7, 7).id);
  const auto after_move = PackBoard(board);
  EXPECT_FALSE(GetPackedBit(after_move, FeatureBitIndex(0, 7, 7)));
  EXPECT_TRUE(GetPackedBit(after_move, FeatureBitIndex(1, 7, 7)));
  EXPECT_FALSE(GetPackedBit(after_move, FeatureBitIndex(2, 7, 7)));
  EXPECT_TRUE(GetPackedBit(after_move, FeatureBitIndex(3, 7, 7)));
}

TEST(GameDataTest, WritesTheDocumented716ByteRecord) {
  Board board;
  std::vector<float> policy(Board::kNumActions, 0.0f);
  policy[0] = 1.0f;
  policy[Action::kChooseBlack] = 0.5f;

  std::ostringstream output(std::ios::binary);
  ASSERT_TRUE(WriteExample(output, board, policy, -1.0f));

  const std::string bytes = output.str();
  ASSERT_EQ(bytes.size(), kExampleBytes);
  EXPECT_EQ(ReadLittleEndian16(bytes, kPackedStateBytes), FloatToHalf(1.0f));
  EXPECT_EQ(
      ReadLittleEndian16(bytes, kPackedStateBytes + Action::kChooseBlack *
                                                        sizeof(std::uint16_t)),
      FloatToHalf(0.5f));
  EXPECT_EQ(
      ReadLittleEndian16(bytes, kPackedStateBytes +
                                    Board::kNumActions * sizeof(std::uint16_t)),
      FloatToHalf(-1.0f));
}

TEST(GameDataTest, RejectsPoliciesWithTheWrongActionCount) {
  std::ostringstream output(std::ios::binary);
  EXPECT_FALSE(WriteExample(output, Board{}, {}, 0.0f));
  EXPECT_TRUE(output.str().empty());
}
