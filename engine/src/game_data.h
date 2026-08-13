#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <vector>

#include "board.h"

constexpr std::size_t kPackedStateBytes = 254;
constexpr std::size_t kPolicySize = Board::kNumActions;
constexpr std::size_t kExampleBytes = kPackedStateBytes +
                                      kPolicySize * sizeof(std::uint16_t) +
                                      sizeof(std::uint16_t);

// Packs the same 9x15x15 feature planes used by NeuralNetEvaluator into the
// bit order consumed by numpy.packbits in cumulative_dataset.py.
std::array<std::uint8_t, kPackedStateBytes> PackBoard(const Board& board);

// Converts an IEEE-754 float32 to the little-endian bit pattern used by the
// dataset's numpy.float16 fields.
std::uint16_t FloatToHalf(float value);

// Writes one fixed-size (716 byte) training record. Returns false if the
// stream reports a write failure or if policy has the wrong size.
bool WriteExample(std::ostream& output, const Board& board,
                  const std::vector<float>& policy, float value);
