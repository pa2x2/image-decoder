package tachiyomi.decoder.incremental

import tachiyomi.decoder.Format
import java.io.Closeable
import java.nio.ByteBuffer

/**
 * Stateful decoder for encoded image bytes that arrive over time.
 *
 * Calls are serialized per instance. Input buffers are consumed synchronously
 * and may be reused after [append] returns. Updates own their published bitmap
 * snapshots and are retrieved with [pollUpdate].
 */
class IncrementalImageDecoder private constructor(
  private var nativePtr: Long,
) : Closeable {

  private val lock = Any()
  private val transferBuffer = ByteArray(NATIVE_APPEND_CHUNK_SIZE)
  private var inputEnded = false

  val isClosed: Boolean
    get() = synchronized(lock) { nativePtr == 0L }

  /**
   * Appends [length] bytes. The supplied array may be reused after this method
   * returns.
   */
  fun append(
    bytes: ByteArray,
    offset: Int = 0,
    length: Int = bytes.size - offset,
    endOfInput: Boolean = false,
  ) = synchronized(lock) {
    checkOpen()
    check(!inputEnded) { "End of input has already been signalled" }
    require(offset >= 0 && length >= 0 && offset <= bytes.size - length) {
      "Invalid byte array range"
    }

    if (length == 0) {
      nativeAppendByteArray(nativePtr, bytes, offset, 0, endOfInput)
    } else {
      var position = offset
      val end = offset + length
      while (position < end) {
        val chunkSize = minOf(NATIVE_APPEND_CHUNK_SIZE, end - position)
        val isLastChunk = position + chunkSize == end
        nativeAppendByteArray(
          nativePtr,
          bytes,
          position,
          chunkSize,
          endOfInput && isLastChunk,
        )
        position += chunkSize
      }
    }
    if (endOfInput) inputEnded = true
  }

  /**
   * Appends all remaining bytes and advances [buffer] to its limit. Direct
   * buffers avoid the Java-to-native copy. Other buffers use one reusable
   * transfer array.
   */
  fun append(buffer: ByteBuffer, endOfInput: Boolean = false) = synchronized(lock) {
    checkOpen()
    check(!inputEnded) { "End of input has already been signalled" }

    val remaining = buffer.remaining()
    if (remaining == 0) {
      nativeAppendByteArray(nativePtr, transferBuffer, 0, 0, endOfInput)
    } else if (buffer.isDirect) {
      var position = buffer.position()
      val end = buffer.limit()
      while (position < end) {
        val chunkSize = minOf(NATIVE_APPEND_CHUNK_SIZE, end - position)
        val isLastChunk = position + chunkSize == end
        nativeAppendDirectBuffer(
          nativePtr,
          buffer,
          position,
          chunkSize,
          endOfInput && isLastChunk,
        )
        position += chunkSize
        buffer.position(position)
      }
    } else {
      while (buffer.hasRemaining()) {
        val chunkSize = minOf(transferBuffer.size, buffer.remaining())
        buffer.get(transferBuffer, 0, chunkSize)
        nativeAppendByteArray(
          nativePtr,
          transferBuffer,
          0,
          chunkSize,
          endOfInput && !buffer.hasRemaining(),
        )
      }
    }
    if (endOfInput) inputEnded = true
  }

  /** Signals clean end-of-input without appending another byte. */
  fun finish() {
    append(EMPTY_INPUT, endOfInput = true)
  }

  /** Returns the next decoder update, or null when no update is pending. */
  fun pollUpdate(): IncrementalDecodeUpdate? = synchronized(lock) {
    checkOpen()
    val update = nativePollUpdate(nativePtr) ?: return@synchronized null
    check(update.size == UPDATE_VALUE_COUNT) {
      "Unexpected native incremental update size ${update.size}"
    }
    val format = update[UPDATE_FORMAT_INDEX]
      .takeIf { it >= 0 }
      ?.let(Format::from)
    when (update[UPDATE_TYPE_INDEX]) {
      UPDATE_FORMAT_DETECTED -> IncrementalDecodeUpdate.FormatDetected(
        format = checkNotNull(format),
        capabilities = IncrementalDecodeCapabilities(
          stillImageUpdates = update[UPDATE_CAPABILITIES_INDEX] and CAPABILITY_STILL != 0,
          animationFrames = update[UPDATE_CAPABILITIES_INDEX] and CAPABILITY_ANIMATION != 0,
        ),
      )
      UPDATE_UNSUPPORTED -> IncrementalDecodeUpdate.Unsupported(format)
      UPDATE_ERROR -> IncrementalDecodeUpdate.Error("Incremental decoder failed")
      else -> error("Unknown native incremental update ${update[UPDATE_TYPE_INDEX]}")
    }
  }

  override fun close() = synchronized(lock) {
    val pointer = nativePtr
    if (pointer == 0L) return@synchronized
    nativePtr = 0L
    nativeRecycle(pointer)
  }

  @Suppress("deprecation")
  protected fun finalize() {
    close()
  }

  private fun checkOpen() {
    check(nativePtr != 0L) { "The incremental decoder has been closed" }
  }

  private external fun nativeAppendByteArray(
    nativePtr: Long,
    bytes: ByteArray,
    offset: Int,
    length: Int,
    endOfInput: Boolean,
  )

  private external fun nativeAppendDirectBuffer(
    nativePtr: Long,
    buffer: ByteBuffer,
    offset: Int,
    length: Int,
    endOfInput: Boolean,
  )

  private external fun nativePollUpdate(nativePtr: Long): IntArray?

  private external fun nativeRecycle(nativePtr: Long)

  companion object {
    init {
      System.loadLibrary("imagedecoder")
    }

    fun newInstance(
      options: IncrementalDecodeOptions = IncrementalDecodeOptions(),
    ): IncrementalImageDecoder {
      val nativePtr = nativeNewInstance(
        options.preferredOutputWidth,
        options.maximumBitmapPixels,
        options.displayProfile,
      )
      check(nativePtr != 0L) { "Failed to initialize incremental decoder" }
      return IncrementalImageDecoder(nativePtr)
    }

    @JvmStatic
    private external fun nativeNewInstance(
      preferredOutputWidth: Int,
      maximumBitmapPixels: Long,
      displayProfile: ByteArray?,
    ): Long
  }
}

private const val NATIVE_APPEND_CHUNK_SIZE = 64 * 1_024
private val EMPTY_INPUT = ByteArray(0)

private const val UPDATE_TYPE_INDEX = 0
private const val UPDATE_FORMAT_INDEX = 1
private const val UPDATE_CAPABILITIES_INDEX = 2
private const val UPDATE_VALUE_COUNT = 3

private const val UPDATE_FORMAT_DETECTED = 1
private const val UPDATE_UNSUPPORTED = 2
private const val UPDATE_ERROR = 3

private const val CAPABILITY_STILL = 1
private const val CAPABILITY_ANIMATION = 1 shl 1
