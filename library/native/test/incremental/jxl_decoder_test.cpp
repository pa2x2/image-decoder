#include "fixtures/jxl_fixture.h"
#include "incremental/jxl_decoder.h"
#include "support/decode_harness.h"

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <malloc.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

const IncrementalDecoderFactory kJxlFactory = create_incremental_jxl_decoder;

void require_truncated_jxl_fails(const EncodedImageFixture& fixture) {
  const size_t truncatedSize = fixture.encoded.size() * 3 / 4;
  std::vector<uint8_t> truncated(fixture.encoded.begin(),
                                 fixture.encoded.begin() + truncatedSize);
  bool threw = false;
  try {
    decode_in_chunks(kJxlFactory,
                     full_size_options(fixture.width, fixture.height),
                     truncated, 509);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require_condition(threw, "truncated JXL must fail at end of input");
}

void require_lossless_still_decode() {
  const auto fixture = make_jxl_still_fixture(false);
  const auto trace = decode_in_chunks(
      kJxlFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 509);
  require_valid_completed_trace(trace, fixture.width, fixture.height);
  require_condition(maximum_channel_error(trace.stillUpdates.back().rgba,
                                          fixture.expectedRgba) <= 1,
                    "lossless incremental JXL must preserve RGBA pixels");
  require_truncated_jxl_fails(fixture);
}

void require_progressive_still_decode() {
  const auto fixture = make_jxl_still_fixture(true);
  const auto trace = decode_in_chunks(
      kJxlFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 257);
  require_valid_completed_trace(trace, fixture.width, fixture.height);
  const auto preview =
      std::find_if(trace.stillUpdates.begin(), trace.stillUpdates.end(),
                   [](const CapturedStillUpdate& update) {
                     return !update.receivedAtEndOfInput &&
                            populated_pixel_count(update) > 0;
                   });
  require_condition(preview != trace.stillUpdates.end(),
                    "progressive JXL must publish pixels before end of input");
  require_condition(
      maximum_channel_error(trace.stillUpdates.back().rgba,
                            fixture.expectedRgba) <= 1,
      "incremental JXL final pixels must match conventional JXL decode");
}

void require_scaled_still_decode() {
  const auto fixture = make_jxl_still_fixture(false);
  const IncrementalDecodeOptionsNative options{
      .preferredOutputWidth = 48,
      .maximumBitmapPixels = 48 * 96,
      .displayProfile = {},
  };
  const auto trace =
      decode_in_chunks(kJxlFactory, options, fixture.encoded, 1021);
  require_condition(trace.result == IncrementalBackendResult::Complete,
                    "scaled JXL must complete");
  require_condition(trace.metadata.has_value(),
                    "scaled JXL must publish metadata");
  require_condition(trace.metadata->outputWidth == 48 &&
                        trace.metadata->outputHeight == 96,
                    "scaled JXL must respect preferred output dimensions");
  require_condition(!trace.stillUpdates.empty() &&
                        trace.stillUpdates.back().rgba.size() == 48 * 96 * 4,
                    "scaled JXL must stay inside its pixel budget");
}

void require_animated_decode() {
  const auto fixture = make_animated_jxl_fixture();
  const auto trace = decode_in_chunks(
      kJxlFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 311);
  require_condition(trace.result == IncrementalBackendResult::Complete,
                    "animated JXL must complete");
  require_condition(trace.completeUpdates == 1,
                    "animated JXL must publish one completion update");
  require_condition(trace.metadata.has_value() && trace.metadata->isAnimated,
                    "animated JXL metadata must advertise animation");
  require_condition(trace.completionInfo.has_value(),
                    "animated JXL completion must include final metadata");
  require_condition(trace.completionInfo->frameCount ==
                        static_cast<int32_t>(fixture.expectedFrames.size()),
                    "animated JXL must report its decoded frame count");
  require_condition(trace.completionInfo->loopCount == fixture.loopCount,
                    "animated JXL must preserve its loop count");
  require_condition(trace.animationFrames.size() ==
                        fixture.expectedFrames.size(),
                    "animated JXL must publish every display frame");

  for (size_t index = 0; index < trace.animationFrames.size(); ++index) {
    const auto& actual = trace.animationFrames[index];
    require_condition(actual.index == static_cast<int32_t>(index),
                      "animated JXL frame indices must be ordered");
    require_condition(actual.durationMillis == fixture.durationsMillis[index],
                      "animated JXL frame durations must be preserved");
    require_condition(actual.blendOperation ==
                              IncrementalBlendOperationNative::Source &&
                          actual.disposalOperation ==
                              IncrementalDisposalOperationNative::None,
                      "coalesced JXL frames must replace the full canvas");
    require_condition(actual.width == fixture.width &&
                          actual.height == fixture.height,
                      "animated JXL frame dimensions must match metadata");
    require_condition(
        maximum_channel_error(actual.rgba, fixture.expectedFrames[index]) <= 1,
        "lossless animated JXL frames must preserve RGBA pixels");
  }
}

size_t current_resident_bytes() {
  std::ifstream statm("/proc/self/statm");
  size_t totalPages = 0;
  size_t residentPages = 0;
  statm >> totalPages >> residentPages;
  require_condition(statm.good() || statm.eof(),
                    "benchmark must read current resident memory");
  static_cast<void>(totalPages);
  const long pageSize = sysconf(_SC_PAGESIZE);
  require_condition(pageSize > 0, "benchmark must read the system page size");
  return residentPages * static_cast<size_t>(pageSize);
}

size_t peak_resident_bytes() {
  rusage usage{};
  require_condition(getrusage(RUSAGE_SELF, &usage) == 0,
                    "benchmark must read peak resident memory");
  return static_cast<size_t>(usage.ru_maxrss) * 1024;
}

void decode_for_benchmark(const EncodedImageFixture& fixture) {
  auto decoder = kJxlFactory(full_size_options(fixture.width, fixture.height));
  size_t offset = 0;
  size_t updates = 0;
  IncrementalBackendResult result = IncrementalBackendResult::Accepted;
  const IncrementalUpdateSink sink = [&](IncrementalUpdate&& update) {
    if (update.type == IncrementalUpdateType::StillImageAvailable) {
      require_condition(update.snapshot != nullptr &&
                            update.snapshot->rgba != nullptr,
                        "benchmark JXL update must contain pixels");
      ++updates;
    }
  };
  while (offset < fixture.encoded.size()) {
    constexpr size_t chunkSize = 16 * 1024;
    const size_t count = std::min(chunkSize, fixture.encoded.size() - offset);
    const bool endOfInput = offset + count == fixture.encoded.size();
    result = decoder->append(fixture.encoded.data() + offset, count, endOfInput,
                             sink);
    offset += count;
  }
  require_condition(result == IncrementalBackendResult::Complete && updates > 0,
                    "benchmark JXL decode must complete with pixels");
}

} // namespace

void run_jxl_decoder_tests() {
  require_lossless_still_decode();
  require_progressive_still_decode();
  require_scaled_still_decode();
  require_animated_decode();
}

void run_jxl_decoder_benchmark() {
  const auto fixture = make_large_jxl_benchmark_fixture();
  malloc_trim(0);
  const pid_t child = fork();
  require_condition(child >= 0, "benchmark process must fork");
  if (child > 0) {
    int status = 0;
    require_condition(waitpid(child, &status, 0) == child &&
                          WIFEXITED(status) && WEXITSTATUS(status) == 0,
                      "isolated JXL benchmark must complete");
    return;
  }

  const size_t baselineResidentBytes = current_resident_bytes();
  constexpr size_t iterations = 3;
  const std::clock_t cpuStart = std::clock();
  const auto wallStart = std::chrono::steady_clock::now();
  for (size_t iteration = 0; iteration < iterations; ++iteration) {
    decode_for_benchmark(fixture);
  }
  const auto wallEnd = std::chrono::steady_clock::now();
  const std::clock_t cpuEnd = std::clock();
  const size_t finalResidentBytes = current_resident_bytes();
  const size_t peakResidentBytes = peak_resident_bytes();
  const double cpuMillis =
      static_cast<double>(cpuEnd - cpuStart) * 1000.0 / CLOCKS_PER_SEC;
  const double wallMillis =
      std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();
  constexpr double bytesPerMebibyte = 1024.0 * 1024.0;

  std::cout << std::fixed << std::setprecision(2)
            << "JXL_BENCHMARK dimensions=" << fixture.width << 'x'
            << fixture.height << " encoded_bytes=" << fixture.encoded.size()
            << " iterations=" << iterations << " cpu_ms_total=" << cpuMillis
            << " cpu_ms_per_decode=" << cpuMillis / iterations
            << " wall_ms_total=" << wallMillis
            << " wall_ms_per_decode=" << wallMillis / iterations
            << " baseline_rss_mib=" << baselineResidentBytes / bytesPerMebibyte
            << " final_rss_mib=" << finalResidentBytes / bytesPerMebibyte
            << " peak_rss_mib=" << peakResidentBytes / bytesPerMebibyte
            << " peak_growth_mib="
            << (peakResidentBytes > baselineResidentBytes
                    ? (peakResidentBytes - baselineResidentBytes) /
                          bytesPerMebibyte
                    : 0.0)
            << '\n';
  std::cout.flush();
  _exit(0);
}
