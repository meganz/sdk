pipeline {
    agent { label 'amd64 && linux && android' }
    options { 
        buildDiscarder(logRotator(numToKeepStr: '25', daysToKeepStr: '15'))
    }
    environment {
        ANDROID_HOME = '/home/jenkins/android-cmdlinetools/'
        ANDROID_NDK_HOME = '/home/jenkins/android-ndk'
    }
    stages {
        stage('build'){
            steps{
                sh "export PATH=\$PATH:\$ANDROID_HOME/cmdline-tools/tools/bin/"
                dir ("examples/android/ExampleApp/app/src/main/jni/") {
                    sh "sed -i \"s#-j[0-9]##g\" build.sh"
                    sh "sed -i 's#LOG_FILE=/dev/null#LOG_FILE=/dev/stdout#g' build.sh"
                    sh "rm -rf ../java/nz/mega/sdk/"
                    // Build libs and SDK.
                    sh "bash -x ./build.sh clean"
                    sh "bash -x ./build.sh all"
                }
                dir ( "examples/android/ExampleApp") {
                    sh "./gradlew --no-daemon --max-workers=1 build"
                }
            }
        }
    }
    post  {
        always {
            deleteDir() /* clean up our workspace */
        }        
    }
}
