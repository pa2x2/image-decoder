#ifndef IMAGEDECODER_INCREMENTAL_AREA_ROW_RESAMPLER_H
#define IMAGEDECODER_INCREMENTAL_AREA_ROW_RESAMPLER_H

#include "incremental/area_resampling.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// Area-averages source rows as they are decoded. Each pass over the image must
// deliver every row once, top to bottom; a later pass, such as a progressive
// JPEG refinement, replaces the output of the previous one. Every output row a
// source row touches is re-resolved from the rows received so far, so partly
// decoded rows are shown instead of waiting for the whole footprint.
class IncrementalAreaRowResampler {
public:
  using OutputRowSink =
      std::function<void(uint32_t outputY, const uint8_t* rgba)>;

  IncrementalAreaRowResampler(uint32_t sourceWidth, uint32_t sourceHeight,
                              uint32_t outputWidth, uint32_t outputHeight);

  void addRow(uint32_t sourceY, const uint8_t* rgba,
              const OutputRowSink& sink);

private:
  struct RowSums {
    uint64_t receivedWeight = 0;
    std::vector<AreaPixelSums> sums;
  };

  void addToRow(uint32_t outputY, uint32_t rowWeight, uint32_t sourceY,
                const OutputRowSink& sink);

  AreaAxis xAxis;
  AreaAxis yAxis;
  uint32_t nextSourceY = 0;
  std::vector<AreaPixelSums> sourceRowSums;
  // A source row reaches at most two adjacent output rows, so rows in progress
  // never share a slot when slots are chosen by output row parity.
  std::array<RowSums, 2> rowsInProgress;
  std::vector<uint8_t> resolvedRow;
};

#endif // IMAGEDECODER_INCREMENTAL_AREA_ROW_RESAMPLER_H
