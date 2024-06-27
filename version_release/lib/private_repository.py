from gitlab import Gitlab  # python-gitlab
from gitlab.v4.objects import Project, ProjectLabel, ProjectMergeRequest, ProjectTag
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

        mr = self._project.mergerequests.create(
            {
                "title": mr_title,
                "source_branch": mr_source,
                "target_branch": mr_target,
                "remove_source_branch": remove_source,
                "squash": squash,
                "subscribed": True,
                "labels": labels,
            }
        )

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
        commits = self._project.commits.list(ref_name=branch_name, per_page=1)
        assert isinstance(commits, list)
        assert len(commits) == 1
        return commits[0].sha

    def create_release(self, name: str, target: str, notes: str):
        release = self._project.releases.create(
            {"name": name, "tag_name": target, "description": notes}
        )
        assert release is not None
