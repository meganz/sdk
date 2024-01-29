pipeline {
    agent { label 'linux && amd64 && webrtc' }
    options { 
        buildDiscarder(logRotator(numToKeepStr: '25', daysToKeepStr: '15'))
    }
    stages {
        stage('build'){
            environment {
                MEGA_PWD_DEFAULT = credentials('MEGA_PWD_DEFAULT')
            }
            steps{
              deleteDir()
              withCredentials([gitUsernamePassword(credentialsId: 'jenkins_sdk_token_with_user', gitToolName: 'Default')]) {
                  sh """#!/bin/bash -x
                      set -e

                      git clone --branch master https://code.developers.mega.co.nz/megachat/MEGAchat .
                      sed -i "s#MEGAChatTest#JenkinsCanSpam#g" tests/sdk_test/sdk_test.h

                      export MEGA_EMAIL0=aag+jenkarere01.3@mega.co.nz
                      export MEGA_EMAIL1=aag+jenkarere01.4@mega.co.nz
                      export MEGA_EMAIL2=aag+jen06.8@mega.co.nz
                      export MEGA_PWD0=$MEGA_PWD_DEFAULT
                      export MEGA_PWD1=$MEGA_PWD_DEFAULT
                      export MEGA_PWD2=$MEGA_PWD_DEFAULT

                      kareredir=`pwd`
                      sdkinstalldir=`pwd`/sdk4karere

                      #Use a custom SDK with the corresponding branch
                      rm -vr megasdk || :
                      mkdir megasdk
                      git clone --branch master https://code.developers.mega.co.nz/sdk/sdk.git megasdk

                      # Create build directory to avoid problems with symlinks and ../../../ in the build scripts.
                      mkdir -p build

                      cd megasdk
                      ./autogen.sh
                      ./configure  --enable-chat --enable-shared --without-ffmpeg --without-pdfium --prefix=\$sdkinstalldir

                      cd "\$kareredir"

                      # Build with Qt
                      ln -sfrT megasdk third-party/mega
                      # Pointing to the old WebRTC until changes reach to Master
                      export WEBRTC_SRC=~/webrtc/src
                      export PATH=~/tools:\$PATH
                      cd third-party/mega/bindings/qt
                      sed -i "s#nproc#echo 1#" build_with_webrtc.sh
                      bash -x build_with_webrtc.sh all withExamples

                      # Config test environment
                      cd "\$kareredir"
                      rm -rvf ./build/subfolder || :
                      rm -fv build/test*.log || :
                      mkdir -p build/subfolder
                      cd build/subfolder
                      ln -sfr \$kareredir/build/MEGAchatTests/megachat_tests sdk_test

                      rm -fv test.log || :
                      rm -fv core 2>/dev/null || :

                      # Run test
                      ulimit -c unlimited
                      ./sdk_test || FAILED=1

                      if [ -n "\${FAILED}" ]; then
                          maxTime=10
                          startTime=`date +%s`
                          # Wait \$maxTime seconds for core to be generated, if any.
                          while [ ! -e "core" -o -n "\$( lsof core 2>/dev/null )" ] && [ \$( expr `date +%s` - \${startTime} ) -lt \${maxTime} ]; do
                              echo "Waiting for core dump..."
                              sleep 1
                          done
                          if [ -e "core" ] && [ -z "\$( lsof core 2>/dev/null )" ]; then
                              echo "Processing core dump..."
                              echo thread apply all bt > backtrace
                              echo quit >> backtrace
                              gdb -q ./sdk_test core -x \$PWD/backtrace
                          fi
                      fi

                      gzip -c test.log > test_${BUILD_ID}.log.gz || :
                      rm -fv test.log || :

                      if [ ! -z "\$FAILED" ]; then
                          false
                      fi
                  """
              }
            }
        }
    }
}
