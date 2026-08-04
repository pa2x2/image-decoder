package tachiyomi.decoder.incremental

/** An immutable half-open image region: [left, right) x [top, bottom). */
data class IncrementalImageRegion(
  val left: Int,
  val top: Int,
  val right: Int,
  val bottom: Int,
)
