def failedTargets = []

pipeline {
    agent { label 'docker' }
    options { 
        buildDiscarder(logRotator(numToKeepStr: '60', daysToKeepStr: '21'))
        gitLabConnection('GitLabConnectionJenkins')
    }
    parameters {
        booleanParam(name: 'RESULT_TO_SLACK', defaultValue: true, description: 'Should the job result be sent to slack?')
        booleanParam(name: 'CUSTOM_ARCH', defaultValue: false, description: 'If true, will use ARCH_TO_BUILD. If false, will build all architectures')
        string(name: 'ARCH_TO_BUILD', defaultValue: 'alpine', description: 'Only used if CUSTOM_ARCH is true')  
        string(name: 'SDK_BRANCH', defaultValue: 'develop', description: 'Define a custom SDK branch.')
    }
    environment {
        VCPKGPATH = "/opt/vcpkg"
        VCPKGPATH_CACHE = "${HOME}/.cache/vcpkg"
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
                    build_agent = "${NODE_NAME}"
                }
            }
        }

        stage('Build DSM docker image'){
            steps{
                dir("dockerfile"){
                    sh "docker build -t meganz/dsm-build-env:${env.BUILD_NUMBER} -f ./dms-cross-build.dockerfile ."
                }
            }
        }

        stage ('Build custom architecture'){
            when { 
                beforeAgent true
                expression { params.CUSTOM_ARCH == true } 
            }
            steps {
                echo "Do Build for ${params.ARCH_TO_BUILD}"
                dir(sdk_sources_workspace){
                    sh """ 
                        docker run --name dsm-builder-${params.ARCH_TO_BUILD}-${env.BUILD_NUMBER} --rm \
                            -v ${sdk_sources_workspace}:/mega/sdk \
                            -v ${VCPKGPATH}:/mega/vcpkg \
                            -v ${VCPKGPATH_CACHE}:/mega/.cache/vcpkg \
                            -e ARCH=${params.ARCH_TO_BUILD} meganz/dsm-build-env:${env.BUILD_NUMBER}
                    """
                }
            }
            post{
                aborted {
                    sh "docker kill android-builder-${params.ARCH_TO_BUILD}-${env.BUILD_NUMBER}"
                    script {
                        failedTargets.add("${params.ARCH_TO_BUILD}")
                    }
                }
                failure {
                    script {
                        failedTargets.add("${params.ARCH_TO_BUILD}")
                    }
                }
            }           
        }

        stage ('Build all distributions'){
            when {
                beforeAgent true
                expression { params.CUSTOM_ARCH == false }
            }
            matrix {
                axes {
                    axis { 
                        name 'ARCH'; 
                        values 'alpine', 'alpine4k', 'apollolake', 'armada37xx', 'armada38x',
                               'avoton','braswell', 'broadwell', 'broadwellnk', 'broadwellnkv2',
                               'broadwellntbap', 'bromolow', 'denverton', 'epyc7002',
                               'geminilake', 'grantley', 'kvmx64', 'monaco',
                               'purley', 'r1000', 'rtd1296', 'rtd1619b', 'v1000'
                    }
                }
                stages {
                    stage('Build') {
                        agent { label "${build_agent}" }
                        steps {
                            echo "Do Build for DSM - ${ARCH}"
                            dir(sdk_sources_workspace){
                                sh """ 
                                    docker run --name dsm-builder-${ARCH}-${env.BUILD_NUMBER} --rm \
                                        -v ${sdk_sources_workspace}:/mega/sdk \
                                        -v ${VCPKGPATH}:/mega/vcpkg \
                                        -v ${VCPKGPATH_CACHE}:/mega/.cache/vcpkg \
                                        -e ARCH=${ARCH} meganz/dsm-build-env:${env.BUILD_NUMBER}
                                """
                            }
                        }
                        post{
                            aborted {
                                sh "docker kill android-builder-${ARCH}-${env.BUILD_NUMBER}"
                                script {
                                    failedTargets.add("${ARCH}")
                                }
                            }
                            failure {
                                script {
                                    failedTargets.add("${ARCH}")
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
            sh "docker image rm meganz/dsm-build-env:${env.BUILD_NUMBER}"
            script {
                if (params.RESULT_TO_SLACK) {
                    sdk_commit = sh(script: "git -C ${sdk_sources_workspace} rev-parse HEAD", returnStdout: true).trim()
                    messageStatus = currentBuild.currentResult
                    messageColor = messageStatus == 'SUCCESS'? "#00FF00": "#FF0000" //green or red
                    message = """
                        *DSM* <${BUILD_URL}|Build result>: '${messageStatus}'.
                        SDK branch: `${SDK_BRANCH}`
                        SDK_commit: `${sdk_commit}`
                    """.stripIndent()
                    
                    if (failedTargets.size() > 0) {
                        message += "\nFailed targets: ${failedTargets.join(', ')}"
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
            deleteDir() /* clean up our workspace */
        }
    }
}
