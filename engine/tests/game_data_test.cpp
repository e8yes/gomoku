#include "game_data.h"

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

#include "neural_net_evaluator.h"

namespace {

bool GetPackedBit(const std::array<std::uint8_t, kPackedStateBytes>& packed,
                  int bit) {
  return (packed[bit / 8] & (1u << (7 - bit % 8))) != 0;
}

int FeatureBitIndex(int channel, int x, int y) {
  return channel * Board::kNumCells + y * Board::kSize + x;
}

void ExpectPackedStateMatchesRuntimeTensor(const Board& board) {
  const auto packed = PackBoard(board);
  const torch::Tensor tensor =
      neural_net_evaluator_internal::BoardToTensor(board);

  ASSERT_EQ(tensor.sizes(),
            torch::IntArrayRef({9, Board::kSize, Board::kSize}));
  ASSERT_EQ(tensor.scalar_type(), torch::kFloat);
  ASSERT_TRUE(tensor.device().is_cpu());
  const auto tensor_accessor = tensor.accessor<float, 3>();
  for (int channel = 0; channel < 9; ++channel) {
    for (int y = 0; y < Board::kSize; ++y) {
      for (int x = 0; x < Board::kSize; ++x) {
        EXPECT_EQ(GetPackedBit(packed, FeatureBitIndex(channel, x, y)),
                  tensor_accessor[channel][y][x] == 1.0f)
            << "channel=" << channel << " x=" << x << " y=" << y;
      }
    }
  }
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

TEST(GameDataTest, PackedTrainingStateMatchesRuntimeTensorAcrossSwap2Phases) {
  Board board;
  ExpectPackedStateMatchesRuntimeTensor(board);

  board.Apply(Action::FromXY(7, 7).id);
  ExpectPackedStateMatchesRuntimeTensor(board);
  board.Apply(Action::FromXY(7, 8).id);
  ExpectPackedStateMatchesRuntimeTensor(board);
  board.Apply(Action::FromXY(8, 7).id);
  ExpectPackedStateMatchesRuntimeTensor(board);

  // The no-stone phases are especially important: the encoder deliberately
  // uses Black as a stable first-person fallback while exposing phase planes.
  ASSERT_EQ(board.phase(), Phase::kSwap2Decision);
  ASSERT_EQ(board.stone_to_place(), Player::kNone);
  ExpectPackedStateMatchesRuntimeTensor(board);

  board.Apply(Action::kSwap2PlaceTwo);
  ExpectPackedStateMatchesRuntimeTensor(board);
  board.Apply(Action::FromXY(6, 7).id);
  ExpectPackedStateMatchesRuntimeTensor(board);
  board.Apply(Action::FromXY(8, 8).id);
  ExpectPackedStateMatchesRuntimeTensor(board);

  ASSERT_EQ(board.phase(), Phase::kChooseColor);
  ASSERT_EQ(board.stone_to_place(), Player::kNone);
  ExpectPackedStateMatchesRuntimeTensor(board);

  board.Apply(Action::kChooseBlack);
  ASSERT_EQ(board.phase(), Phase::kStandard);
  ExpectPackedStateMatchesRuntimeTensor(board);
  board.Apply(Action::FromXY(6, 8).id);
  ExpectPackedStateMatchesRuntimeTensor(board);

  Board choose_directly;
  choose_directly.Apply(Action::FromXY(7, 7).id);
  choose_directly.Apply(Action::FromXY(7, 8).id);
  choose_directly.Apply(Action::FromXY(8, 7).id);
  choose_directly.Apply(Action::kSwap2ChooseWhite);
  ASSERT_EQ(choose_directly.phase(), Phase::kStandard);
  ExpectPackedStateMatchesRuntimeTensor(choose_directly);
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
