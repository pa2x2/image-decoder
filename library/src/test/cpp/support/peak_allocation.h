#ifndef IMAGEDECODER_TEST_PEAK_ALLOCATION_H
#define IMAGEDECODER_TEST_PEAK_ALLOCATION_H

#include <cstddef>
#include <functional>

// The most bytes allocated through operator new and not yet freed at any point
// while `action` runs, beyond what was allocated before it started. Allocations
// made directly with malloc, such as those of the codec libraries, are not
// counted.
size_t peak_allocated_bytes_during(const std::function<void()>& action);

#endif // IMAGEDECODER_TEST_PEAK_ALLOCATION_H
