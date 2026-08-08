#ifndef IMAGEDECODER_INCREMENTAL_ANIMATED_WEBP_DECODER_H
#define IMAGEDECODER_INCREMENTAL_ANIMATED_WEBP_DECODER_H

#include "incremental/format_decoder.h"

#include <memory>

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_animated_webp_decoder(
    const IncrementalDecodeOptionsNative& options);

#endif // IMAGEDECODER_INCREMENTAL_ANIMATED_WEBP_DECODER_H
