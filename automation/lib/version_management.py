from collections import defaultdict
from jira import JIRA, Project
from jira.resources import Issue, Version
from requests import Response
import re


class JiraProject:
    """
    Functionality:
    - rename NextRelease to the new version
    - create new NextRelease version
    - get release notes
    - check all tickets on a release are resolved or closed
    """

    _NEXT_RELEASE = "NextRelease"

    def __init__(
        self,
        url: str,
        username: str,
        password: str,
        project: str,
    ):
        self._jira = JIRA(url, basic_auth=(username, password))
        self._project_key = self._get_project_key(project)
        assert self._project_key, f"No project found with name {project}"
        self._version_manager_url = f"{self._jira.server_url}/rest/versionmanager/1.0/versionmanager/{self._project_key}"

    def _get_project_key(self, project_name: str) -> str:
        all_projects = self._jira.projects()
        # find by name, otherwise by name that starts with the received value
        temp_key = ""
        temp_count = 0
        for p in all_projects:
            assert isinstance(p, Project)
            if p.name == project_name:
                return p.key
            if p.name.startswith(project_name + " "):
                temp_count += 1
                temp_key = p.key
        return temp_key if temp_count == 1 else ""

    def setup_release(self, release_name: str = _NEXT_RELEASE):
        # validate version
        self._version: Version | None = self._jira.get_project_version_by_name(
            self._project_key, release_name
        )
        assert self._version is not None
        assert self._version.released == False
        self._version_id = self._version.id
        self._check_all_tickets_are_resolved_or_closed()

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

    def create_new_version_for_patch(self, name: str, for_apps: str):
        # use the logged in session to access the REST API of the plugin
        assert self._jira._session is not None

        from datetime import date

        today = date.today().isoformat()  # YYYY-MM-DD required by the REST api

        r: Response = self._jira._session.post(
            url=self._version_manager_url,
            data={
                "name": name,
                "releasedate": today,
                "description": f"Version {name} - {for_apps}",
            },
        )
        r.raise_for_status()

    def update_version_close_release(self):
        from datetime import date

        today = date.today().isoformat()  # YYYY-MM-DD required by the REST api
        version_data = {
            "releasedate": today,
            "status": "Released",
        }
        self._update_version(version_data)

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

    def get_release_notes_for_slack(self, apps: list[str]) -> str:
        return self._get_notes(apps, formatting="slack", include_urls=True)

    def get_release_notes_for_gitlab(self, apps: list[str]) -> str:
        return self._get_notes(apps, formatting="git", include_urls=True)

    def get_release_notes_for_github(self) -> str:
        return self._get_notes([], formatting="git", include_urls=False)

    def _get_notes(self, apps: list[str], formatting: str, include_urls: bool) -> str:
        if len(apps) == 0:
            # get apps from description
            assert self._version is not None
            app_descr: str = self._version.description.partition(" - ")[2]
            apps = [a.strip() for a in app_descr.split("/")]

        # get issues
        issues_found = self._jira.search_issues(
            f"project={self._project_key} AND fixVersion={self._version_id} AND status=Resolved AND resolution=Done",
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
        bullet = self._get_bullet_placeholder(formatting)
        for k, vs in issues.items():
            notes += self._get_notes_chapter(k, formatting)
            for p in vs:
                url = p[0] if include_urls else ""
                notes += (
                    bullet + " " + self._get_notes_issue(p[1], url, p[2], formatting)
                )
            notes += "\n"
        notes += self._get_notes_chapter("Target apps", formatting)
        for a in apps:
            notes += f"{bullet} {a}\n"
        return notes

    def _get_bullet_placeholder(self, formatting: str) -> str:
        if formatting == "slack":
            return "\U00002022"  # utf8 bullet
        return "-"  # "git" and others

    def _get_notes_chapter(self, title: str, formatting: str) -> str:
        if formatting == "slack":
            return f"{title}\n"
        if formatting == "git":
            return f"## **{title}**\n\n"
        return f"{title}\n\n"  # return it with no formatting

    def _get_notes_issue(
        self, id: str, url: str, description: str, formatting: str
    ) -> str:
        prefix = ""
        if not url:
            prefix = f"[{id}]"
        elif formatting == "slack":
            prefix = f"[<{url}|{id}>]"
        elif formatting == "git":
            prefix = f"\\[[{id}]({url})\\]"
        else:
            prefix = id  # return it with no formatting

        issue = prefix + f" - {description}\n"
        return issue

    def get_version_info(self, version: str) -> tuple[bool, bool, str]:
        v: Version | None = self._jira.get_project_version_by_name(
            self._project_key, f"v{version}"
        )
        if v is None:
            return False, False, ""
        assert not v.archived, f"Archived v{version} version already exists"
        # get apps from description
        app_descr: str = v.description.partition(" - ")[2]
        return True, v.released, app_descr

    def earlier_versions_are_closed(self):
        assert self._version is not None
        new_major, new_minor, new_micro = (
            int(n) for n in self._version.name[1:].split(".")
        )
        all_versions = self._jira.project_versions(self._project_key)
        for v in all_versions:
            assert isinstance(v, Version)
            if (
                not v.archived
                and not v.released
                and v.name != self._version.name
                and re.match(r"^v(\d)+\.(\d+)\.(\d+)$", v.name)
            ):
                old_major, old_minor, old_micro = (
                    int(n) for n in v.name[1:].split(".")
                )
                assert new_major < old_major or (
                    new_major == old_major
                    and (
                        new_minor < old_minor
                        or (new_minor == old_minor and new_micro < old_micro)
                    )
                ), f"Release {v.name} must be closed before continuing"

    def get_next_version(self) -> tuple[int, int, int]:
        highest_existing_version = self._get_highest_existing_version()
        release_number_affected = self._get_version_component_to_increment()
        match release_number_affected:
            case "Major":
                next_version = (highest_existing_version[0] + 1, 0, 0)
            case "Minor":
                next_version = (
                    highest_existing_version[0],
                    highest_existing_version[1] + 1,
                    0,
                )
            case _:
                next_version = (
                    highest_existing_version[0],
                    highest_existing_version[1],
                    highest_existing_version[2] + 1,
                )
        return next_version

    def _get_highest_existing_version(self) -> tuple[int, int, int]:
        # find the highest existing version
        highest_major = highest_minor = highest_micro = 0
        all_versions = self._jira.project_versions(self._project_key)
        for v in all_versions:
            assert isinstance(v, Version)
            if re.match(r"^v(\d)+\.(\d+)\.(\d+)$", v.name):
                major, minor, micro = (int(n) for n in v.name[1:].split("."))
                if major > highest_major:
                    highest_major = major
                    highest_minor = minor
                    highest_micro = micro
                elif major == highest_major:
                    if minor > highest_minor:
                        highest_minor = minor
                        highest_micro = micro
                    elif minor == highest_minor and micro > highest_micro:
                        highest_micro = micro
        return (highest_major, highest_minor, highest_micro)

    def _get_version_component_to_increment(self) -> str:
        # get id of custom field
        custom_field_id = ""
        all_the_fields = self._jira.fields()
        for f in all_the_fields:
            if f["custom"] and f["name"] == "Release number affected":
                custom_field_id = f["id"]  # 'customfield_10502'
                break
        assert custom_field_id

        # get relevant issues
        unreleased_issues = self._jira.search_issues(
            f"project={self._project_key} AND fixVersion={self._version_id} AND status=Resolved AND resolution=Done",
            maxResults=200,
        )

        # get higest Release number affected
        release_number_affected = "Patch"
        for i in unreleased_issues:
            assert isinstance(i, Issue)
            affected = i.raw["fields"][custom_field_id]["value"]
            if affected == "Major":
                return affected
            if affected == "Minor":
                release_number_affected = affected
        return release_number_affected

    def add_fix_version_to_tickets(self, tickets: list[str]):
        assert self._version
        for t in tickets:
            issue: Issue = self._jira.issue(t)
            fixVersions = []
            for v in issue.fields.fixVersions:
                if v.name != self._version.name:
                    fixVersions.append({"name": v.name})
            fixVersions.append({"name": self._version.name})
            issue.update({"fixVersions": fixVersions})

    def _check_all_tickets_are_resolved_or_closed(self):
        jql_query = (
            f'project = "{self._project_key}" AND fixVersion = "{self._version.name}" '
            f'AND status NOT IN ("Resolved", "Closed")'
        )
        issues = self._jira.search_issues(jql_query)

        assert not issues, (
            f"The following tickets are not resolved or closed for Fix Version '{self._version.name}':\n"
            + "\n".join(
                f"- {issue.key} -> {issue.fields.status.name}" for issue in issues
            )
        )
