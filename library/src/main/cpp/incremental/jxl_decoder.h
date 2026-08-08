#ifndef IMAGEDECODER_INCREMENTAL_JXL_DECODER_H
#define IMAGEDECODER_INCREMENTAL_JXL_DECODER_H

#include "incremental/format_decoder.h"

#include <memory>

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_jxl_decoder(const IncrementalDecodeOptionsNative& options);

#endif // IMAGEDECODER_INCREMENTAL_JXL_DECODER_H
