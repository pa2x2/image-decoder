package tachiyomi.decoder

/** An immutable half-open image region: [left, right) x [top, bottom). */
data class ImageRegion(
  val left: Int,
  val top: Int,
  val right: Int,
  val bottom: Int,
) {
  val width: Int
    get() = right - left

  val height: Int
    get() = bottom - top
}
