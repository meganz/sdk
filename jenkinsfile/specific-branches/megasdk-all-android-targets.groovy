pipeline {
    agent { label 'docker' }
    options { 
        buildDiscarder(logRotator(numToKeepStr: '60', daysToKeepStr: '21'))
        gitLabConnection('GitLabConnectionJenkins')
    }
    parameters {
        booleanParam(name: 'RESULT_TO_SLACK', defaultValue: true, description: 'Should the job result be sent to slack?')
        booleanParam(name: 'BUILD_ARM', defaultValue: true, description: 'Build for ARM')
        booleanParam(name: 'BUILD_ARM64', defaultValue: true, description: 'Build for ARM64')
        booleanParam(name: 'BUILD_X86', defaultValue: true, description: 'Build for X86')
        booleanParam(name: 'BUILD_X64', defaultValue: true, description: 'Build for X64')
        string(name: 'SDK_BRANCH', defaultValue: 'develop', description: 'Define a custom SDK branch.')
    }
    environment {
        VCPKGPATH = "/opt/vcpkg"
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
                    sdk_sources_workspace = WORKSPACE
                }
            }
        }

        stage('Build Android docker image'){
            steps{
                dir("dockerfile"){
                    sh "docker build -t meganz/android-build-env:${env.BUILD_NUMBER} -f ./android-cross-build.dockerfile ."
                }
            }
        }
        stage('Get UID and GID') {
            steps {
                script {
                    def uid = sh(script: 'id -u', returnStdout: true).trim()
                    def gid = sh(script: 'id -g', returnStdout: true).trim()
                    env.UID = uid
                    env.GID = gid
                }
            }
        }
        stage('Build with docker'){
            parallel {
                stage('Build arm'){
                    when {
                        beforeAgent true
                        expression { params.BUILD_ARM == true }
                    }
                    steps {
                        sh "docker run --name android-builder-arm-${env.BUILD_NUMBER} --rm -v ${WORKSPACE}:/mega/sdk -v ${VCPKGPATH}:/mega/vcpkg -e ARCH=arm meganz/android-build-env:${env.BUILD_NUMBER}"
                    }
                    post{
                        aborted {
                            sh "docker kill android-builder-arm-${env.BUILD_NUMBER}" 
                        }
                    }
                }
                stage('Build arm64'){
                    when {
                        beforeAgent true
                        expression { params.BUILD_ARM64 == true }
                    }
                    steps {
                        sh "docker run --name android-builder-arm64-${env.BUILD_NUMBER} --rm -v ${WORKSPACE}:/mega/sdk -v ${VCPKGPATH}:/mega/vcpkg -e ARCH=arm64 meganz/android-build-env:${env.BUILD_NUMBER}"
                    }
                    post{
                        aborted {
                            sh "docker kill android-builder-arm64-${env.BUILD_NUMBER}" 
                        }
                    }
                }
                stage('Build x86'){
                    when {
                        beforeAgent true
                        expression { params.BUILD_X86 == true }
                    }
                    steps {
                        sh "docker run --name android-builder-x86-${env.BUILD_NUMBER} --rm -v ${WORKSPACE}:/mega/sdk -v ${VCPKGPATH}:/mega/vcpkg -e ARCH=x86 meganz/android-build-env:${env.BUILD_NUMBER}"
                    }
                    post{
                        aborted {
                            sh "docker kill android-builder-x86-${env.BUILD_NUMBER}" 
                        }
                    }
                }
                stage('Build x64'){
                    when {
                        beforeAgent true
                        expression { params.BUILD_X64 == true }
                    }
                    steps {
                        sh "docker run --name android-builder-x64-${env.BUILD_NUMBER} --rm -v ${WORKSPACE}:/mega/sdk -v ${VCPKGPATH}:/mega/vcpkg -e ARCH=x64 meganz/android-build-env:${env.BUILD_NUMBER}"
                    }
                    post{
                        aborted {
                            sh "docker kill android-builder-x64-${env.BUILD_NUMBER}" 
                        }
                    }
                }
            }
        }
    }
    post {
        always {
            sh "docker image rm meganz/android-build-env:${env.BUILD_NUMBER}"
            script {
                if (params.RESULT_TO_SLACK) {
                    sdk_commit = sh(script: "git -C ${sdk_sources_workspace} rev-parse HEAD", returnStdout: true).trim()
                    messageStatus = currentBuild.currentResult
                    messageColor = messageStatus == 'SUCCESS'? "#00FF00": "#FF0000" //green or red
                    message = """
                        *Android* <${BUILD_URL}|Build result>: '${messageStatus}'.
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
