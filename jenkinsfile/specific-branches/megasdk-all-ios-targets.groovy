pipeline {
    agent { label 'osx && arm64' }

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
                    ios_sources_workspace = WORKSPACE
                }
            }
        }
        stage('Build iOS'){
            options{
                timeout(time: 120, unit: 'MINUTES')
            }
            environment{
                PATH = "/usr/local/bin:${env.PATH}"
                VCPKGPATH = "${env.HOME}/jenkins/vcpkg"
                BUILD_DIR_ARM64 = "build_dir_arm64"
                BUILD_DIR_X64 = "build_dir_x64"
            }
            steps{
                //Build SDK for arm64
                sh "echo Building SDK for iOS arm64"
                sh "cmake -DVCPKG_TARGET_TRIPLET=arm64-ios -DENABLE_SDKLIB_WERROR=ON -DENABLE_LOG_PERFORMANCE=ON -DUSE_LIBUV=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVCPKG_ROOT=${VCPKGPATH} -DCMAKE_VERBOSE_MAKEFILE=ON -S ${WORKSPACE} -B ${WORKSPACE}/${BUILD_DIR_ARM64}"
                sh "cmake --build ${WORKSPACE}/${BUILD_DIR_ARM64} -j2"

                //Build SDK for x64
                sh "echo \"Building SDK iOS x64 (crosscompiling)\""
                sh "cmake -DVCPKG_TARGET_TRIPLET=x64-ios -DENABLE_SDKLIB_WERROR=ON -DENABLE_LOG_PERFORMANCE=ON -DUSE_LIBUV=ON -DENABLE_SDKLIB_EXAMPLES=OFF -DENABLE_SDKLIB_TESTS=OFF -DENABLE_ISOLATED_GFX=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVCPKG_ROOT=${VCPKGPATH} -DCMAKE_VERBOSE_MAKEFILE=ON -S ${WORKSPACE} -B ${WORKSPACE}/${BUILD_DIR_X64}"
                sh "cmake --build ${WORKSPACE}/${BUILD_DIR_X64} -j2"
            }
        }
    }
    post {
        always {
            script {
                if (params.RESULT_TO_SLACK) {
                    sdk_commit = sh(script: "git -C ${ios_sources_workspace} rev-parse HEAD", returnStdout: true).trim()
                    messageStatus = currentBuild.currentResult
                    messageColor = messageStatus == 'SUCCESS'? "#00FF00": "#FF0000" //green or red
                    message = """
                        *iOS* <${BUILD_URL}|Build result>: '${messageStatus}'.
                        SDK branch: `${SDK_BRANCH}`
                        SDK_commit: `${sdk_commit}`
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
            deleteDir() /* clean up our workspace */
        }
    }
}
