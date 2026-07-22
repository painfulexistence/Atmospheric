import java.io.BufferedInputStream
import java.io.FileOutputStream
import java.net.URI
import java.util.Properties
import java.util.zip.ZipInputStream

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Release signing, kept out of the repo: put an upload keystore + a
// keystore.properties next to settings.gradle.kts (both gitignored):
//   storeFile=upload-keystore.jks   (path relative to platforms/android/)
//   storePassword=...
//   keyAlias=upload
//   keyPassword=...
// Without the file, release builds stay unsigned (CI/library use).
val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties().apply {
    if (keystorePropertiesFile.exists()) {
        keystorePropertiesFile.inputStream().use { load(it) }
    }
}

val downloadSDLJavaSources = tasks.register("downloadSDLJavaSources") {
    val outputDir = file("src/main/java")
    val sdlVersion = "release-3.2.12"
    val zipUrl = "https://github.com/libsdl-org/SDL/archive/refs/tags/$sdlVersion.zip"

    outputs.dir(outputDir.resolve("org/libsdl/app"))

    doLast {
        val targetDir = outputDir.resolve("org/libsdl/app")
        if (targetDir.exists() && targetDir.list()?.isNotEmpty() == true) {
            return@doLast
        }

        println("Downloading SDL3 Java sources from GitHub ($zipUrl)...")
        targetDir.mkdirs()

        URI(zipUrl).toURL().openStream().use { inputStream ->
            ZipInputStream(BufferedInputStream(inputStream)).use { zipInputStream ->
                var entry = zipInputStream.nextEntry
                while (entry != null) {
                    val name = entry.name
                    val prefix = "SDL-$sdlVersion/android-project/app/src/main/java/org/libsdl/app/"
                    if (name.startsWith(prefix) && !entry.isDirectory) {
                        val fileName = name.substring(prefix.length)
                        val outFile = targetDir.resolve(fileName)
                        outFile.parentFile.mkdirs()
                        FileOutputStream(outFile).use { outputStream ->
                            zipInputStream.copyTo(outputStream)
                        }
                        println("Extracted: $fileName")
                    }
                    entry = zipInputStream.nextEntry
                }
            }
        }
        println("SDL3 Java sources successfully downloaded and extracted.")
    }
}

val copyAssetsTask = tasks.register<Copy>("copyAssetsForAndroid") {
    from("../../../../../Engine/default_assets") {
        into("assets")
    }
    from("../../../assets") {
        into("assets")
    }
    into(file("build/generated/assets"))
}

android {
    namespace = "com.atmospheric.midnightskyraiders"
    // Google Play requires new apps/updates to target API 35 (since Aug 2025).
    compileSdk = 35
    ndkVersion = "29.0.13113456"

    defaultConfig {
        applicationId = "com.atmospheric.midnightskyraiders"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"

        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${android.ndkDirectory.absolutePath}/build/cmake/android.toolchain.cmake"
                )
                // CMake target defined by Examples/MidnightSkyraiders/CMakeLists.txt's
                // ANDROID branch; also the library name MainActivity loads.
                targets("MidnightSkyraiders")
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            // Points to root CMakeLists.txt
            path = file("../../../../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    signingConfigs {
        create("release") {
            if (keystorePropertiesFile.exists()) {
                storeFile = rootProject.file(keystoreProperties["storeFile"] as String)
                storePassword = keystoreProperties["storePassword"] as String
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
            if (keystorePropertiesFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(file("build/generated/assets"))
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

tasks.named("preBuild") {
    dependsOn(downloadSDLJavaSources)
    dependsOn(copyAssetsTask)
}

dependencies {
    // SDL3 Java sources are copied from raw source by CMake during externalNativeBuild
}
