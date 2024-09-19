import re
from gitlab import Gitlab  # python-gitlab
from gitlab.v4.objects import (
    CurrentUser,
    Project,
    ProjectLabel,
    ProjectMergeRequest,
    ProjectTag,
)
import time
from typing import cast


class GitLabRepository:  # use gitlab API
    """
    Functionality:
    - open MR
    - merge MR
    - close MR
    - create branch
    - delete branch
    - create tag
    - delete tag
    """

    def __init__(self, url: str, gitlab_token: str, project_name: str):
        gl = Gitlab(url, gitlab_token)

        # test the authentication with private token
        # (will throw if url was wrong or token bad/expired)
        gl.auth()

        # find project by name
        possible_projects = gl.projects.list(
            search=project_name, simple=True, iterator=True
        )
        # There can be multiple projects with the same name, at least in different namespaces.
        # The one we care about apparently is in a namespace of type "group".
        # So far filtering by namespace type was enough, otherwise we might need to provide
        # the exact namespace or directly the project id.
        valid_projects = [
            p
            for p in possible_projects
            if p.name == project_name and p.namespace["kind"] == "group"
        ]
        if len(valid_projects) != 1:
            raise NameError(
                f"{len(valid_projects)} projects found with name {project_name}"
            )
        self._project = cast(Project, valid_projects[0])

        # create Release label if missing
        label_name = "Release"
        if label_name not in [l.name for l in self._project.labels.list(iterator=True)]:
            print(
                f"WARNING: Label {label_name} did not exist. Attempting to create it..."
            )
            l = self._project.labels.create({"name": label_name, "color": "#8899aa"})
            assert isinstance(l, ProjectLabel)

    def get_url_to_private_repo(self) -> str:
        return self._project.ssh_url_to_repo

    def _get_id_of_open_mr(self, mr_title: str, mr_source: str, mr_target: str) -> int:
        mrs = self._project.mergerequests.list(
            state="opened",
            source_branch=mr_source,
            target_branch=mr_target,
            iterator=True,
        )
        for mr in mrs:
            if mr.title == mr_title:
                return mr.iid
        return 0

    def _get_default_mr_description(self) -> str:
        # Default MR description configured for a GitLab project is apparently
        # not accessible, and most likely not appropriate for a Release. Use
        # only the minimum, potentially required by pipelines.
        description = (
            "ANDROID_BRANCH_TO_TEST=develop\n\n"
            "IOS_BRANCH_TO_TEST=develop\n\n"
            # leave empty to receive default value (can be removed after CID-491):
            "USE_APIURL_TO_TEST="
        )
        specific_description = {
            "SDK": "MEGACHAT_BRANCH_TO_TEST=develop",
            "MEGAchat": "SDK_BRANCH_TO_TEST=develop",
        }
        if self._project.name in specific_description:
            description += "\n\n" + specific_description[self._project.name]
        return description

    def open_mr(
        self,
        mr_title: str,
        mr_source: str,
        mr_target: str,
        remove_source: bool,
        squash: bool,
        labels: str | None = None,
    ) -> tuple[int, str]:
        mr_id = self._get_id_of_open_mr(mr_title, mr_source, mr_target)
        if mr_id > 0:
            print(f'MR with title "{mr_title}" was already opened')
            return 0, ""
        assert isinstance(self._project.manager.gitlab.user, CurrentUser)

        default_mr_description = self._get_default_mr_description()
        if not default_mr_description:
            print("WARN: default MR description not available, pipeline might fail")

        mr = self._project.mergerequests.create(
            {
                "title": mr_title,
                "source_branch": mr_source,
                "target_branch": mr_target,
                "remove_source_branch": remove_source,
                "squash": squash,
                "subscribed": True,
                "labels": labels,
                "assignee_id": self._project.manager.gitlab.user.id,
                "description": default_mr_description,
            }
        )
        assert isinstance(mr, ProjectMergeRequest)

        return mr.iid, mr.web_url

    def merge_mr(self, mr_id: int, wait_seconds: int) -> bool:
        mr = self._wait_for_mr_approval(mr_id, wait_seconds)
        if mr:
            mr.merge()  # TODO: check that this actually worked
            return True
        return False

    def _wait_for_mr_approval(
        self, mr_id: int, seconds: int
    ) -> ProjectMergeRequest | None:

        go_sleep = False
        sleep_interval = 2
        for _ in range(int(seconds / sleep_interval + 1)):
            if go_sleep:
                time.sleep(sleep_interval)
            mr = self._project.mergerequests.get(mr_id)
            assert isinstance(mr, ProjectMergeRequest)
            if mr.state != "opened":
                print("MR waiting for approval not found")
                return None
            if (
                mr.merge_status == "can_be_merged"
                and mr.detailed_merge_status == "mergeable"
                and not mr.draft
                and not mr.work_in_progress
                and not mr.has_conflicts
            ):
                return mr
            go_sleep = True

        print("Timeout waiting for MR approval")
        return None

    def close_mr(self, mr_id: int):
        if self._project:
            mr = self._project.mergerequests.get(mr_id)
            assert isinstance(mr, ProjectMergeRequest)
            mr.state_event = "close"
            mr.save()

    def create_branch(self, name: str, target: str):
        branch = self._project.branches.create({"branch": name, "ref": target})
        assert branch is not None

    def delete_branch(self, name: str):
        self._project.branches.delete(name)

    def create_tag(self, name: str, target: str):
        tag = self._project.tags.create({"tag_name": name, "ref": target})
        assert tag is not None

    def delete_tag(self, name: str):
        self._project.tags.delete(name)

    def get_tag_url(self, tag_name: str) -> str:
        tag: ProjectTag = self._project.tags.get(tag_name)
        commit_url = tag.commit["web_url"]
        tag_url = commit_url.replace(f"/commit/{tag.target}", f"/commits/{tag.name}")
        return tag_url

    def get_last_commit_in_branch(self, branch_name: str) -> str:
        commits = self._project.commits.list(
            ref_name=branch_name, get_all=False, per_page=1
        )
        assert isinstance(commits, list)
        assert len(commits) == 1
        return commits[0].id

    def create_release(self, name: str, target: str, notes: str):
        release = self._project.releases.create(
            {"name": name, "tag_name": target, "description": notes}
        )
        assert release is not None

    def get_last_rc(self, release_name: str) -> int:
        re_pattern = "^" + re.escape(f"{release_name}-rc.") + r"(\d+)$"
        rc = 0
        tag_list = self._project.tags.list(get_all=True)
        for t in tag_list:
            if (
                isinstance(t.name, str)
                and (result := re.search(re_pattern, t.name))
                and int(result.group(1)) > rc
            ):
                rc = int(result.group(1))
        return rc
