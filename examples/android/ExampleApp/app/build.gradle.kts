plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    compileSdk = 36

    namespace = "nz.mega.android.bindingsample"

    defaultConfig {
        applicationId = "nz.mega.android.bindingsample"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
    }

    sourceSets.getByName("main") {
        java.srcDirs("../../../../bindings/java/nz/mega/sdk")
        java.exclude("**/MegaApiSwing.java")
        jniLibs.setSrcDirs(listOf("src/main/jniLibs"))
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    kotlinOptions {
        jvmTarget = "21"
    }
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation(fileTree(mapOf("dir" to "libs", "include" to listOf("*.jar"))))
    implementation(libs.appcompat)
    implementation(libs.exifinterface)
    implementation(libs.jetbrains.annotations)
}

