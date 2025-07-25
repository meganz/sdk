//Build SDK for a given architecture
def build_for_arch(String architecture) {
    def BUILD_DIR = "${WORKSPACE}\\build_dir_${architecture}"
    def QTPATH = "C:\\Qt\\Qt5.15.13\\5.15.13"
    def VCPKGPATH  = "${WORKSPACE}\\..\\..\\vcpkg"
    def CMAKE_FLAGS = "-DVCPKG_ROOT='${VCPKGPATH}' -DSWIG_EXECUTABLE='C:\\swigwin-4.0.2\\swig.exe' -DCMAKE_VERBOSE_MAKEFILE=ON -DENABLE_LOG_PERFORMANCE=ON -DENABLE_JAVA_BINDINGS=ON -DUSE_LIBUV=ON -DENABLE_MEDIA_FILE_METADATA=ON -S '${WORKSPACE}' -B '${BUILD_DIR}'"
    def CMAKE_PLATFORM = "-DCMAKE_GENERATOR_PLATFORM=${architecture}"

    // x64 and Win32 have QT bindings. arm64 does not.
    // Win32 is called x86 here
    def CMAKE_QT_FLAGS = ""
    switch (architecture) {
        case 'Win32':
            CMAKE_QT_FLAGS = "-DCMAKE_PREFIX_PATH='${QTPATH}\\x86' -DENABLE_QT_BINDINGS=ON"
            break
        case 'x64':
            CMAKE_QT_FLAGS = "-DCMAKE_PREFIX_PATH='${QTPATH}\\x64' -DENABLE_QT_BINDINGS=ON"
    }

    sh "rm -vrf '${BUILD_DIR}'; mkdir -v '${BUILD_DIR}'"
    sh "cmake ${CMAKE_PLATFORM} ${CMAKE_QT_FLAGS} ${CMAKE_FLAGS}"
    sh "cmake --build '${BUILD_DIR}' --config RelWithDebInfo -j 1"
}

pipeline {
    agent { label 'windows && amd64' }
    options { 
        buildDiscarder(logRotator(numToKeepStr: '60', daysToKeepStr: '21'))
        gitLabConnection('GitLabConnectionJenkins')
    }
    parameters {
        booleanParam(name: 'RESULT_TO_SLACK', defaultValue: true, description: 'Should the job result be sent to slack?')
        string(name: 'SDK_BRANCH', defaultValue: 'develop', description: 'Define a custom SDK branch.')
    }
    stages {
        stage('Clean previous runs'){
            steps{
                deleteDir()
            }
        }
        stage('Checkout SDK'){
            steps {
                checkout([
                    $class: 'GitSCM', 
                    branches: [[name: "${env.SDK_BRANCH}"]],
                    userRemoteConfigs: [[ url: "${env.GIT_URL_SDK}", credentialsId: "12492eb8-0278-4402-98f0-4412abfb65c1" ]],
                    extensions: [
                        [$class: "UserIdentity",name: "jenkins", email: "jenkins@jenkins"]
                        ]
                ])
                script {
                    windows_sources_workspace = WORKSPACE
                }
            }
        }
        stage('Build Windows'){
            options{
                // timeout: 30 min per architecture
                timeout(time: 90, unit: 'MINUTES')
            }
            stages {
                stage("Build x64") {
                    steps{
                        build_for_arch('x64')
                    }
                }
                stage("Build Win32") {
                    steps{
                        build_for_arch('Win32')
                    }
                }
                stage("Build ARM64") {
                    steps{
                        build_for_arch('ARM64')
                    }
                }
            }
        }
    }
    post {
        always {
            script {
                if (params.RESULT_TO_SLACK) {
                    sdk_commit = sh(script: "git -C '${windows_sources_workspace}' rev-parse HEAD", returnStdout: true).trim()
                    messageStatus = currentBuild.currentResult
                    messageColor = messageStatus == 'SUCCESS'? "#00FF00": "#FF0000" //green or red
                    message = """
                        *Windows* <${BUILD_URL}|Build result>: '${messageStatus}'.
                        SDK branch: `${SDK_BRANCH}`
                        SDK commit: `${sdk_commit}`
                    """.stripIndent()
                    
                    withCredentials([string(credentialsId: 'slack_webhook_sdk_report', variable: 'SLACK_WEBHOOK_URL')]) {
                        sh """
                            curl -X POST -H 'Content-type: application/json' --data '
                                {
                                "attachments": [
                                    {
                                        "color": "${messageColor}",
                                        "blocks": [
                                        {
                                            "type": "section",
                                            "text": {
                                                    "type": "mrkdwn",
                                                    "text": "${message}"
                                            }
                                        }
                                        ]
                                    }
                                    ]
                                }' \${SLACK_WEBHOOK_URL}
                        """
                    }
                }
            }
            deleteDir()
        }
    }
}
