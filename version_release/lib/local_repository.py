from pathlib import Path
import re, subprocess


class LocalRepository:  # use raw git commands
    """
    Functionality:
    - check private remote being configured
    - check for uncommitted changes
    - switch to a branch
    - update current branch from remote
    - commit changes to a new branch
    - push a branch to a remote
    - delete a local branch
    """

    version_file = Path(__file__).parent.parent.parent / "include" / "mega" / "version.h"

    def __init__(self, remote_name: str, remote_url: str):
        # confirm remote being correctly configured
        self.add_remote(remote_name, remote_url, fetch_is_optional=False)

    @staticmethod
    def has_version_file() -> bool:
        return LocalRepository.version_file.is_file()

    def add_remote(
        self, remote_name: str, remote_url: str, fetch_is_optional: bool
    ) -> bool:
        # add remote or confirm being correctly configured already
        byte_output = subprocess.check_output(
            ["git", "remote", "-v"], stderr=subprocess.STDOUT
        )
        remotes = byte_output.decode("utf-8").splitlines()
        assert isinstance(remotes, list), f"Error:\n  git remote -v\n  {remotes}"

        escaped_url = re.escape(remote_url)
        remote_push = remote_fetch = False

        for r in remotes:
            if re.match(rf"{remote_name}\s+{escaped_url}\s+\(push\)", r):
                remote_push = True
            elif re.match(rf"{remote_name}\s+{escaped_url}\s+\(fetch\)", r):
                remote_fetch = True

        if remote_push and (remote_fetch or fetch_is_optional):
            return True

        if not remote_push and not remote_fetch:
            assert subprocess.run(
                ["git", "remote", "add", remote_name, remote_url],
                stdout=subprocess.DEVNULL,
            ), f"Failed to add remote {remote_name} {remote_url}"
            print("  Added remote\n  ", remote_name, "\t", remote_url, flush=True)
            return True

        assert (
            remote_push
        ), f"{remote_name} {remote_url} (push): NOT FOUND\nfound:\n{remotes}"
        assert (
            remote_fetch
        ), f"{remote_name} {remote_url} (fetch): NOT FOUND\nfound:\n{remotes}"
        return False

    def check_for_uncommitted_changes(self):
        byte_output = subprocess.check_output(["git", "diff", "--shortstat"])
        assert (
            len(byte_output) == 0
        ), f'Found unstaged changes:\n{byte_output.decode("utf-8")}'

        byte_output = subprocess.check_output(
            ["git", "diff", "--shortstat", "--cached"]
        )
        assert (
            len(byte_output) == 0
        ), f'Found staged changes:\n{byte_output.decode("utf-8")}'

    def _get_current_branch(self) -> str:
        byte_output = subprocess.check_output(["git", "branch", "--show-current"])
        branch_name = byte_output.decode("utf-8").strip()
        return branch_name

    def switch_to_branch(self, target_branch: str):
        cb = self._get_current_branch()
        if cb != target_branch:
            print(f"Switching to branch {target_branch} from {cb}...", flush=True)
            subprocess.run(["git", "switch", target_branch])
            assert (
                self._get_current_branch() == target_branch
            ), f"Failed to switch to branch {target_branch}"

    def sync_current_branch(self, remote: str):
        my_branch = self._get_current_branch()

        assert subprocess.run(
            ["git", "fetch"], stdout=subprocess.DEVNULL
        ), f"Failed to fetch {remote}/{my_branch}"

        byte_output = subprocess.check_output(
            [
                "git",
                "rev-list",
                "--left-right",
                "--count",
                f"{remote}/{my_branch}...{my_branch}",
            ]
        )
        behind_or_ahead = byte_output.decode("utf-8").split()

        assert int(behind_or_ahead[1]) == 0, (  # true here means ahead
            # local branch is ahead, has local-only changes
            # let a human take action
            f"{my_branch} is ahead by {behind_or_ahead[1]} commits"
        )

        if int(behind_or_ahead[0]):  # true here means behind
            # local branch is behind; pull latest changes
            assert subprocess.run(
                ["git", "pull", remote, my_branch], stdout=subprocess.DEVNULL
            ), f"Failed to pull from {remote}/{my_branch}"

    def commit_changes_to_new_branch(self, commit_message: str, branch_name: str):
        # branch should not exist
        assert subprocess.run(
            ["git", "show-ref", "--quiet", f"refs/heads/{branch_name}"],
            stdout=subprocess.DEVNULL,
        ), f'Branch "{branch_name}" already existed. Delete it and try again'

        # create branch
        assert subprocess.run(
            ["git", "checkout", "-b", branch_name], stdout=subprocess.DEVNULL
        ), f'Failed to create new branch "{branch_name}"'

        # stage changes
        assert subprocess.run(
            ["git", "add", "-u"], stdout=subprocess.DEVNULL
        ), "Failed to stage changes"

        # commit and sign
        assert subprocess.run(
            ["git", "commit", "-S", "-m", commit_message], stdout=subprocess.DEVNULL
        ), (
            # TODO: this can also fail if GPG signing failed
            "Failed to commit changes"
        )

    def push_branch(self, remote: str, branch: str):
        assert subprocess.run(
            ["git", "push", remote, branch], stdout=subprocess.DEVNULL
        ), f"Failed to push branch {branch} to {remote}"

    def clean_version_changes(self, new_branch: str, fallback_branch: str):
        subprocess.run(["git", "reset"], stdout=subprocess.DEVNULL)
        subprocess.run(
            ["git", "checkout", "--", self.version_file.as_posix()],
            stdout=subprocess.DEVNULL,
        )
        subprocess.run(["git", "checkout", fallback_branch], stdout=subprocess.DEVNULL)
        subprocess.run(["git", "branch", "-D", new_branch], stdout=subprocess.DEVNULL)
