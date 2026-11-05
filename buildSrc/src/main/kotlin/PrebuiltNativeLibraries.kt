import org.gradle.api.file.DirectoryProperty
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction

/**
 * Takes libraries built elsewhere, such as by the workflows' parallel jobs, instead of building them: each of
 * [subdirectories] must hold [libraryFileName] under [prebuiltDirectory], and a missing one fails the build rather than
 * leaving a platform out of the artifact.
 */
abstract class PrebuiltNativeLibraries : NativeLibrariesTask() {

    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val prebuiltDirectory: DirectoryProperty

    @get:Input
    abstract val subdirectories: ListProperty<String>

    @get:Input
    abstract val libraryFileName: Property<String>

    @TaskAction
    fun copy() {
        val output = outputDirectory.get().asFile
        output.deleteRecursively()
        for (subdirectory in subdirectories.get()) {
            val library = prebuiltDirectory.get().dir(subdirectory).file(libraryFileName.get()).asFile
            check(library.isFile) { "No prebuilt library at $library" }
            library.copyTo(output.resolve(subdirectory).resolve(library.name))
        }
    }
}
