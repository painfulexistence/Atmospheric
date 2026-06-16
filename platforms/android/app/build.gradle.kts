plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.atmospheric.engine"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.atmospheric.engine.atmoslua"
        minSdk = 26          // OpenGL ES 3.0 + Vulkan baseline
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        // Tell Gradle to invoke CMake for the native (C++) library
        externalNativeBuild {
            cmake {
                // The root CMakeLists.txt builds AtmosphericEngine + AtmosLua
                // as a shared library when CMAKE_ANDROID_* vars are set.
                arguments(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=TRUE",
                )
                targets("AtmosLua")
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
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
}

dependencies {
    // Google Play Billing 7 — the reason Kotlin is preferred over Java here
    implementation(libs.billing)
    implementation(libs.billing.ktx)
}
