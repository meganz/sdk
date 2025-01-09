pipeline {
    agent { label 'windows && amd64' }

    options { 
        buildDiscarder(logRotator(numToKeepStr: '135', daysToKeepStr: '21'))
        gitLabConnection('GitLabConnectionJenkins')
    }
    
    stages {
        stage('Build Windows'){
            environment{
                VCPKGPATH  = "${WORKSPACE}\\..\\..\\vcpkg"
                BUILD_DIR = "build_dir"
            }
            options{
                timeout(time: 150, unit: 'MINUTES')
            }
            steps{
                //Build SDK
                sh "echo Building SDK x64"
                sh "rm -rf ${BUILD_DIR}; mkdir ${BUILD_DIR}"
                sh "cmake -DENABLE_CHAT=ON -DVCPKG_ROOT='${VCPKGPATH}' -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_GENERATOR_PLATFORM=x64 -S '${WORKSPACE}' -B '${WORKSPACE}'\\\\build_dir\\\\"
                sh "cmake --build '${WORKSPACE}'\\\\build_dir\\\\ --config ${BUILD_TYPE} -j 1"

                sh "echo Building SDK x86"
                sh "rm -rf build_dir_x86; mkdir build_dir_x86"
                sh "cmake -DENABLE_CHAT=ON -DVCPKG_ROOT='${VCPKGPATH}' -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_GENERATOR_PLATFORM=Win32 -S '${WORKSPACE}' -B '${WORKSPACE}'\\\\build_dir_x86\\\\"
                sh "cmake --build '${WORKSPACE}'\\\\build_dir_x86\\\\ --config ${BUILD_TYPE} -j 1"
            }
        }
        stage('Run Windows tests'){
            options{
                timeout(time: 250, unit: 'MINUTES')
            }
            environment {
                PATH = "${WORKSPACE}\\\\vcpkg_installed\\\\x64-windows-mega\\\\debug\\\\bin;${env.PATH}"
                MEGA_PWD = credentials('MEGA_PWD_DEFAULT')
                MEGA_PWD_AUX = credentials('MEGA_PWD_DEFAULT')
                MEGA_PWD_AUX2 = credentials('MEGA_PWD_DEFAULT')
                MEGA_REAL_PWD=credentials('MEGA_REAL_PWD_TEST')
            }
            steps{
                script {
                    def lockLabel = ''
                    if ("${APIURL_TO_TEST}" == 'https://g.api.mega.co.nz/') {
                        lockLabel = 'SDK_Concurrent_Test_Accounts'
                    } else  {
                        lockLabel = 'SDK_Concurrent_Test_Accounts_Staging'
                    }
                    lock(label: lockLabel, variable: 'ACCOUNTS_COMBINATION', quantity: 1, resourceSelectStrategy: "random", resource: null){
                        script{
                            env.MEGA_EMAIL = "${env.ACCOUNTS_COMBINATION}"
                            echo "${env.ACCOUNTS_COMBINATION}"
                        }
                        sh "echo Running tests"
                        bat """

                        cd build_dir

                        tests\\\\unit\\\\${BUILD_TYPE}\\\\test_unit.exe
                        if %ERRORLEVEL% NEQ 0 exit 1
                        tests\\\\integration\\\\${BUILD_TYPE}\\\\test_integration.exe --CI --USERAGENT:${env.USER_AGENT_TESTS_SDK} --APIURL:${APIURL_TO_TEST} ${TESTS_PARALLEL}
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
    post {
        always {
            archiveArtifacts artifacts: 'build_dir/*.log.gz', fingerprint: true
            deleteDir() /* clean up our workspace */
        }
    }
}
