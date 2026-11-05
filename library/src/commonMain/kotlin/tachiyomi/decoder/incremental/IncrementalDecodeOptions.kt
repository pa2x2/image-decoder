package tachiyomi.decoder.incremental

/**
 * Resource bounds requested by an incremental image consumer.
 *
 * The decoder may emit smaller output when the encoded image or its configured
 * pixel limit requires it. Long images may be emitted as bounded tiles by a
 * format backend instead of one proportional full-height bitmap.
 */
class IncrementalDecodeOptions(
  val preferredOutputWidth: Int = DEFAULT_PREFERRED_OUTPUT_WIDTH,
  val maximumBitmapPixels: Long = DEFAULT_MAXIMUM_BITMAP_PIXELS,
  displayProfile: ByteArray? = null,
) {

  internal val displayProfile: ByteArray? = displayProfile?.clone()

  init {
    require(preferredOutputWidth > 0) { "Preferred output width must be positive" }
    require(preferredOutputWidth <= MAXIMUM_OUTPUT_DIMENSION) {
      "Preferred output width must not exceed $MAXIMUM_OUTPUT_DIMENSION"
    }
    require(maximumBitmapPixels > 0) { "Maximum bitmap pixels must be positive" }
    require(maximumBitmapPixels <= MAXIMUM_BITMAP_PIXELS) {
      "Maximum bitmap pixels must not exceed $MAXIMUM_BITMAP_PIXELS"
    }
    require(displayProfile == null || displayProfile.size <= MAXIMUM_DISPLAY_PROFILE_BYTES) {
      "Display profile must not exceed $MAXIMUM_DISPLAY_PROFILE_BYTES bytes"
    }
  }

  companion object {
    const val DEFAULT_PREFERRED_OUTPUT_WIDTH = 2_048
    const val DEFAULT_MAXIMUM_BITMAP_PIXELS = 4_194_304L
    const val MAXIMUM_OUTPUT_DIMENSION = 32_768
    const val MAXIMUM_BITMAP_PIXELS = 67_108_864L
    const val MAXIMUM_DISPLAY_PROFILE_BYTES = 4 * 1_024 * 1_024
  }
}
