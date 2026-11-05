package tachiyomi.decoder

import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.Locale

private const val LIBRARY_NAME = "imagedecoder"

/** A directory holding the library, for hosts that ship it outside the jar. */
private const val LIBRARY_PATH_PROPERTY = "tachiyomi.decoder.library.path"

private val loadLock = Any()
private var loaded = false

/**
 * Loads the library from [LIBRARY_PATH_PROPERTY] when it is set, otherwise from the copy this jar carries for the
 * running platform, which the JVM can only load from a file and so is copied out to a temporary one first.
 */
internal actual fun loadNativeLibrary() = synchronized(loadLock) {
  if (loaded) return@synchronized
  val fileName = System.mapLibraryName(LIBRARY_NAME)
  val directory = System.getProperty(LIBRARY_PATH_PROPERTY)
  if (directory != null) {
    System.load(File(directory, fileName).absolutePath)
  } else {
    val resource = "jni/${platformDirectory()}/$fileName"
    val copy = Files.createTempFile(LIBRARY_NAME, null).toFile().apply { deleteOnExit() }
    val stream = ImageDecoder::class.java.classLoader.getResourceAsStream(resource)
      ?: throw UnsatisfiedLinkError("This jar has no image decoder for ${platformDirectory()} ($resource)")
    stream.use { Files.copy(it, copy.toPath(), StandardCopyOption.REPLACE_EXISTING) }
    System.load(copy.absolutePath)
  }
  loaded = true
}

private fun platformDirectory(): String {
  val os = System.getProperty("os.name").lowercase(Locale.ROOT)
  val arch = System.getProperty("os.arch").lowercase(Locale.ROOT)
  return when {
    os.contains("linux") && (arch == "amd64" || arch == "x86_64") -> "linux_x64"
    else -> "${os.replace(' ', '_')}_$arch"
  }
}
