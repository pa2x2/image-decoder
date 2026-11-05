import com.android.build.api.variant.KotlinMultiplatformAndroidComponentsExtension

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.kotlin.multiplatform.library)
    alias(libs.plugins.maven.publish)
    alias(libs.plugins.tapmoc)
}

// The Android ABIs the AAR carries a library for. -PandroidAbis=<abi>[,<abi>…] limits a build to those, as each of the
// workflows' parallel native jobs builds only its own.
val androidAbis = providers.gradleProperty("androidAbis").orNull?.split(',')
    ?: listOf("arm64-v8a", "armeabi-v7a", "x86", "x86_64")
// Directory in the desktop jar the loader looks for this platform's library in (see NativeLibrary.jvm.kt).
val desktopPlatform = "linux_x64"

// -PprebuiltNativeLibraries=<directory> packages libraries built elsewhere instead of building them, from
// <directory>/android/<abi>/ and <directory>/desktop/jni/<platform>/: the layout the workflows' native jobs upload.
val prebuiltNativeLibraries = providers.gradleProperty("prebuiltNativeLibraries").orNull?.let(::file)

val nativeSources = fileTree("native") { exclude("test/**") }
val androidComponents = extensions.getByType<KotlinMultiplatformAndroidComponentsExtension>()

/**
 * Registers the task that provides the [kind] libraries, one per subdirectory: built with CMake and the arguments
 * [configureArguments] gives each subdirectory, or copied from [prebuiltNativeLibraries] when that is set.
 */
fun nativeLibraries(
    name: String,
    kind: String,
    subdirectories: List<String>,
    configureArguments: Provider<(String) -> List<String>>,
): TaskProvider<NativeLibrariesTask> {
    val output = layout.buildDirectory.dir("generated/native/$kind")
    if (prebuiltNativeLibraries != null) {
        tasks.register<PrebuiltNativeLibraries>(name) {
            prebuiltDirectory = prebuiltNativeLibraries.resolve(kind)
            this.subdirectories = subdirectories
            libraryFileName = "libimagedecoder.so"
            outputDirectory = output
        }
    } else {
        tasks.register<CMakeLibraryBuild>(name) {
            sources.from(nativeSources)
            sourceDirectory = layout.projectDirectory.dir("native/main")
            target = "imagedecoder"
            libraryFileName = "libimagedecoder.so"
            variants = configureArguments.map { arguments -> subdirectories.associateWith(arguments) }
            workDirectory = layout.buildDirectory.dir("native/$kind")
            outputDirectory = output
        }
    }
    return tasks.named<NativeLibrariesTask>(name)
}

val buildAndroidNativeLibraries = nativeLibraries(
    name = "buildAndroidNativeLibraries",
    kind = "android",
    subdirectories = androidAbis,
    configureArguments = libs.versions.android.ndk.flatMap { ndkVersion ->
        androidComponents.sdkComponents.sdkDirectory.map { sdk ->
            val ndk = sdk.dir("ndk/$ndkVersion").asFile
            check(ndk.isDirectory) {
                "NDK $ndkVersion is not installed in ${sdk.asFile}; install it with sdkmanager \"ndk;$ndkVersion\""
            }
            val arguments = { abi: String ->
                listOf(
                    "-DCMAKE_TOOLCHAIN_FILE=${ndk.resolve("build/cmake/android.toolchain.cmake")}",
                    "-DANDROID_ABI=$abi",
                    "-DANDROID_PLATFORM=android-21",
                )
            }
            arguments
        }
    },
)

val buildDesktopNativeLibrary = nativeLibraries(
    name = "buildDesktopNativeLibrary",
    kind = "desktop",
    subdirectories = listOf("jni/$desktopPlatform"),
    configureArguments = providers.systemProperty("java.home").map { javaHome ->
        { _: String ->
            listOf(
                "--toolchain", layout.projectDirectory.file("native/cmake/linux-x86_64.cmake").asFile.absolutePath,
                "-DJAVA_HOME=$javaHome",
            )
        }
    },
)

kotlin {
    android {
        namespace = "tachiyomi.decoder"
        compileSdk = 37
        minSdk = 21

        optimization {
            consumerKeepRules.publish = true
            consumerKeepRules.file("consumer-rules.pro")
        }
    }
    jvm()

    sourceSets {
        jvmMain {
            resources.srcDir(buildDesktopNativeLibrary)
        }
        jvmTest {
            dependencies {
                implementation(kotlin("test"))
                implementation(project.dependencies.platform(libs.junit.bom))
                runtimeOnly(libs.junit.platform.launcher)
            }
        }
    }
}

androidComponents.onVariants { variant ->
    variant.sources.jniLibs?.addGeneratedSourceDirectory(
        buildAndroidNativeLibraries,
        NativeLibrariesTask::outputDirectory,
    )
}

tasks.withType<Test>().configureEach {
    useJUnitPlatform()
}

tapmoc {
    java(17)
}
