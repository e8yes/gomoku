#include "batched_blocking_queue.h"

#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(BatchedBlockingQueueTest, RejectsNonPositiveWaitUs) {
  EXPECT_THROW(
      (BatchedBlockingQueue<int>(64, std::chrono::microseconds(0))),
      std::invalid_argument);
  EXPECT_THROW(
      (BatchedBlockingQueue<int>(64, std::chrono::microseconds(-1))),
      std::invalid_argument);
}

TEST(BatchedBlockingQueueTest, ConstructsWithPositiveWaitUs) {
  EXPECT_NO_THROW(
      (BatchedBlockingQueue<int>(64, std::chrono::microseconds(1))));
}

// ---------------------------------------------------------------------------
// Single-producer basic correctness
// ---------------------------------------------------------------------------

TEST(BatchedBlockingQueueTest, SingleItemDelivered) {
  BatchedBlockingQueue<int> q(64, std::chrono::microseconds(1000));
  q.Push(42);
  auto batch = q.PopBatch();
  ASSERT_EQ(batch.size(), 1u);
  EXPECT_EQ(batch[0], 42);
}

TEST(BatchedBlockingQueueTest, BatchSizeCapped) {
  constexpr int kMax = 4;
  BatchedBlockingQueue<int> q(kMax, std::chrono::microseconds(1000));
  for (int i = 0; i < 10; ++i) q.Push(i);

  auto batch = q.PopBatch();
  EXPECT_LE(static_cast<int>(batch.size()), kMax);
  EXPECT_GE(static_cast<int>(batch.size()), 1);
}

TEST(BatchedBlockingQueueTest, AllItemsEventuallyConsumed) {
  constexpr int kItems = 20;
  BatchedBlockingQueue<int> q(4, std::chrono::microseconds(1000));
  for (int i = 0; i < kItems; ++i) q.Push(i);
  q.Shutdown();

  int total = 0;
  while (true) {
    auto batch = q.PopBatch();
    if (batch.empty()) break;
    total += static_cast<int>(batch.size());
  }
  EXPECT_EQ(total, kItems);
}

// ---------------------------------------------------------------------------
// Shutdown semantics
// ---------------------------------------------------------------------------

TEST(BatchedBlockingQueueTest, ShutdownWithEmptyQueueTerminates) {
  BatchedBlockingQueue<int> q(64, std::chrono::microseconds(1000));
  q.Shutdown();
  auto batch = q.PopBatch();
  EXPECT_TRUE(batch.empty());
}

TEST(BatchedBlockingQueueTest, ShutdownDrainsRemainingItemsFirst) {
  BatchedBlockingQueue<int> q(64, std::chrono::microseconds(1000));
  q.Push(1);
  q.Push(2);
  q.Push(3);
  q.Shutdown();

  // First PopBatch should return items, not empty.
  auto batch = q.PopBatch();
  EXPECT_FALSE(batch.empty());

  // Subsequent PopBatch calls drain until empty.
  int total = static_cast<int>(batch.size());
  while (true) {
    batch = q.PopBatch();
    if (batch.empty()) break;
    total += static_cast<int>(batch.size());
  }
  EXPECT_EQ(total, 3);
}

// ---------------------------------------------------------------------------
// Coalescing: concurrent pushes should merge into fewer PopBatch calls
// ---------------------------------------------------------------------------

TEST(BatchedBlockingQueueTest, ConcurrentPushesCoalesce) {
  constexpr int kProducers = 16;
  // Large batch and long wait window so all producers have time to enqueue
  // before the consumer wakes up.
  BatchedBlockingQueue<int> q(kProducers,
                               std::chrono::microseconds(50'000));

  std::atomic<int> pop_call_count{0};
  std::atomic<int> total_received{0};

  // Consumer thread.
  std::thread consumer([&] {
    while (true) {
      auto batch = q.PopBatch();
      if (batch.empty()) break;
      pop_call_count.fetch_add(1, std::memory_order_relaxed);
      total_received.fetch_add(static_cast<int>(batch.size()),
                               std::memory_order_relaxed);
    }
  });

  // All producers push simultaneously.
  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int i = 0; i < kProducers; ++i)
    producers.emplace_back([&q, i] { q.Push(i); });
  for (auto& t : producers) t.join();

  q.Shutdown();
  consumer.join();

  EXPECT_EQ(total_received.load(), kProducers);
  // With all pushes racing against the coalescing window, the consumer should
  // have processed them in fewer batch calls than there are producers.
  EXPECT_LT(pop_call_count.load(), kProducers);
}

// ---------------------------------------------------------------------------
// Move-only item type
// ---------------------------------------------------------------------------

TEST(BatchedBlockingQueueTest, MoveOnlyItemType) {
  BatchedBlockingQueue<std::unique_ptr<int>> q(64,
                                               std::chrono::microseconds(1000));
  q.Push(std::make_unique<int>(99));
  q.Shutdown();
  auto batch = q.PopBatch();
  ASSERT_EQ(batch.size(), 1u);
  EXPECT_EQ(*batch[0], 99);
}
