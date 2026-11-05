package tachiyomi.decoder.incremental

import tachiyomi.decoder.Format
import tachiyomi.decoder.ImageRegion

/**
 * A format-neutral update produced by [IncrementalImageDecoder], whose pixels
 * are of the platform's type [P].
 */
sealed interface IncrementalDecodeUpdate<out P : Any> {

  data class FormatDetected(
    val format: Format,
    val capabilities: IncrementalDecodeCapabilities,
  ) : IncrementalDecodeUpdate<Nothing>

  data class MetadataAvailable(
    val info: IncrementalImageInfo,
  ) : IncrementalDecodeUpdate<Nothing>

  /**
   * An immutable snapshot of useful still-image pixels.
   *
   * The decoder will not mutate [pixels] after publishing this update. The
   * consumer owns them and is responsible for releasing them when safe.
   */
  data class StillImageAvailable<out P : Any>(
    val pixels: P,
    val updatedRegion: ImageRegion,
    val generation: Long,
  ) : IncrementalDecodeUpdate<P>

  /**
   * A complete, immutable animation canvas ready for presentation.
   *
   * The decoder will not mutate [pixels] after publishing this update. The
   * consumer owns them and is responsible for releasing them when safe.
   */
  data class AnimationFrameAvailable<out P : Any>(
    val pixels: P,
    val frame: IncrementalAnimationFrame,
    val generation: Long,
  ) : IncrementalDecodeUpdate<P>

  data class Complete(
    val info: IncrementalImageInfo,
  ) : IncrementalDecodeUpdate<Nothing>

  data class Unsupported(
    val format: Format?,
  ) : IncrementalDecodeUpdate<Nothing>

  data class Error(
    val message: String,
  ) : IncrementalDecodeUpdate<Nothing>
}
