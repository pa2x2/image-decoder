package tachiyomi.decoder

import java.io.InputStream
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

/**
 * A native region decoder over one encoded image, shared by each platform's
 * public `ImageDecoder`, which adds that platform's pixel type.
 */
internal class NativeImageDecoder private constructor(
  private val nativePtr: Long,
  val sourceWidth: Int,
  val sourceHeight: Int,
  val bounds: ImageRegion,
) {

  val width: Int
    get() = bounds.width

  val height: Int
    get() = bounds.height

  var isRecycled = false
    private set

  private var shouldRecycle = false
  private val decoding = AtomicInteger()
  private val lock = ReentrantReadWriteLock()

  fun <T : Any> decode(region: ImageRegion, sampleSize: Int, allocator: PixelAllocator<T>): T? {
    checkValidInput(region, sampleSize)
    return try {
      lock.read {
        decoding.incrementAndGet()
        require(!isRecycled) { "The decoder has been recycled" }
      }
      @Suppress("UNCHECKED_CAST")
      nativeDecode(
        nativePtr, sampleSize, region.left, region.top, region.width, region.height, allocator,
      ) as T?
    } finally {
      val currentDecoding = decoding.decrementAndGet()
      if (currentDecoding == 0) {
        checkRecycle()
      }
    }
  }

  private fun checkValidInput(region: ImageRegion, sampleSize: Int) {
    val x = region.left
    val y = region.top
    val width = region.width
    val height = region.height

    if (x < 0 || x > this.width ||
      y < 0 || y > this.height ||
      width <= 0 || width + x > this.width ||
      height <= 0 || height + y > this.height
    ) {
      throw IllegalStateException("Requested region is invalid")
    }

    if (!(sampleSize > 0 && Integer.bitCount(sampleSize) == 1)) {
      throw IllegalStateException("Sample size must be a power of 2 but got $sampleSize")
    }
  }

  fun recycle() {
    if (isRecycled) return
    shouldRecycle = true
    checkRecycle()
  }

  private fun checkRecycle() {
    lock.write {
      if (!isRecycled && shouldRecycle && decoding.get() == 0) {
        nativeRecycle(nativePtr)
        isRecycled = true
      }
    }
  }

  protected fun finalize() {
    recycle()
  }

  companion object {
    init {
      loadNativeLibrary()
    }

    fun newInstance(
      stream: InputStream,
      cropBorders: Boolean,
      displayProfile: ByteArray?,
    ): NativeImageDecoder? {
      val info = IntArray(INFO_COUNT)
      val nativePtr = stream.use { nativeNewInstance(it, cropBorders, displayProfile, info) }
      if (nativePtr == 0L) return null
      val left = info[INFO_LEFT]
      val top = info[INFO_TOP]
      return NativeImageDecoder(
        nativePtr = nativePtr,
        sourceWidth = info[INFO_SOURCE_WIDTH],
        sourceHeight = info[INFO_SOURCE_HEIGHT],
        bounds = ImageRegion(left, top, left + info[INFO_WIDTH], top + info[INFO_HEIGHT]),
      )
    }

    fun findType(bytes: ByteArray): ImageType? = nativeFindType(bytes)

    @JvmStatic
    private external fun nativeNewInstance(
      stream: InputStream,
      cropBorders: Boolean,
      displayProfile: ByteArray?,
      info: IntArray,
    ): Long

    @JvmStatic
    private external fun nativeDecode(
      nativePtr: Long,
      sampleSize: Int,
      x: Int,
      y: Int,
      width: Int,
      height: Int,
      allocator: PixelAllocator<*>,
    ): Any?

    @JvmStatic
    private external fun nativeRecycle(nativePtr: Long)

    @JvmStatic
    private external fun nativeFindType(bytes: ByteArray): ImageType?
  }
}

// The order nativeNewInstance writes the decoder's dimensions in.
private const val INFO_SOURCE_WIDTH = 0
private const val INFO_SOURCE_HEIGHT = 1
private const val INFO_LEFT = 2
private const val INFO_TOP = 3
private const val INFO_WIDTH = 4
private const val INFO_HEIGHT = 5
private const val INFO_COUNT = 6
