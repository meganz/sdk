def failedDistros = []

pipeline {
    agent { label 'linux-testing-package-builder' }

    options { 
        buildDiscarder(logRotator(numToKeepStr: '25', daysToKeepStr: '30'))
        gitLabConnection('GitLabConnectionJenkins')
    }
    parameters {
        booleanParam(name: 'UPLOAD_TO_REPOSITORY', defaultValue: true, description: 'Should the package be uploaded to artifactory?')
        booleanParam(name: 'RESULT_TO_SLACK', defaultValue: true, description: 'Should the job result be sent to slack?')
        booleanParam(name: 'CUSTOM_BUILD', defaultValue: false, description: 'If true, will use DISTRO_TO_BUILD and ARCH_TO_BUILD. If false, will build all distributions')
        string(name: 'DISTRO_TO_BUILD', defaultValue: 'xUbuntu_22.04', description: 'Only used if CUSTOM_BUILD is true')
        string(name: 'SDK_BRANCH', defaultValue: 'SDK-4277-Build-SDK-for-all-supported-linux-distributions', description: 'Define a custom SDK branch.')
    }
    environment {
        gitlab_token = credentials('jenkins_sdk_token')
        SDK_BRANCH = "${params.SDK_BRANCH}"
    }

    stages {
        stage('Clean previous runs'){
            steps{
                deleteDir()
            }
        }

        stage('Checkout linux'){
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
                    linux_sources_workspace = WORKSPACE
                }
            }
        }
        stage ('Build custom distribution'){
            when { 
                beforeAgent true
                expression { params.CUSTOM_BUILD == true } 
            }
            steps {
                echo "Do Build for ${params.DISTRO_TO_BUILD}"
                dir(linux_sources_workspace) {
                    lock(resource: "${params.DISTRO_TO_BUILD}-amd64-sdk-build", quantity: 1) {
                        buildAndSignPackage("${params.DISTRO_TO_BUILD}", "amd64", "sdk")
                    }
                    script{
                        if ( params.UPLOAD_TO_REPOSITORY == true) {
                            def SDK_VERSION = getVersionFromHeader("include/mega/version.h")
                            sh "cd ${env.INTERNAL_REPO_PATH}/repo/private/$DISTRO_TO_BUILD && jf rt upload --regexp '((x86_64|amd64)/sdk.*deb\$|(x86_64|amd64)/sdk.*rpm\$|(x86_64|amd64)/sdk.*\\.pkg\\.tar\\.zst\$|(x86_64|amd64)/sdk.*\\.pkg\\.tar\\.xz\$)' sdk/$SDK_VERSION/linux/$DISTRO_TO_BUILD/"
                            echo "Packages successfully uploaded. URL: [${env.REPO_URL}/sdk/$SDK_VERSION/linux/$DISTRO_TO_BUILD/]"
                        }
                    }
                }
            }
            post {
                failure {
                    script {
                        failedDistros.add(params.DISTRO_TO_BUILD)
                    }
                }
            }
        }
        stage ('Build all distributions'){
            when {
                beforeAgent true
                expression { params.CUSTOM_BUILD == false }
            }
            matrix {
                axes {
                    axis { 
                        name 'ARCHITECTURE'; 
                        values 'amd64' 
                    }
                    axis { 
                        name 'DISTRO'; 
                        values  'xUbuntu_24.04', 'xUbuntu_23.10','xUbuntu_22.04', 'xUbuntu_20.04',
                                'Debian_11','Debian_12','Debian_testing',
                                'DEB_Arch_Extra',
                                'Fedora_38', 'Fedora_39', 'Fedora_40',
                                'openSUSE_Leap_15.5','openSUSE_Leap_15.6', 'openSUSE_Tumbleweed'
                    }
                }
                stages {
                    stage('Build') {
                        agent { label 'linux-testing-package-builder' }
                        steps {
                            echo "Do Build for ${DISTRO} - amd64"
                            dir(linux_sources_workspace) {
                                lock(resource: "${DISTRO}-amd64-sdk-build", quantity: 1) {
                                    buildAndSignPackage("${DISTRO}", "amd64", "sdk")
                                }
                            }
                        }
                        post {
                            failure {
                                script {
                                    failedDistros.add(DISTRO)
                                }
                            }
                        }
                    }
                    stage('Upload packages') {
                        when {
                            beforeAgent true
                            expression { params.UPLOAD_TO_REPOSITORY == true }
                        }
                        steps {
                            dir(linux_sources_workspace) {
                                script{
                                    def SDK_VERSION = getVersionFromHeader("include/mega/version.h")
                                    sh "jf rt del sdk/$SDK_VERSION/linux/$DISTRO"
                                    sh "cd ${env.INTERNAL_REPO_PATH}/repo/private/$DISTRO && jf rt upload --regexp '((x86_64|amd64)/sdk.*deb\$|(x86_64|amd64)/sdk.*rpm\$|(x86_64|amd64)/sdk.*\\.pkg\\.tar\\.zst\$|(x86_64|amd64)/sdk.*\\.pkg\\.tar\\.xz\$)' sdk/$SDK_VERSION/linux/$DISTRO/"  
                                    echo "Packages successfully uploaded. URL: [${env.REPO_URL}/sdk/$SDK_VERSION/linux/$DISTRO/]"
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    post {
        always {
            script {
                if (params.RESULT_TO_SLACK) {
                    sdk_commit = sh(script: "git -C ${linux_sources_workspace} rev-parse HEAD", returnStdout: true).trim()
                    messageStatus = currentBuild.currentResult
                    messageColor = messageStatus == 'SUCCESS'? "#00FF00": "#FF0000" //green or red
                    message = """
                        Jenkins job #${BUILD_ID} ended with status '${messageStatus}'.
                        See: ${BUILD_URL}
                        SDK branch: `${SDK_BRANCH}`
                        SDK_commit: `${sdk_commit}`
                    """.stripIndent()

                    if (failedDistros.size() > 0) {
                        message += "\n\nFailed distributions: ${failedDistros.join(', ')}"
                    }
                    
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
        }
    }
}


def buildAndSignPackage(String distro, String architecture, String packageName) {
    sh "${env.BUILDTOOLS_PATH}/build/buildManager.sh -a ${architecture} -j 1 build ${distro} . ${packageName}"
    sh "${env.BUILDTOOLS_PATH}/repo/repoManager.sh add ${env.INTERNAL_REPO_PATH}/builder/results/${distro}/${architecture}/${packageName}/ ${distro}"
    sh "SIGN_KEY_PATH=${env.INTERNAL_REPO_PATH}/sign_test/ ${env.BUILDTOOLS_PATH}/repo/repoManager.sh build -n ${distro}"
}

def getVersionFromHeader(String versionFilePath) {
    return sh(script: """
        awk  '/#define MEGA_MAJOR_VERSION/ { MAJOR=\$3 }; \
              /#define MEGA_MINOR_VERSION/ { MINOR=\$3 }; \
              /#define MEGA_MICRO_VERSION/ { MICRO=\$3 }; \
              END { print MAJOR"."MINOR"."MICRO }' \
              $versionFilePath
        """
        , returnStdout: true).trim()
}