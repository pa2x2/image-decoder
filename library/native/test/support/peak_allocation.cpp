#include "support/peak_allocation.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

// Keeps the size of each allocation in front of it, aligned like malloc.
constexpr size_t kHeaderSize = alignof(std::max_align_t);

std::atomic<size_t> liveBytes{0};
std::atomic<size_t> peakBytes{0};

void record_allocation(size_t size) {
  const size_t live = liveBytes.fetch_add(size) + size;
  size_t peak = peakBytes.load();
  while (live > peak && !peakBytes.compare_exchange_weak(peak, live)) {
  }
}

} // namespace

void* operator new(size_t size) {
  void* block = std::malloc(kHeaderSize + size);
  if (block == nullptr) {
    throw std::bad_alloc();
  }
  *static_cast<size_t*>(block) = size;
  record_allocation(size);
  return static_cast<char*>(block) + kHeaderSize;
}

void operator delete(void* pointer) noexcept {
  if (pointer == nullptr) {
    return;
  }
  void* block = static_cast<char*>(pointer) - kHeaderSize;
  liveBytes.fetch_sub(*static_cast<size_t*>(block));
  std::free(block);
}

void operator delete(void* pointer, size_t) noexcept {
  operator delete(pointer);
}

size_t peak_allocated_bytes_during(const std::function<void()>& action) {
  const size_t before = liveBytes.load();
  peakBytes.store(before);
  action();
  return peakBytes.load() - before;
}
