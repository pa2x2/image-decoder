#include "fixtures/webp_fixture.h"
#include "incremental/webp_decoder.h"
#include "support/decode_harness.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

const IncrementalDecoderFactory kWebpFactory = create_incremental_webp_decoder;

void require_useful_precompletion_update(const IncrementalDecodeTrace& trace) {
  const auto update =
      std::find_if(trace.stillUpdates.begin(), trace.stillUpdates.end(),
                   [](const CapturedStillUpdate& candidate) {
                     return !candidate.receivedAtEndOfInput &&
                            populated_pixel_count(candidate) > 0;
                   });
  require_condition(update != trace.stillUpdates.end(),
                    "WebP must publish useful pixels before end of input");
}

void require_truncated_webp_fails(const EncodedImageFixture& fixture) {
  const size_t truncatedSize = fixture.encoded.size() * 3 / 4;
  std::vector<uint8_t> truncated(fixture.encoded.begin(),
                                 fixture.encoded.begin() + truncatedSize);
  bool threw = false;
  try {
    decode_in_chunks(kWebpFactory,
                     full_size_options(fixture.width, fixture.height),
                     truncated, 487);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require_condition(threw, "truncated WebP must fail at end of input");
}

void run_webp_case(bool lossless) {
  const auto fixture = make_webp_fixture(lossless);
  const auto trace = decode_in_chunks(
      kWebpFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 487);
  require_valid_completed_trace(trace, fixture.width, fixture.height);
  require_useful_precompletion_update(trace);

  const auto& finalPixels = trace.stillUpdates.back().rgba;
  require_condition(maximum_channel_error(finalPixels, fixture.expectedRgba) <=
                        1,
                    "incremental WebP must match conventional WebP decode");
  require_truncated_webp_fails(fixture);
}

void require_animated_webp_decode() {
  const auto fixture = make_animated_webp_fixture();
  const auto trace = decode_in_chunks(
      kWebpFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 487);
  require_condition(trace.result == IncrementalBackendResult::Complete,
                    "animated WebP must complete incrementally");
  require_condition(trace.metadata.has_value() && trace.metadata->isAnimated,
                    "animated WebP metadata must advertise animation");
  require_condition(trace.completionInfo.has_value() &&
                        trace.completionInfo->loopCount == fixture.loopCount,
                    "animated WebP must preserve its loop count");
  require_condition(trace.animationFrames.size() ==
                        fixture.expectedFrames.size(),
                    "animated WebP must publish every display frame");
  for (size_t index = 0; index < trace.animationFrames.size(); ++index) {
    require_condition(trace.animationFrames[index].durationMillis ==
                          fixture.durationsMillis[index],
                      "animated WebP must preserve frame durations");
    require_condition(maximum_channel_error(trace.animationFrames[index].rgba,
                                            fixture.expectedFrames[index]) <= 1,
                      "lossless animated WebP must preserve RGBA pixels");
  }
}

} // namespace

void run_webp_decoder_tests() {
  run_webp_case(false);
  run_webp_case(true);
  require_animated_webp_decode();
}
