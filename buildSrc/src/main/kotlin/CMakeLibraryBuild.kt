import org.gradle.api.DefaultTask
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.provider.MapProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.process.ExecOperations
import javax.inject.Inject

/**
 * Builds CMake target [target] once per entry of [variants], each with its own configure arguments, and copies each
 * build's [libraryFileName] into that entry's subdirectory of [outputDirectory]. CMake's build
 * directories persist under [workDirectory], so an unchanged library only costs a no-op Ninja run.
 */
abstract class CMakeLibraryBuild : DefaultTask() {

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sources: ConfigurableFileCollection

    @get:Internal
    abstract val sourceDirectory: DirectoryProperty

    /** Output subdirectory to the CMake configure arguments of the build that fills it. */
    @get:Input
    abstract val variants: MapProperty<String, List<String>>

    @get:Input
    abstract val target: Property<String>

    @get:Input
    abstract val libraryFileName: Property<String>

    @get:Internal
    abstract val workDirectory: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty

    @get:Inject
    protected abstract val exec: ExecOperations

    @TaskAction
    fun build() {
        val output = outputDirectory.get().asFile
        output.deleteRecursively()
        for ((subdirectory, arguments) in variants.get()) {
            val buildDirectory = workDirectory.get().dir(subdirectory).asFile
            exec.exec {
                commandLine(
                    listOf(
                        "cmake",
                        "-S", sourceDirectory.get().asFile.absolutePath,
                        "-B", buildDirectory.absolutePath,
                        "-G", "Ninja",
                    ) + arguments,
                )
            }
            exec.exec { commandLine("cmake", "--build", buildDirectory.absolutePath, "--target", target.get()) }
            val library = buildDirectory.resolve(libraryFileName.get())
            library.copyTo(output.resolve(subdirectory).resolve(library.name), overwrite = true)
        }
    }
}
