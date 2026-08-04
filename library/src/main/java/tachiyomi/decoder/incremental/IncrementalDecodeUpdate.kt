package tachiyomi.decoder.incremental

import android.graphics.Bitmap
import tachiyomi.decoder.Format

/** A format-neutral update produced by [IncrementalImageDecoder]. */
sealed interface IncrementalDecodeUpdate {

  data class FormatDetected(
    val format: Format,
    val capabilities: IncrementalDecodeCapabilities,
  ) : IncrementalDecodeUpdate

  data class MetadataAvailable(
    val info: IncrementalImageInfo,
  ) : IncrementalDecodeUpdate

  /**
   * An immutable snapshot of useful still-image pixels.
   *
   * The decoder will not mutate [bitmap] after publishing this update. The
   * consumer owns the bitmap and is responsible for recycling it when safe.
   */
  data class StillImageAvailable(
    val bitmap: Bitmap,
    val updatedRegion: IncrementalImageRegion,
    val generation: Long,
  ) : IncrementalDecodeUpdate

  /**
   * A complete, immutable animation canvas ready for presentation.
   *
   * The decoder will not mutate [bitmap] after publishing this update. The
   * consumer owns the bitmap and is responsible for recycling it when safe.
   */
  data class AnimationFrameAvailable(
    val bitmap: Bitmap,
    val frame: IncrementalAnimationFrame,
    val generation: Long,
  ) : IncrementalDecodeUpdate

  data class Complete(
    val info: IncrementalImageInfo,
  ) : IncrementalDecodeUpdate

  data class Unsupported(
    val format: Format?,
  ) : IncrementalDecodeUpdate

  data class Error(
    val message: String,
  ) : IncrementalDecodeUpdate
}
