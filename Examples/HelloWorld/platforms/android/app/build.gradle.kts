import java.io.BufferedInputStream
import java.io.FileOutputStream
import java.net.URL
import java.util.zip.ZipInputStream

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
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
        
        URL(zipUrl).openStream().use { inputStream ->
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
    namespace = "com.atmospheric.helloworld"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.atmospheric.helloworld"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"

        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DANDROID_NDK_HOME=${android.ndkDirectory.absolutePath}",
                    "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${android.ndkDirectory.absolutePath}/build/cmake/android.toolchain.cmake"
                )
                targets("HelloWorld")
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

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(file("build/generated/assets"))
        }
    }
}

tasks.named("preBuild") {
    dependsOn(downloadSDLJavaSources)
    dependsOn(copyAssetsTask)
}

dependencies {
    // SDL3 Java sources are copied from raw source by CMake during externalNativeBuild
}
