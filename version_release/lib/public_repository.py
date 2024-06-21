from github3 import login, GitHub  # github3.py
from github3.repos.repo import Repository
from github3.repos.release import Release

class GitHubRepository:  # use github API

    def __init__(self, github_token: str, repo_owner: str, repo_name: str):
        gh = login(token=github_token)
        assert isinstance(gh, GitHub)
        self._repo = gh.repository("meganz", "sdk")
        assert isinstance(self._repo, Repository)

    def create_release(self, version: str, notes: str):
        assert isinstance(self._repo, Repository)
        release = self._repo.create_release(
            tag_name=f"v{version}", name=f"Version {version}", body=notes
        )
        assert isinstance(release, Release)
