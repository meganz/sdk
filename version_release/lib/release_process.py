import re
from lib.local_repository import LocalRepository
from lib.private_repository import GitLabRepository
from lib.sign_commits import setup_gpg_signing


class ReleaseProcess:

    def __init__(
        self,
        project_name: str,
        gitlab_token: str,
        private_host_url: str,
        private_branch: str,
    ):
        self._project_name = project_name
        self._gitlab_token = gitlab_token
        self._private_host_url = private_host_url
        self._private_branch = private_branch
        self._local_repo: LocalRepository | None = None
        self._remote_private_repo: GitLabRepository | None = None

    # STEP 2: update version in local file
    def update_version_in_local_file(
        self,
        gpg_keygrip: str,
        gpg_password: str,
        private_remote_name: str,
        private_remote_url: str,
        new_branch: str,
        new_version: str,
    ):
        setup_gpg_signing(gpg_keygrip, gpg_password)
        self._prepare_local_repo(private_remote_name, private_remote_url)
        self._change_version_in_file(new_version)
        self._push_to_new_branch(new_version, new_branch, private_remote_name)
        self._merge_local_changes(new_version, new_branch)

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
        self._remote_private_repo = GitLabRepository(
            self._private_host_url, self._gitlab_token, self._project_name
        )
        mr_id, mr_url = self._remote_private_repo.open_mr(
            f"Update SDK version to {new_version}", new_branch, self._private_branch
        )
        if mr_id == 0:
            self._remote_private_repo.delete_branch(new_branch)
            raise ValueError("Failed to open MR with local changes")
        print(f"v MR {mr_id} opened (version upgrade):\n  {mr_url}", flush=True)

        # TODO: send message to chat for MR approval
        print("  **** Release process will continue after the MR has been approved.")
        print("       Cancel by manually closing the MR.", flush=True)

        # MR not approved within the waiting interval will be closed and
        # the process aborted. To abort earlier just close the MR manually.
        if not self._remote_private_repo.merge_mr(
            mr_id, 3600
        ):  # wait 1 h max for approval
            self._remote_private_repo.close_mr(mr_id)
            self._remote_private_repo.delete_branch(new_branch)
            raise ValueError("Failed to merge MR with local changes")
        print("v MR merged for version upgrade", flush=True)
