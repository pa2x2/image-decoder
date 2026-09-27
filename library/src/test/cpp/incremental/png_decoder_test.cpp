#include "fixtures/png_fixture.h"
#include "incremental/png_decoder.h"
#include "support/decode_harness.h"
#include "support/scaled_decode_expectations.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

const IncrementalDecoderFactory kPngFactory = create_incremental_png_decoder;

void require_useful_precompletion_update(const IncrementalDecodeTrace& trace) {
  const auto update =
      std::find_if(trace.stillUpdates.begin(), trace.stillUpdates.end(),
                   [](const CapturedStillUpdate& candidate) {
                     return !candidate.receivedAtEndOfInput &&
                            populated_pixel_count(candidate) > 0;
                   });
  require_condition(update != trace.stillUpdates.end(),
                    "PNG must publish useful pixels before end of input");
}

void require_truncated_png_fails(const EncodedImageFixture& fixture) {
  const size_t truncatedSize = fixture.encoded.size() * 3 / 4;
  std::vector<uint8_t> truncated(fixture.encoded.begin(),
                                 fixture.encoded.begin() + truncatedSize);
  bool threw = false;
  try {
    decode_in_chunks(kPngFactory,
                     full_size_options(fixture.width, fixture.height),
                     truncated, 521);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require_condition(threw, "truncated PNG must fail at end of input");
}

void run_png_case(bool adam7) {
  const auto fixture = make_png_fixture(adam7);
  const auto trace = decode_in_chunks(
      kPngFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 521);
  require_valid_completed_trace(trace, fixture.width, fixture.height);
  require_useful_precompletion_update(trace);
  if (adam7) {
    require_condition(trace.stillUpdates.size() >= 2,
                      "Adam7 PNG must publish multiple generations");
  }
  require_condition(maximum_channel_error(trace.stillUpdates.back().rgba,
                                          fixture.expectedRgba) <= 1,
                    "PNG final pixels must match the source, including alpha");
  require_truncated_png_fails(fixture);
}

void require_apng_decode() {
  const auto fixture = make_animated_png_fixture();
  const auto trace = decode_in_chunks(
      kPngFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 521);
  require_condition(trace.result == IncrementalBackendResult::Complete,
                    "APNG must complete incrementally");
  require_condition(trace.metadata.has_value() && trace.metadata->isAnimated,
                    "APNG metadata must advertise animation");
  require_condition(trace.completionInfo.has_value() &&
                        trace.completionInfo->loopCount == fixture.loopCount,
                    "APNG must preserve its loop count");
  require_condition(trace.animationFrames.size() ==
                        fixture.expectedFrames.size(),
                    "APNG must publish every display frame");
  for (size_t index = 0; index < trace.animationFrames.size(); ++index) {
    require_condition(trace.animationFrames[index].durationMillis ==
                          fixture.durationsMillis[index],
                      "APNG must preserve frame durations");
    require_condition(maximum_channel_error(trace.animationFrames[index].rgba,
                                            fixture.expectedFrames[index]) <= 1,
                      "APNG frames must preserve RGBA pixels");
  }
}

void require_scaled_png_decodes() {
  const auto still = make_png_fixture(false);
  require_scaled_still_is_area_average(kPngFactory, still.encoded, still.width,
                                       still.height, still.width * 3 / 4, 521,
                                       "scaled PNG");
  const auto animation = make_animated_png_fixture();
  require_scaled_frames_are_area_averages(
      kPngFactory, animation.encoded, animation.width, animation.height,
      animation.width * 3 / 4, 521, "scaled APNG");
}

} // namespace

void run_png_decoder_tests() {
  run_png_case(false);
  run_png_case(true);
  require_apng_decode();
  require_scaled_png_decodes();
}
