#include "fixtures/gif_fixture.h"
#include "incremental/gif_decoder.h"
#include "support/decode_harness.h"
#include "support/scaled_decode_expectations.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

const IncrementalDecoderFactory kGifFactory = create_incremental_gif_decoder;

void require_animated_gif_decode() {
  const auto fixture = make_animated_gif_fixture();
  const auto trace = decode_in_chunks(
      kGifFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 7);

  require_condition(trace.result == IncrementalBackendResult::Complete,
                    "animated GIF must complete incrementally");
  require_condition(trace.metadata.has_value() && trace.metadata->isAnimated &&
                        trace.metadata->hasAlpha,
                    "animated GIF metadata must advertise animation and alpha");
  require_condition(trace.completionInfo.has_value() &&
                        trace.completionInfo->frameCount ==
                            static_cast<int32_t>(fixture.expectedFrames.size()) &&
                        trace.completionInfo->loopCount == fixture.loopCount,
                    "animated GIF must preserve frame count and loop count");
  require_condition(trace.animationFrames.size() == fixture.expectedFrames.size(),
                    "animated GIF must publish every display frame");
  require_condition(
      std::any_of(trace.animationFrames.begin(), trace.animationFrames.end(),
                  [](const auto& frame) { return !frame.receivedAtEndOfInput; }),
      "animated GIF must publish a frame before end of input");

  for (size_t index = 0; index < trace.animationFrames.size(); ++index) {
    const auto& actual = trace.animationFrames[index];
    require_condition(actual.index == static_cast<int32_t>(index),
                      "animated GIF frame indices must be consecutive");
    require_condition(actual.durationMillis == fixture.durationsMillis[index],
                      "animated GIF must preserve frame durations");
    require_condition(actual.blendOperation == IncrementalBlendOperationNative::Over,
                      "animated GIF frames must use source-over blending");
    require_condition(
        maximum_channel_error(actual.rgba, fixture.expectedFrames[index]) == 0,
        "animated GIF must compose transparency and disposal correctly");
  }
}

void require_truncated_gif_fails() {
  const auto fixture = make_animated_gif_fixture();
  std::vector<uint8_t> truncated(fixture.encoded.begin(),
                                 fixture.encoded.end() - 1);
  bool threw = false;
  try {
    decode_in_chunks(kGifFactory,
                     full_size_options(fixture.width, fixture.height), truncated,
                     7);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require_condition(threw, "truncated GIF must fail at end of input");
}

void require_scaled_gif_decode() {
  // The fixture's frames update sub-rectangles at odd offsets and use both
  // background and previous disposal, so scaled frames must follow the
  // composed canvas rather than the rectangles an encoder chose.
  const auto fixture = make_animated_gif_fixture();
  require_scaled_frames_are_area_averages(kGifFactory, fixture.encoded,
                                          fixture.width, fixture.height,
                                          fixture.width * 3 / 4, 7,
                                          "scaled GIF");
}

} // namespace

void run_gif_decoder_tests() {
  require_animated_gif_decode();
  require_scaled_gif_decode();
  require_truncated_gif_fails();
}
