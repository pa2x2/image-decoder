#include "fixtures/jpeg_fixture.h"
#include "incremental/jpeg_decoder.h"
#include "support/decode_harness.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

const IncrementalDecoderFactory kJpegFactory = create_incremental_jpeg_decoder;

void require_useful_precompletion_update(const IncrementalDecodeTrace& trace) {
  const auto update =
      std::find_if(trace.stillUpdates.begin(), trace.stillUpdates.end(),
                   [](const CapturedStillUpdate& candidate) {
                     return !candidate.receivedAtEndOfInput &&
                            populated_pixel_count(candidate) > 0;
                   });
  require_condition(update != trace.stillUpdates.end(),
                    "JPEG must publish useful pixels before end of input");
}

void require_truncated_jpeg_fails(const EncodedImageFixture& fixture) {
  const size_t truncatedSize = fixture.encoded.size() * 3 / 4;
  std::vector<uint8_t> truncated(fixture.encoded.begin(),
                                 fixture.encoded.begin() + truncatedSize);
  bool threw = false;
  try {
    decode_in_chunks(kJpegFactory,
                     full_size_options(fixture.width, fixture.height),
                     truncated, 509);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require_condition(threw, "truncated JPEG must fail at end of input");
}

void run_jpeg_case(bool progressive) {
  const auto fixture = make_jpeg_fixture(progressive);
  const auto trace = decode_in_chunks(
      kJpegFactory, full_size_options(fixture.width, fixture.height),
      fixture.encoded, 509);
  require_valid_completed_trace(trace, fixture.width, fixture.height);
  require_useful_precompletion_update(trace);
  if (progressive) {
    require_condition(trace.stillUpdates.size() >= 2,
                      "progressive JPEG must publish multiple generations");
  }

  const auto& finalPixels = trace.stillUpdates.back().rgba;
  require_condition(maximum_channel_error(finalPixels, fixture.expectedRgba) <=
                        1,
                    "incremental JPEG must match conventional JPEG decode");
  for (size_t offset = 3; offset < finalPixels.size(); offset += 4) {
    require_condition(finalPixels[offset] == 255,
                      "JPEG output must remain opaque");
  }
  require_truncated_jpeg_fails(fixture);
}

} // namespace

void run_jpeg_decoder_tests() {
  run_jpeg_case(false);
  run_jpeg_case(true);
}
