import com.android.build.api.variant.KotlinMultiplatformAndroidComponentsExtension

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.kotlin.multiplatform.library)
    alias(libs.plugins.maven.publish)
    alias(libs.plugins.tapmoc)
}

// The Android ABIs the AAR carries a library for.
val androidAbis = listOf("arm64-v8a", "armeabi-v7a", "x86", "x86_64")
// Directory in the desktop jar the loader looks for this platform's library in (see NativeLibrary.jvm.kt).
val desktopPlatform = "linux_x64"

val nativeSources = fileTree("native") { exclude("test/**") }
val androidComponents = extensions.getByType<KotlinMultiplatformAndroidComponentsExtension>()

val buildAndroidNativeLibraries by tasks.registering(CMakeLibraryBuild::class) {
    val ndk = androidComponents.sdkComponents.ndkDirectory
    sources.from(nativeSources)
    sourceDirectory = layout.projectDirectory.dir("native/main")
    target = "imagedecoder"
    libraryFileName = "libimagedecoder.so"
    variants = ndk.map { ndkDirectory ->
        androidAbis.associateWith { abi ->
            listOf(
                "-DCMAKE_TOOLCHAIN_FILE=${ndkDirectory.file("build/cmake/android.toolchain.cmake").asFile}",
                "-DANDROID_ABI=$abi",
                "-DANDROID_PLATFORM=android-21",
            )
        }
    }
    workDirectory = layout.buildDirectory.dir("native/android")
    outputDirectory = layout.buildDirectory.dir("generated/native/android")
}

val buildDesktopNativeLibrary by tasks.registering(CMakeLibraryBuild::class) {
    sources.from(nativeSources)
    sourceDirectory = layout.projectDirectory.dir("native/main")
    target = "imagedecoder"
    libraryFileName = "libimagedecoder.so"
    variants = providers.systemProperty("java.home").map { javaHome ->
        mapOf(
            "jni/$desktopPlatform" to listOf(
                "--toolchain", layout.projectDirectory.file("native/cmake/linux-x86_64.cmake").asFile.absolutePath,
                "-DJAVA_HOME=$javaHome",
            ),
        )
    }
    workDirectory = layout.buildDirectory.dir("native/desktop")
    outputDirectory = layout.buildDirectory.dir("generated/native/desktop")
}

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
        CMakeLibraryBuild::outputDirectory,
    )
}

tasks.withType<Test>().configureEach {
    useJUnitPlatform()
}

tapmoc {
    java(17)
}
