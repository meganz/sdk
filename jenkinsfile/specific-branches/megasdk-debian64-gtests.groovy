pipeline {
    agent { label 'linux && amd64' }
    options { 
        buildDiscarder(logRotator(numToKeepStr: '25', daysToKeepStr: '15'))
    }
    stages {
        stage('Build Linux'){
            options{
                timeout(time: 120, unit: 'MINUTES')
            }
            environment{
                VCPKGPATH = "/opt/vcpkg"
                BUILD_DIR = "build_dir"
            }
            steps{
                sh "rm -rf ${BUILD_DIR}; mkdir ${BUILD_DIR}"
                sh "cmake -DENABLE_SDKLIB_WERROR=ON -DENABLE_CHAT=ON -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DVCPKG_ROOT=${VCPKGPATH} -DCMAKE_VERBOSE_MAKEFILE=ON -S ${WORKSPACE} -B ${WORKSPACE}/${BUILD_DIR}"
                sh "cmake --build ${WORKSPACE}/${BUILD_DIR} -j1"
            }
        }
        stage('Run Linux tests'){
            options{
                timeout(time: 250, unit: 'MINUTES')
            }
            environment {
                MEGA_PWD = credentials('MEGA_PWD_DEFAULT')
                MEGA_PWD_AUX = credentials('MEGA_PWD_DEFAULT')
                MEGA_PWD_AUX2 = credentials('MEGA_PWD_DEFAULT')
                MEGA_REAL_PWD=credentials('MEGA_REAL_PWD_TEST')
                BUILD_DIR = "build_dir"
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
                        sh """#!/bin/bash
                        set -x
                        ulimit -c unlimited
                        cd ${env.BUILD_DIR}

                        ./tests/unit/test_unit || FAILED=1
                        
                        if [ -z \"\$FAILED\" ]; then
                            # Parallel run
                            ./tests/integration/test_integration --CI --USERAGENT:${env.USER_AGENT_TESTS} --APIURL:${APIURL_TO_TEST} ${TESTS_PARALLEL} 2>&1 | tee tests.stdout
                        [ \"\${PIPESTATUS[0]}\" != \"0\" ] && FAILED=2
                        fi
                        if [ -n \"\$FAILED\" ]; then
                            if [ \"\$FAILED\" -eq 1 ]; then
                                coreFiles=core
                            else # FAILED=2
                                # Parallel run
                                procFailed=`grep \"<< PROCESS\" tests.stdout | sed 's/.*PID:\\([0-9]*\\).*/\\1/'`
                                if [ -n \"\$procFailed\" ]; then
                                    # Parallel run
                                    for i in \$procFailed; do
                                        coreFiles=\"\$coreFiles pid_\$i/core\"
                                    done
                                fi
                            fi
                            if [ -n \"\$coreFiles\" ]; then
                                maxTime=10
                                startTime=`date +%s`
                                coresProcessed=0
                                coresTotal=`echo \$coreFiles | wc -w`
                                # While there are pending cores
                                while [ \$coresProcessed -lt \$coresTotal ] && [ \$( expr `date +%s` - \$startTime ) -lt \$maxTime ]; do
                                    echo "Waiting for core dumps..."
                                    sleep 1
                                    for i in \$coreFiles; do
                                        if [ -e \"\$i\" ] && [ -z \"\$( lsof \$i 2>/dev/null )\" ]; then
                                            echo \"Processing core dump \$i :: \$(grep `echo \$i | sed 's#pid_\\([0-9].*\\)/core#\\1#'` tests.stdout)\"
                                            echo thread apply all bt > backtrace
                                            echo quit >> backtrace
                                            [ \"\$FAILED\" = \"1\" ] && gdb -q ./tests/unit/test_unit \$i -x ${WORKSPACE}/backtrace
                                            [ \"\$FAILED\" = \"2\" ] && gdb -q ./tests/integration/test_integration \$i -x ${WORKSPACE}/backtrace
                                            tar rf core.tar \$i
                                            coresProcessed=`expr \$coresProcessed + 1`
                                            coreFiles=`echo \$coreFiles | sed -e \"s#\$i##\"`
                                        fi
                                    done
                                done
                                if [ -e core.tar ]; then
                                    [ \"\$FAILED\" = \"1\" ] && tar rf core.tar -C ./tests/unit/ test_unit
                                    [ \"\$FAILED\" = \"2\" ] && tar rf core.tar -C ./tests/integration/ test_integration
                                    gzip core.tar
                                fi
                            fi
                        fi

                        gzip -c test_integration.log > test_integration_${BUILD_ID}.log.gz || :
                        rm test_integration.log || :
                        if [ -n \"\$FAILED\" ]; then
                            exit \"\$FAILED\"
                        fi
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
