#include "vcf_solver.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "board.h"

namespace {

constexpr int kDirections[4][2] = {
    {1, 0},
    {0, 1},
    {1, 1},
    {1, -1},
};

void Put(Position* position, Stone stone,
         std::initializer_list<std::pair<int, int>> coordinates) {
  for (const auto& [x, y] : coordinates) {
    ASSERT_TRUE(position->Set(x, y, stone));
  }
}

int CountInDirection(const Position& position, int x, int y, int dx, int dy,
                     Stone stone) {
  int count = 0;
  x += dx;
  y += dy;
  while (position.At(x, y) == stone) {
    ++count;
    x += dx;
    y += dy;
  }
  return count;
}

bool HasExactFive(const Position& position, Stone stone) {
  for (int action = 0; action < kVcfNumCells; ++action) {
    if (position.At(action) != stone) continue;

    const int x = VcfActionX(action);
    const int y = VcfActionY(action);
    for (const auto& [dx, dy] : kDirections) {
      if (position.At(x - dx, y - dy) == stone) continue;

      int run_length = 1;
      int next_x = x + dx;
      int next_y = y + dy;
      while (position.At(next_x, next_y) == stone) {
        ++run_length;
        next_x += dx;
        next_y += dy;
      }
      if (run_length == 5) return true;
    }
  }
  return false;
}

int CountWinningMoves(const Position& position, Stone stone) {
  int count = 0;
  for (int action = 0; action < kVcfNumCells; ++action) {
    if (!position.IsEmpty(action)) continue;

    const int x = VcfActionX(action);
    const int y = VcfActionY(action);
    for (const auto& [dx, dy] : kDirections) {
      const int run_length = 1 +
                             CountInDirection(position, x, y, dx, dy, stone) +
                             CountInDirection(position, x, y, -dx, -dy, stone);
      if (run_length == 5) {
        ++count;
        break;
      }
    }
  }
  return count;
}

void ExpectLegalForcingLine(Position position, const std::vector<int>& line) {
  ASSERT_FALSE(line.empty());

  Stone to_move = position.current_player;
  for (size_t ply = 0; ply < line.size(); ++ply) {
    SCOPED_TRACE("line ply " + std::to_string(ply));
    ASSERT_TRUE(position.IsEmpty(line[ply]));
    ASSERT_TRUE(position.Set(line[ply], to_move));

    if (ply % 2 == 0) {
      if (HasExactFive(position, to_move)) {
        ASSERT_EQ(ply + 1, line.size());
      } else {
        EXPECT_GT(CountWinningMoves(position, to_move), 0);
        EXPECT_EQ(CountWinningMoves(position, OtherStone(to_move)), 0);
      }
    }
    to_move = OtherStone(to_move);
  }
}

Board MakeStandardBlackToMove() {
  Board board;
  board.Apply(Action::FromXY(0, 0).id);
  board.Apply(Action::FromXY(1, 0).id);
  board.Apply(Action::FromXY(2, 0).id);
  board.Apply(Action::kSwap2ChooseBlack);

  board.Apply(Action::FromXY(10, 10).id);
  board.Apply(Action::FromXY(0, 1).id);
  board.Apply(Action::FromXY(12, 10).id);
  board.Apply(Action::FromXY(1, 1).id);
  board.Apply(Action::FromXY(14, 10).id);
  board.Apply(Action::FromXY(2, 1).id);
  board.Apply(Action::FromXY(10, 12).id);
  board.Apply(Action::FromXY(3, 1).id);
  board.Apply(Action::FromXY(12, 12).id);
  return board;
}

}  // namespace

struct ImmediateWinCase {
  const char* name;
  std::vector<std::pair<int, int>> black;
  std::vector<std::pair<int, int>> white;
  std::pair<int, int> expected_action;
};

class ImmediateWinTest : public testing::TestWithParam<ImmediateWinCase> {};

TEST_P(ImmediateWinTest, FindsWinInEveryDirection) {
  const ImmediateWinCase& test_case = GetParam();
  Position position(Stone::kBlack);
  for (const auto& [x, y] : test_case.black) {
    ASSERT_TRUE(position.Set(x, y, Stone::kBlack));
  }
  for (const auto& [x, y] : test_case.white) {
    ASSERT_TRUE(position.Set(x, y, Stone::kWhite));
  }

  const std::vector<int> line = SolveVCF(position);

  ASSERT_EQ(line.size(), 1u);
  EXPECT_EQ(line.front(), VcfActionFromXY(test_case.expected_action.first,
                                          test_case.expected_action.second));
  ExpectLegalForcingLine(position, line);
}

INSTANTIATE_TEST_SUITE_P(
    Directions, ImmediateWinTest,
    testing::Values(
        ImmediateWinCase{
            "horizontal", {{3, 7}, {4, 7}, {5, 7}, {6, 7}}, {{2, 7}}, {7, 7}},
        ImmediateWinCase{
            "vertical", {{7, 3}, {7, 4}, {7, 5}, {7, 6}}, {{7, 2}}, {7, 7}},
        ImmediateWinCase{"diagonal_down",
                         {{3, 3}, {4, 4}, {5, 5}, {6, 6}},
                         {{2, 2}},
                         {7, 7}},
        ImmediateWinCase{
            "diagonal_up", {{3, 6}, {4, 5}, {5, 4}, {6, 3}}, {{2, 7}}, {7, 2}}),
    [](const testing::TestParamInfo<ImmediateWinCase>& info) {
      return info.param.name;
    });

TEST(VcfSolverTest, FindsDoubleFour) {
  Position position(Stone::kBlack);

  // (6, 7) makes both a horizontal and a vertical four. White can block at
  // most one of (7, 7) and (6, 6), so this is a one-move VCF.
  Put(&position, Stone::kBlack,
      {{3, 7}, {4, 7}, {5, 7}, {6, 3}, {6, 4}, {6, 5}});
  Put(&position, Stone::kWhite, {{2, 7}, {6, 2}});

  const std::vector<int> line = SolveVCF(position);

  ASSERT_EQ(line.size(), 1u);
  EXPECT_EQ(line.front(), VcfActionFromXY(6, 7));
  ExpectLegalForcingLine(position, line);
}

TEST(VcfSolverTest, FindsAndReplaysMultiStepVcf) {
  Position position(Stone::kBlack);
  Put(&position, Stone::kBlack, {{3, 7}, {4, 7}, {5, 7}, {6, 4}, {6, 5}});
  Put(&position, Stone::kWhite, {{2, 7}});

  const Position original = position;
  const std::vector<int> line = SolveVCF(position);

  ASSERT_EQ(line.size(), 3u);
  EXPECT_EQ(line[0], VcfActionFromXY(6, 7));
  EXPECT_EQ(line[1], VcfActionFromXY(7, 7));
  EXPECT_EQ(line[2], VcfActionFromXY(6, 6));
  ExpectLegalForcingLine(position, line);
  EXPECT_EQ(position.cells, original.cells);
  EXPECT_EQ(position.current_player, original.current_player);
}

TEST(VcfSolverTest, RejectsPositionWithoutAForcingMove) {
  Position position(Stone::kBlack);
  Put(&position, Stone::kBlack, {{3, 7}, {4, 7}, {5, 7}});
  Put(&position, Stone::kWhite, {{2, 7}, {6, 7}});

  EXPECT_TRUE(SolveVCF(position).empty());
}

TEST(VcfSolverTest, RejectsEveryBranchWhenDefenderCanCounterWin) {
  Position position(Stone::kBlack);
  Put(&position, Stone::kBlack, {{3, 7}, {4, 7}, {5, 7}});
  Put(&position, Stone::kWhite, {{3, 10}, {4, 10}, {5, 10}, {6, 10}});

  // White has two immediate wins. Black's forcing moves cannot be accepted
  // merely because they also create a threat; White may win instead.
  EXPECT_EQ(CountWinningMoves(position, Stone::kWhite), 2);
  EXPECT_TRUE(SolveVCF(position).empty());
}

TEST(VcfSolverTest, DoesNotTreatAnOverlineAsAWin) {
  Position position(Stone::kBlack);
  Put(&position, Stone::kBlack, {{0, 7}, {1, 7}, {2, 7}, {3, 7}, {5, 7}});

  // Filling (4, 7) creates six in a row, which is illegal under the engine's
  // exact-five rule. There is no other forcing continuation here.
  EXPECT_TRUE(SolveVCF(position).empty());
}

TEST(VcfSolverTest, RejectsAlreadyTerminalPositionsForEitherStone) {
  Position black_win(Stone::kBlack);
  Put(&black_win, Stone::kBlack, {{3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}});
  EXPECT_TRUE(SolveVCF(black_win).empty());

  Position white_win(Stone::kBlack);
  Put(&white_win, Stone::kWhite, {{3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}});
  EXPECT_TRUE(SolveVCF(white_win).empty());
}

TEST(VcfSolverTest, ChoosesTheFirstWinningActionDeterministically) {
  Position position(Stone::kBlack);
  Put(&position, Stone::kBlack, {{3, 7}, {4, 7}, {5, 7}, {6, 7}});

  const std::vector<int> first = SolveVCF(position);
  const std::vector<int> second = SolveVCF(position);

  ASSERT_EQ(first, second);
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first.front(), VcfActionFromXY(2, 7));
}

TEST(VcfSolverTest, RejectsInvalidCurrentPlayer) {
  Position position(Stone::kEmpty);
  EXPECT_TRUE(SolveVCF(position).empty());
}

TEST(VcfSolverTest, BoardAdapterOnlySolvesLiveStandardBoards) {
  Board initial_board;
  EXPECT_TRUE(SolveVCF(initial_board).empty());

  Board standard_board = MakeStandardBlackToMove();
  const std::vector<int> line = SolveVCF(standard_board);
  ASSERT_EQ(line.size(), 1u);
  EXPECT_EQ(line.front(), Action::FromXY(4, 1).id);

  standard_board.Apply(line.front());
  ASSERT_TRUE(standard_board.IsTerminal());
  EXPECT_TRUE(SolveVCF(standard_board).empty());
}

TEST(VcfSolverTest, FreeFunctionIsSafeToCallConcurrently) {
  Position position(Stone::kBlack);
  Put(&position, Stone::kBlack, {{3, 7}, {4, 7}, {5, 7}, {6, 7}});
  Put(&position, Stone::kWhite, {{2, 7}});

  constexpr int kWorkers = 8;
  std::vector<std::vector<int>> lines(kWorkers);
  std::vector<std::thread> workers;
  workers.reserve(kWorkers);

  for (int i = 0; i < kWorkers; ++i) {
    workers.emplace_back(
        [&position, &lines, i] { lines[i] = SolveVCF(position); });
  }
  for (auto& worker : workers) worker.join();

  for (const auto& line : lines) {
    ASSERT_EQ(line.size(), 1u);
    EXPECT_EQ(line.front(), VcfActionFromXY(7, 7));
  }
}
