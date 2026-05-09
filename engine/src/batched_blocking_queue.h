#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <vector>

// BatchedBlockingQueue<T> is a thread-safe, multi-producer single-consumer
// queue that delivers items in batches.
//
// PopBatch() blocks until at least one item is available, then waits up to
// max_wait_us for additional concurrent pushes to join the same batch before
// draining up to max_batch_size items atomically.
//
// This coalescing window is the core mechanism that allows many concurrent
// callers to share a single expensive operation (e.g. a GPU forward pass).
//
// Shutdown():
//   Signals the consumer to stop after draining remaining items. Each
//   PopBatch() call continues to return available items until the queue is
//   empty, then returns an empty vector to indicate termination.
template <typename T>
class BatchedBlockingQueue {
 public:
  // max_wait_us must be > 0 — a zero or negative wait defeats batching.
  BatchedBlockingQueue(int max_batch_size,
                       std::chrono::microseconds max_wait_us)
      : max_batch_size_(max_batch_size), max_wait_us_(max_wait_us) {
    if (max_wait_us.count() <= 0) {
      throw std::invalid_argument(
          "BatchedBlockingQueue: max_wait_us must be > 0");
    }
  }

  // Non-copyable, non-movable.
  BatchedBlockingQueue(const BatchedBlockingQueue&) = delete;
  BatchedBlockingQueue& operator=(const BatchedBlockingQueue&) = delete;

  // Thread-safe push. May be called concurrently from any number of threads.
  void Push(T item) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(std::move(item));
    }
    cv_.notify_one();
  }

  // Blocks until at least one item is available or the queue is shut down.
  //   - If items are available: waits up to max_wait_us_ for more items to
  //     arrive (the coalescing window), then atomically drains up to
  //     max_batch_size_ items and returns them.
  //   - If shut down and empty: returns an empty vector (termination signal).
  std::vector<T> PopBatch() {
    std::unique_lock<std::mutex> lock(mutex_);

    // Phase 1: block until at least one item arrives or shutdown.
    cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
    if (shutdown_ && queue_.empty()) return {};

    // Phase 2: coalescing window — wait for concurrent pushes to join.
    cv_.wait_for(lock, max_wait_us_, [this] {
      return static_cast<int>(queue_.size()) >= max_batch_size_ || shutdown_;
    });

    std::vector<T> batch;
    while (!queue_.empty() &&
           static_cast<int>(batch.size()) < max_batch_size_) {
      batch.push_back(std::move(queue_.front()));
      queue_.pop();
    }
    return batch;
  }

  // Signals shutdown. In-flight and already-queued items will still be
  // returned by pending/future PopBatch() calls until the queue is empty.
  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      shutdown_ = true;
    }
    cv_.notify_all();
  }

 private:
  const int max_batch_size_;
  const std::chrono::microseconds max_wait_us_;

  std::queue<T> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool shutdown_ = false;
};
