from collections import defaultdict
from jira import JIRA
from jira.resources import Issue, Version
from requests import Response


class JiraProject:
    """
    Functionality:
    - rename NextRelease to the new version
    - create new NextRelease version
    - get release notes
    """

    _NEXT_RELEASE = "NextRelease"

    def __init__(
        self,
        url: str,
        username: str,
        password: str,
        project: str,
        version_name: str = _NEXT_RELEASE,
    ):
        self._jira = JIRA(url, basic_auth=(username, password))
        self._project_name = project

        # validate version
        self._version: Version | None = self._jira.get_project_version_by_name(
            self._project_name, version_name
        )
        assert self._version is not None
        assert self._version.released == False
        self._version_id = self._version.id
        self._version_manager_url = f"{self._jira.server_url}/rest/versionmanager/1.0/versionmanager/{self._project_name}"

    def update_current_version(self, to_version: str, used_by_apps: str):
        from datetime import date

        today = date.today().isoformat()  # YYYY-MM-DD required by the REST api
        version_data = {
            "name": f"v{to_version}",
            "startdate": today,
            "description": f"Version {to_version} - {used_by_apps}",
        }
        self._update_version(version_data)

    def create_new_version(self):
        # use the logged in session to access the REST API of the plugin
        assert self._jira._session is not None
        r: Response = self._jira._session.post(
            url=self._version_manager_url, data={"name": self._NEXT_RELEASE}
        )
        r.raise_for_status()

    def _update_version(self, new_data: dict):
        # A Version can be updated only by a project administrator.
        # To allow updating version details by the release captain,
        # there is a plugin installed called Version Manager for Jira.
        # The plugin can only be accessed through its own REST API.

        # use the logged in session to access the REST API of the plugin
        assert self._jira._session is not None
        r: Response = self._jira._session.put(
            url=f"{self._version_manager_url}/{self._version_id}", data=new_data
        )
        r.raise_for_status()

    def get_release_notes(self, apps: list[str]) -> str:
        return self._get_notes(apps, True)

    def _get_notes(self, apps: list[str], include_urls: bool) -> str:
        # get issues
        issues_found = self._jira.search_issues(
            f"project={self._project_name} AND fixVersion={self._version_id} AND status=Resolved AND resolution=Done",
            fields="issuetype, key, summary, ",
            maxResults=200,
        )
        issues: dict[str, list[tuple[str, str, str]]] = defaultdict(list)
        for i in issues_found:
            assert isinstance(i, Issue)
            i_type = i.get_field("issuetype").name
            i_url = i.permalink()
            i_summary = str(i.get_field("summary"))
            issues[i_type].append((i_url, i.key, i_summary))

        # build notes
        notes = ""
        for k, vs in issues.items():
            notes += f"{k}\n"
            for p in vs:
                notes += "\U00002022 ["
                if include_urls:
                    notes += f"<{p[0]}|{p[1]}>"
                else:
                    notes += p[0]
                notes += f"] - {p[2]}\n"
            notes += "\n"
        notes += "*Target apps*\n"
        for a in apps:
            notes += f"\U00002022 *{a}*\n"
        return notes
