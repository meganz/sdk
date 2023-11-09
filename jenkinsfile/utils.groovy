import groovy.json.JsonSlurperClassic

// Uploads a file to a Gitlab project
// Requires env.GITLAB_BASE_URL
String uploadFileToGitLab(String fileName, String projectId) {
    String link = ""
    String gitlabBaseUrl = "https://code.developers.mega.co.nz"
    withCredentials([usernamePassword(credentialsId: 'Gitlab-Access-Token', usernameVariable: 'USERNAME', passwordVariable: 'TOKEN')]) {
        String response = sh(script: "curl -s --request POST --header PRIVATE-TOKEN:$TOKEN --form file=@${fileName} ${gitlabBaseUrl}/api/v4/projects/${projectId}/uploads", returnStdout: true).trim()
        link = new JsonSlurperClassic().parseText(response).markdown
    }
    return link
}

// Downloads the console log from this Jenkins build
void downloadJenkinsConsoleLog(String fileName) {
    withCredentials([usernameColonPassword(credentialsId: 'Jenkins-Login', variable: 'CREDENTIALS')]) {
        sh "curl -u ${CREDENTIALS} ${BUILD_URL}consoleText -o ${fileName}"
    }
}

// Posts an error message in a merge request
void commentErrorInMr(String projectId) {
    String message = ""
    String fileName = "build.log"
    String logUrl ""
    downloadJenkinsConsoleLog(fileName)
    logUrl = uploadFileToGitLab(fileName, projectId)
    message = """
        :x: Build Failed
        Commit ID: ${env.GIT_COMMIT}
        Build URL: ${BUILD_URL}
        <br/>Build Log: ${logUrl}
    """
    addGitLabMRComment(comment: message)
}
