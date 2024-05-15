import re
from lib.chat import Slack
from lib.local_repository import LocalRepository
from lib.private_repository import GitLabRepository
from lib.sign_commits import setup_gpg_signing
from lib.version_management import JiraProject


class ReleaseProcess:

    def __init__(
        self,
        project_name: str,
        gitlab_token: str,
        private_host_url: str,
        private_branch: str,
        new_version: str,
        slack_token: str,
        slack_channel: str,
    ):
        self._private_branch = private_branch
        self._local_repo: LocalRepository | None = None
        self._remote_private_repo = GitLabRepository(
            private_host_url, gitlab_token, project_name
        )
        self._project_name = project_name
        self._new_version = new_version
        self._slack = (
            Slack(slack_token) if slack_token != "" and slack_channel != "" else None
        )
        self._slack_channel = slack_channel

    # STEP 3: update version in local file
    def update_version_in_local_file(
        self,
        gpg_keygrip: str,
        gpg_password: str,
        private_remote_name: str,
        private_remote_url: str,
        new_branch: str,
    ):
        setup_gpg_signing(gpg_keygrip, gpg_password)
        self._prepare_local_repo(private_remote_name, private_remote_url)
        self._change_version_in_file(self._new_version)
        self._push_to_new_branch(self._new_version, new_branch, private_remote_name)
        self._merge_local_changes(self._new_version, new_branch)

    def _prepare_local_repo(
        self,
        remote_name: str,
        remote_url: str,
    ):
        self._local_repo = LocalRepository(remote_name, remote_url)
        self._local_repo.check_for_uncommitted_changes()
        self._local_repo.switch_to_branch(self._private_branch)
        self._local_repo.sync_current_branch(remote_name)

    # Edit version file
    def _change_version_in_file(
        self,
        new_version: str,
    ):
        version = new_version.split(".")
        assert len(version) == 3, f"Invalid requested version: {new_version}"

        # read old version
        oldMajor = oldMinor = oldMicro = 0
        assert self._local_repo is not None, "Call _prepare_local_repo() first"
        lines = self._local_repo.version_file.read_text().splitlines()
        for i, line in enumerate(lines):
            if result := re.search(
                r"^(#define\s+MEGA_MAJOR_VERSION\s+)(\d+)([.\s]*)", line
            ):
                oldMajor = int(result.group(2))
                lines[i] = result.group(1) + version[0] + result.group(3)
            elif result := re.search(
                r"^(#define\s+MEGA_MINOR_VERSION\s+)(\d+)([.\s]*)", line
            ):
                oldMinor = int(result.group(2))
                lines[i] = result.group(1) + version[1] + result.group(3)
            elif result := re.search(
                r"^(#define\s+MEGA_MICRO_VERSION\s+)(\d+)([.\s]*)", line
            ):
                oldMicro = int(result.group(2))
                lines[i] = result.group(1) + version[2] + result.group(3)
        print(
            f"Updating version: {oldMajor}.{oldMinor}.{oldMicro} -> ",
            new_version,
            flush=True,
        )

        # validate new version
        major = int(version[0])
        minor = int(version[1])
        micro = int(version[2])
        assert (major > oldMajor) or (
            major == oldMajor
            and (minor > oldMinor or (minor == oldMinor and micro > oldMicro))
        ), f"Invalid version: {oldMajor}.{oldMinor}.{oldMicro} -> {new_version}"

        # write new version
        self._local_repo.version_file.write_text("\n".join(lines))

    def _push_to_new_branch(
        self,
        new_version: str,
        new_branch: str,
        remote_name: str,
    ):
        assert self._local_repo is not None, "Call _prepare_local_repo() first"
        try:
            self._local_repo.commit_changes_to_new_branch(
                f"Update SDK version to {new_version}", new_branch
            )
            print("v Changes committed to", new_branch, flush=True)
            self._local_repo.push_branch(remote_name, new_branch)
            print(f"v Branch {new_branch} pushed", flush=True)
        except AssertionError:
            self._local_repo.clean_version_changes(new_branch, self._private_branch)
            raise

        self._local_repo.clean_version_changes(new_branch, self._private_branch)

    # Merge new branch with changes in version file
    def _merge_local_changes(
        self,
        new_version: str,
        new_branch: str,
    ):
        mr_id, mr_url = self._remote_private_repo.open_mr(
            f"Update SDK version to {new_version}", new_branch, self._private_branch
        )
        if mr_id == 0:
            self._remote_private_repo.delete_branch(new_branch)
            raise ValueError("Failed to open MR with local changes")
        print(f"v MR {mr_id} opened (version upgrade):\n  {mr_url}")
        print("  **** Release process will continue after the MR has been approved.")
        print("       Cancel by manually closing the MR.", flush=True)

        # Send message to chat for MR approval
        if self._slack is not None:
            self._slack.post_message(
                "sdk_devs_only",
                f"Hello @channel,\n\nPlease approve the MR for the new `{self._project_name}` release `{self._new_version}`:\n{mr_url}",
            )

        # MR not approved within the waiting interval will be closed and
        # the process aborted. To abort earlier just close the MR manually.
        if not self._remote_private_repo.merge_mr(
            mr_id, 3600
        ):  # wait 1 h max for approval
            self._remote_private_repo.close_mr(mr_id)
            self._remote_private_repo.delete_branch(new_branch)
            raise ValueError("Failed to merge MR with local changes")
        print("v MR merged for version upgrade", flush=True)

    # STEP 4: Create "release/vX.Y.Z" branch
    def create_release_branch(self):
        self._version_v_prefixed = f"v{self._new_version}"
        self._release_branch = f"release/{self._version_v_prefixed}"

        print("Creating branch", self._release_branch, flush=True)
        self._remote_private_repo.create_branch(
            self._release_branch, self._private_branch
        )
        print("v Created branch", self._release_branch)

    # STEP 5: Create rc tag "vX.Y.Z-rc.1" from branch "release/vX.Y.Z"
    def create_rc_tag(self):
        assert self._remote_private_repo is not None
        assert self._version_v_prefixed is not None
        assert self._release_branch is not None
        self._rc_tag = f"{self._version_v_prefixed}-rc.1"

        print("Creating tag", self._rc_tag, flush=True)
        try:
            self._remote_private_repo.create_tag(self._rc_tag, self._release_branch)
        except Exception:
            self._remote_private_repo.delete_branch(self._release_branch)
            raise
        print("v Created tag", self._rc_tag)

    # STEP 6: Open MR to merge branch "release/vX.Y.Z" into public branch (don't merge)
    def open_mr_for_release_branch(self, public_branch: str):
        assert self._remote_private_repo is not None
        assert self._release_branch is not None
        assert self._rc_tag is not None
        print(
            f"Opening MR to merge {self._release_branch} into {public_branch}",
            flush=True,
        )
        mr_id, _ = self._remote_private_repo.open_mr(
            f"Update SDK version to {self._new_version}",
            self._release_branch,
            public_branch,
            "Release",
        )
        if mr_id == 0:
            self._remote_private_repo.close_mr(mr_id)
            self._remote_private_repo.delete_branch(self._release_branch)
            self._remote_private_repo.delete_tag(self._rc_tag)
            raise ValueError(
                f"Failed to open MR to merge {self._release_branch} into {public_branch}"
            )
        print(f"v Opened MR to merge {self._release_branch} into {public_branch}")
        print(
            "  **** Do NOT merge this MR until the release will be closed (dependent apps are live)!!",
            flush=True,
        )

    # STEP 7: Update and rename previous NextRelease version; create new NextRelease version
    def manage_versions(self, url: str, user: str, password: str, apps: str):
        self._jira = JiraProject(url, user, password, self._project_name)

        self._jira.update_current_version(
            self._new_version,  # i.e. "X.Y.Z"
            apps,  # i.e. "iOS A.B / Android C.D / MEGAsync E.F.G"
        )

        self._jira.create_new_version()

    # STEP 8: Post release notes to Slack
    def post_notes(self, apps: list[str]):
        print("Generating release notes", flush=True)
        assert self._rc_tag is not None
        assert self._jira is not None
        tag_url = self._remote_private_repo.get_tag_url(self._rc_tag)

        notes = self._jira.get_release_notes(self._rc_tag, tag_url, apps)
        if self._slack is None:
            print("\n" + notes, flush=True)
        else:
            self._slack.post_message(self._slack_channel, notes)
            print(f"v Posted release notes to #{self._slack_channel}", flush=True)
