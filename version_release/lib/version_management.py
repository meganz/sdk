import json
from jira import JIRA
from jira.resources import Version
from requests import Response


class JiraProject:
    """
    Functionality:
    - rename NextRelease to the new version
    - create new NextRelease version
    """

    _NEXT_RELEASE = "NextRelease"

    def __init__(self, url: str, username: str, password: str, project: str):
        self._jira = JIRA(url, basic_auth=(username, password))
        self._project_name = project

        # validate version
        version: Version | None = self._jira.get_project_version_by_name(
            self._project_name, self._NEXT_RELEASE
        )
        assert version is not None
        assert version.released == False
        self._version_id = version.id
        self._version_manager_url = f"{self._jira.server_url}/rest/versionmanager/1.0/versionmanager/{self._project_name}"

    def update_current_version(self, to_version: str, used_by_apps: str):

        # A Version can be updated only by a project administrator.
        # To allow updating version details by the release captain,
        # there is a plugin installed called Version Manager for Jira.
        # The plugin can only be accessed through its own REST API.
        from datetime import date

        today = date.today().isoformat()  # YYYY-MM-DD required by the REST api

        json_data = json.dumps(
            {
                "name": f"v{to_version}",
                "startdate": today,
                "description": f"Version {to_version} - {used_by_apps}",
            }
        )

        # use the logged in session to access the REST API of the plugin
        assert self._jira._session is not None
        r: Response = self._jira._session.put(
            url=f"{self._version_manager_url}/{self._version_id}", data=json_data
        )
        r.raise_for_status()

    def create_new_version(self):
        # use the logged in session to access the REST API of the plugin
        assert self._jira._session is not None
        r: Response = self._jira._session.post(
            url=self._version_manager_url, data={"name": self._NEXT_RELEASE}
        )
        r.raise_for_status()
