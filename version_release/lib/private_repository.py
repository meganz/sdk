from gitlab import Gitlab  # python-gitlab
from gitlab.v4.objects import MergeRequest
import time


class GitLabRepository:  # use gitlab API
    """
    Functionality:
    - open MR
    - merge MR
    - close MR
    - delete branch
    """

    def __init__(self, url: str, gitlab_token: str, project_name: str):
        gl = Gitlab(url, gitlab_token)

        # test the authentication with private token
        # (will throw if url was wrong or token bad/expired)
        gl.auth()

        # find project by name
        valid_projects = [
            p for p in gl.projects.list(iterator=True) if p.name == project_name
        ]
        if not valid_projects:
            raise NameError(f"No project found with name {project_name}")
        if len(valid_projects) != 1:
            raise NameError(
                f"${len(valid_projects)} projects found with name {project_name}"
            )
        self._project = valid_projects[0]

        # create label if missing
        label_name = "Release"
        if label_name not in [l.name for l in self._project.labels.list(iterator=True)]:
            print(
                f"WARNING: Label {label_name} did not exist. Attempting to create it..."
            )
            if not self._project.labels.create(
                {"name": label_name, "color": "#8899aa"}
            ):
                raise IOError(f"Unable to create the label {label_name}")

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
        self, mr_title: str, mr_source: str, mr_target: str, labels: str | None = None
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
                "remove_source_branch": True,
                "squash": True,
                "subscribed": True,
                "labels": labels,
            }
        )

        if not mr:
            print("Failed to create MR")
            return 0, ""

        return mr.iid, mr.web_url

    def merge_mr(self, mr_id: int, wait_seconds: int) -> bool:
        mr = self._wait_for_mr_approval(mr_id, wait_seconds)
        if mr:
            mr.merge()  # TODO: check that this actually worked
            return True
        return False

    def _wait_for_mr_approval(self, mr_id: int, seconds: int) -> MergeRequest | None:
        if not self._project:
            print("Invalid repository. No project defined")
            return None

        mr: MergeRequest | None = None
        sleep_interval = 2
        for _ in range(int(seconds / sleep_interval + 1)):
            if mr:
                time.sleep(sleep_interval)
            mr = self._project.mergerequests.get(mr_id)
            if not mr or mr.state != "opened":
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

        print("Timeout waiting for MR approval")
        return None

    def close_mr(self, mr_id: int):
        if self._project:
            mr = self._project.mergerequests.get(mr_id)
            if mr:
                mr.state_event = "close"
                mr.save()

    def delete_branch(self, name: str):
        self._project.branches.delete(name)
