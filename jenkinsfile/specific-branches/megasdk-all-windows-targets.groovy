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
            environment{
                VCPKGPATH  = "${WORKSPACE}\\..\\..\\vcpkg"
                QTPATH = "C:\\Qt\\Qt5.15.13\\5.15.13"
                BUILD_DIR = "build_dir"
            }
            options{
                timeout(time: 150, unit: 'MINUTES')
            }
            steps{
                //Build SDK
                sh "echo Building SDK x64"
                sh "rm -rf ${BUILD_DIR}; mkdir ${BUILD_DIR}"
                sh "cmake -DVCPKG_ROOT='${VCPKGPATH}' -DCMAKE_PREFIX_PATH='${QTPATH}'\\\\x64 -DCMAKE_VERBOSE_MAKEFILE=ON -DENABLE_QT_BINDINGS=ON -DENABLE_LOG_PERFORMANCE=ON -DUSE_LIBUV=ON -DCMAKE_GENERATOR_PLATFORM=x64 -S '${WORKSPACE}' -B '${WORKSPACE}'\\\\build_dir\\\\"
                sh "cmake --build '${WORKSPACE}'\\\\build_dir\\\\ --config RelWithDebInfo -j 1"

                sh "echo Building SDK x86"
                sh "rm -rf build_dir_x86; mkdir build_dir_x86"
                sh "cmake -DVCPKG_ROOT='${VCPKGPATH}' -DCMAKE_PREFIX_PATH='${QTPATH}'\\\\x86 -DCMAKE_VERBOSE_MAKEFILE=ON -DENABLE_QT_BINDINGS=ON -DENABLE_LOG_PERFORMANCE=ON -DUSE_LIBUV=ON -DCMAKE_GENERATOR_PLATFORM=Win32 -S '${WORKSPACE}' -B '${WORKSPACE}'\\\\build_dir_x86\\\\"
                sh "cmake --build '${WORKSPACE}'\\\\build_dir_x86\\\\ --config RelWithDebInfo -j 1"

                sh "echo Building SDK arm64"
                sh "rm -rf build_dir_arm64; mkdir build_dir_arm64"
                sh "cmake -A ARM64 -DVCPKG_ROOT='${VCPKGPATH}' -S '${WORKSPACE}' -B '${WORKSPACE}'\\\\build_dir_arm64\\\\"
                sh "cmake --build '${WORKSPACE}'\\\\build_dir_arm64\\\\ -j 1"
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
                                }' ${SLACK_WEBHOOK_URL}
                        """
                    }
                }
            }
            deleteDir()
        }
    }
}
