pipeline {
    agent { label 'windows && amd64' }

    options { 
        buildDiscarder(logRotator(numToKeepStr: '135', daysToKeepStr: '21'))
        gitLabConnection('GitLabConnectionJenkins')
    }
    
    stages {
        stage('Checkout Windows'){
            steps {
                deleteDir()
                checkout([
                    $class: 'GitSCM', 
                    branches: [[name: "${env.BRANCH_NAME}"]],
                    userRemoteConfigs: [[ url: "${env.GIT_URL_SDK}", credentialsId: "12492eb8-0278-4402-98f0-4412abfb65c1" ]],
                    extensions: [
                        [$class: "UserIdentity",name: "jenkins", email: "jenkins@jenkins"],
                        ]
                ])
                script{
                    windows_sources_workspace = WORKSPACE           
                }
            }
        }
        stage('Build Windows'){
            environment{
                VCPKGPATH  = "${windows_sources_workspace}\\..\\..\\vcpkg"
                BUILD_DIR = "build_dir"
            }
            options{
                timeout(time: 150, unit: 'MINUTES')
            }
            steps{
                dir(windows_sources_workspace){
                    //Build SDK
                    sh "echo Building SDK x64"
                    sh "rm -rf ${BUILD_DIR}; mkdir ${BUILD_DIR}"
                    sh "cmake -DENABLE_CHAT=ON -DVCPKG_ROOT='${VCPKGPATH}' -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_GENERATOR_PLATFORM=x64 -S '${windows_sources_workspace}' -B '${windows_sources_workspace}'\\\\build_dir\\\\"
                    sh "cmake --build '${windows_sources_workspace}'\\\\build_dir\\\\ --config Release -j 1"

                    sh "echo Building SDK x86"
                    sh "mkdir build_dir_x86"
                    sh "cmake -DENABLE_SDKLIB_WERROR=OFF -DENABLE_CHAT=ON -DVCPKG_ROOT='${VCPKGPATH}' -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_GENERATOR_PLATFORM=Win32 -S '${windows_sources_workspace}' -B '${windows_sources_workspace}'\\\\build_dir_x86\\\\"
                    sh "cmake --build '${windows_sources_workspace}'\\\\build_dir_x86\\\\ --config Release -j 1"
                }
            }
        }

        stage('Run Windows tests'){
            options{
                timeout(time: 250, unit: 'MINUTES')
            }
            environment {
                PATH = "${windows_sources_workspace}\\\\vcpkg_installed\\\\x64-windows-mega\\\\debug\\\\bin;${env.PATH}"
                MEGA_PWD = credentials('MEGA_PWD_DEFAULT')
                MEGA_PWD_AUX = credentials('MEGA_PWD_DEFAULT')
                MEGA_PWD_AUX2 = credentials('MEGA_PWD_DEFAULT')
                MEGA_REAL_PWD=credentials('MEGA_REAL_PWD_TEST')
            }
            steps{
                lock(label: 'SDK_Concurrent_Test_Accounts', variable: 'ACCOUNTS_COMBINATION', quantity: 1, resource: null){
                    dir("${windows_sources_workspace}") {
                        script{
                            env.MEGA_EMAIL = "${env.ACCOUNTS_COMBINATION}"
                            echo "${env.ACCOUNTS_COMBINATION}"
                        }
                        sh "echo Running tests"
                        bat """

                        cd build_dir

                        tests\\\\unit\\\\Release\\\\test_unit.exe
                        if %ERRORLEVEL% NEQ 0 exit 1
                        tests\\\\integration\\\\Release\\\\test_integration.exe --CI --USERAGENT:${env.USER_AGENT_TESTS} --APIURL:${APIURL_TO_TEST} ${TESTS_PARALLEL}
                        if %ERRORLEVEL% NEQ 0 set ERROR_VAL=1
                        gzip -c test_integration.log > test_integration_${BUILD_ID}.log.gz
                        rm test_integration.log
                        exit %ERROR_VAL%
                        """
                    }
                }
            }
        }              
    }
}
