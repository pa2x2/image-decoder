#include "fixtures/jpeg_fixture.h"
#include "incremental/session.h"
#include "support/decode_harness.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct SessionTrace {
  std::vector<IncrementalUpdateType> types;
  int32_t detectedCapabilities = 0;
};

SessionTrace drain_updates(IncrementalDecoderSession& session) {
  SessionTrace trace;
  IncrementalUpdate update{};
  while (session.pollUpdate(&update)) {
    trace.types.push_back(update.type);
    if (update.type == IncrementalUpdateType::FormatDetected) {
      trace.detectedCapabilities = update.capabilities;
    }
    if (update.type == IncrementalUpdateType::StillImageAvailable) {
      require_condition(update.snapshot != nullptr,
                        "session still update must carry a snapshot");
    }
    update = IncrementalUpdate{};
  }
  return trace;
}

size_t event_count(const SessionTrace& trace, IncrementalUpdateType type) {
  return static_cast<size_t>(
      std::count(trace.types.begin(), trace.types.end(), type));
}

size_t event_position(const SessionTrace& trace, IncrementalUpdateType type) {
  const auto position = std::find(trace.types.begin(), trace.types.end(), type);
  require_condition(position != trace.types.end(),
                    "required session event must be present");
  return static_cast<size_t>(position - trace.types.begin());
}

void require_session_completion_and_coalescing() {
  const auto fixture = make_jpeg_fixture(true);
  IncrementalDecoderSession session(
      full_size_options(fixture.width, fixture.height));

  size_t offset = 0;
  while (offset < fixture.encoded.size()) {
    const size_t count = std::min<size_t>(503, fixture.encoded.size() - offset);
    const bool endOfInput = offset + count == fixture.encoded.size();
    require_condition(
        session.append(fixture.encoded.data() + offset, count, endOfInput) ==
            IncrementalAppendResult::Accepted,
        "session must accept valid JPEG chunks");
    offset += count;
  }

  const auto trace = drain_updates(session);
  require_condition(event_count(trace, IncrementalUpdateType::FormatDetected) ==
                        1,
                    "session must detect the format once");
  require_condition((trace.detectedCapabilities & IncrementalCapabilityStill) !=
                        0,
                    "JPEG session must advertise still updates");
  require_condition(
      event_count(trace, IncrementalUpdateType::MetadataAvailable) == 1,
      "session must publish metadata once");
  require_condition(
      event_count(trace, IncrementalUpdateType::StillImageAvailable) == 1,
      "unpolled session still updates must be coalesced into one snapshot");
  require_condition(event_count(trace, IncrementalUpdateType::Complete) == 1,
                    "session must publish completion once");
  require_condition(event_count(trace, IncrementalUpdateType::Error) == 0,
                    "valid session must not publish an error");
  require_condition(
      event_position(trace, IncrementalUpdateType::FormatDetected) <
              event_position(trace, IncrementalUpdateType::MetadataAvailable) &&
          event_position(trace, IncrementalUpdateType::MetadataAvailable) <
              event_position(trace,
                             IncrementalUpdateType::StillImageAvailable) &&
          event_position(trace, IncrementalUpdateType::StillImageAvailable) <
              event_position(trace, IncrementalUpdateType::Complete),
      "session events must retain format, metadata, still, completion order");
}

void require_session_reports_truncation() {
  const auto fixture = make_jpeg_fixture(false);
  const size_t truncatedSize = fixture.encoded.size() * 3 / 4;
  IncrementalDecoderSession session(
      full_size_options(fixture.width, fixture.height));
  require_condition(session.append(fixture.encoded.data(), truncatedSize,
                                   true) == IncrementalAppendResult::Accepted,
                    "session must consume a terminal truncated chunk");

  const auto trace = drain_updates(session);
  require_condition(event_count(trace, IncrementalUpdateType::Error) == 1,
                    "truncated session must publish an error");
  require_condition(event_count(trace, IncrementalUpdateType::Complete) == 0,
                    "truncated session must not publish completion");
}

void require_session_rejects_unknown_format() {
  const std::vector<uint8_t> unknown = {'n', 'o', 't', '-', 'a', 'n',
                                        '-', 'i', 'm', 'a', 'g', 'e'};
  IncrementalDecoderSession session(full_size_options(64, 64));
  require_condition(
      session.append(unknown.data(), unknown.size(), true) ==
          IncrementalAppendResult::Accepted,
      "session must consume an unsupported terminal input safely");

  const auto trace = drain_updates(session);
  require_condition(event_count(trace, IncrementalUpdateType::Unsupported) == 1,
                    "unknown input must publish fallback");
  require_condition(
      event_count(trace, IncrementalUpdateType::StillImageAvailable) == 0,
      "unknown input must not publish pixels");
}

} // namespace

void run_session_tests() {
  require_session_completion_and_coalescing();
  require_session_reports_truncation();
  require_session_rejects_unknown_format();
}
