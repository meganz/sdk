pipeline {
    agent { label 'linux && amd64' }
    options { 
        buildDiscarder(logRotator(numToKeepStr: '25', daysToKeepStr: '15'))
    }
    stages {
        stage('build'){
            steps{
                sh "./clean.sh"
                sh "./contrib/build_sdk.sh -g -e -v -u -q -a -f"
            }
        }
    }
}
