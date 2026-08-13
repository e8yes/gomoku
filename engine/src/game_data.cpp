#include "game_data.h"

#include <cstring>
#include <ostream>

namespace {

bool FeatureBit(const Board& board, int channel, int x, int y) {
  const Player actual_current = board.stone_to_place();
  const Player current =
      actual_current == Player::kNone ? Player::kBlack : actual_current;
  const Player opponent =
      current == Player::kBlack ? Player::kWhite : Player::kBlack;

  const Player cell = board.cell(x, y);
  if (channel == 0) return cell == current;
  if (channel == 1) return cell == opponent;
  if (channel == 2) return actual_current == Player::kBlack;
  if (channel == 3) return actual_current == Player::kWhite;
  if (channel == 4) return board.phase() == Phase::kPlaceInitialThree;
  if (channel == 5) return board.phase() == Phase::kSwap2Decision;
  if (channel == 6) return board.phase() == Phase::kSwap2PlaceTwo;
  if (channel == 7) return board.phase() == Phase::kChooseColor;
  if (channel == 8) return board.phase() == Phase::kStandard;
  return false;
}

void WriteUint16(std::ostream& output, std::uint16_t value) {
  const char bytes[2] = {static_cast<char>(value & 0xffu),
                         static_cast<char>((value >> 8) & 0xffu)};
  output.write(bytes, sizeof(bytes));
}

}  // namespace

std::array<std::uint8_t, kPackedStateBytes> PackBoard(const Board& board) {
  std::array<std::uint8_t, kPackedStateBytes> packed{};

  constexpr int kFeatureBits = 9 * Board::kNumCells;
  for (int bit = 0; bit < kFeatureBits; ++bit) {
    const int channel = bit / Board::kNumCells;
    const int cell = bit % Board::kNumCells;
    const int x = cell % Board::kSize;
    const int y = cell / Board::kSize;
    if (FeatureBit(board, channel, x, y)) {
      packed[bit / 8] |= static_cast<std::uint8_t>(1u << (7 - bit % 8));
    }
  }
  return packed;
}

std::uint16_t FloatToHalf(float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));

  const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
  const std::uint32_t exponent = (bits >> 23) & 0xffu;
  std::uint32_t mantissa = bits & 0x007fffffu;

  if (exponent == 0xffu) {
    // Preserve infinities and turn float NaNs into a quiet half NaN.
    if (mantissa == 0) return static_cast<std::uint16_t>(sign | 0x7c00u);
    return static_cast<std::uint16_t>(sign | 0x7e00u);
  }

  int half_exponent = static_cast<int>(exponent) - 127 + 15;
  if (half_exponent >= 31) {
    return static_cast<std::uint16_t>(sign | 0x7c00u);
  }

  if (half_exponent <= 0) {
    if (half_exponent < -10) return sign;

    // Add the implicit leading one and round the subnormal mantissa.
    mantissa |= 0x00800000u;
    const int shift = 14 - half_exponent;
    std::uint32_t half_mantissa = mantissa >> shift;
    const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
    const std::uint32_t halfway = 1u << (shift - 1);
    if (remainder > halfway ||
        (remainder == halfway && (half_mantissa & 1u) != 0)) {
      ++half_mantissa;
    }
    return static_cast<std::uint16_t>(sign | half_mantissa);
  }

  std::uint32_t half_mantissa = mantissa >> 13;
  const std::uint32_t remainder = mantissa & 0x1fffu;
  if (remainder > 0x1000u ||
      (remainder == 0x1000u && (half_mantissa & 1u) != 0)) {
    ++half_mantissa;
    if (half_mantissa == 0x400u) {
      half_mantissa = 0;
      ++half_exponent;
      if (half_exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
      }
    }
  }

  return static_cast<std::uint16_t>(
      sign | (static_cast<std::uint16_t>(half_exponent) << 10) |
      static_cast<std::uint16_t>(half_mantissa));
}

bool WriteExample(std::ostream& output, const Board& board,
                  const std::vector<float>& policy, float value) {
  if (policy.size() != kPolicySize) return false;

  const auto packed = PackBoard(board);
  output.write(reinterpret_cast<const char*>(packed.data()),
               static_cast<std::streamsize>(packed.size()));
  for (float probability : policy)
    WriteUint16(output, FloatToHalf(probability));
  WriteUint16(output, FloatToHalf(value));
  return static_cast<bool>(output);
}
