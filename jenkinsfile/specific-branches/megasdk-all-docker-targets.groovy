def getImageTag (String distro, String build_id) {
    String tag = distro.replaceAll(/\.|:/, "-")
    tag += ":${build_id}"
    return tag
}

pipeline {
    agent { label 'docker' }
    options {
        timeout(time: 300, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '135', daysToKeepStr: '21'))
        gitLabConnection('GitLabConnectionJenkins')
        ansiColor('xterm')
    }
    parameters {
        booleanParam(name: 'RESULT_TO_SLACK', defaultValue: true, description: 'Should the job result be sent to slack?')
        string(name: 'SDK_BRANCH', defaultValue: 'develop', description: 'Define a custom SDK branch.')
        choice(name: 'DISTRO',
            choices: [
                'ubuntu:24.04', // Default value
                'debian:12',
                'debian:13',
                'debian:testing',
                'archlinux',
                'almalinux:9',
                'centos:stream9',
                'opensuse/leap:16.0',
                'opensuse/tumbleweed',
                'ubuntu:24.04',
                'fedora:44'
            ],
            description: 'Distribution to build')
    }
    stages {
        stage('checkout') {
            steps {
                checkout([
                    $class: 'GitSCM',
                    branches: [[name: "${SDK_BRANCH}"]],
                    userRemoteConfigs: [[ url: "${GIT_URL_SDK}", credentialsId: "12492eb8-0278-4402-98f0-4412abfb65c1" ]],
                    extensions: [
                        [$class: "UserIdentity",name: "jenkins", email: "jenkins@jenkins"]
                    ]
                ])
            }
        }
        stage("build image") {
            environment {
                IMAGE_TAG = getImageTag("$DISTRO", "$BUILD_ID")
            }
            steps {
                script {
                    sh """
                        docker build \
                          --build-arg DISTRO=${DISTRO} \
                          --build-arg ARCH=amd64 \
                          -f dockerfile/linux-build.dockerfile \
                          -t ${IMAGE_TAG} \
                          .
                    """
                    sh """
                        docker run \
                            -v ${WORKSPACE}:/mega/sdk:ro \
                    ${IMAGE_TAG}
                    """
                }
            }
            post {
                always {
                    script {
                        sh "docker rmi -f ${IMAGE_TAG}"
                    }
                }
            }
        }
    }
    post {
        always {
            script {
                if (params.RESULT_TO_SLACK) {
                    def sdk_commit = sh(script: "git -C ${WORKSPACE} rev-parse HEAD", returnStdout: true).trim()
                    def messageStatus = currentBuild.currentResult
                    def messageColor = messageStatus == 'SUCCESS'? "#00FF00": "#FF0000" //green or red
                    message = """
                        *SDK Docker ${DISTRO}* <${BUILD_URL}|Build result>: '${messageStatus}'.
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
                deleteDir()
            }
        }
    }
}
// vim: syntax=groovy tabstop=4 shiftwidth=4
