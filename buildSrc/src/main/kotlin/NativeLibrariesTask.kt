import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.tasks.OutputDirectory

/**
 * Provides the native library for each of a set of platforms, as `<subdirectory>/<library>` under [outputDirectory]:
 * an Android ABI, or the directory the desktop jar carries one platform's library in.
 */
abstract class NativeLibrariesTask : DefaultTask() {
    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty
}
