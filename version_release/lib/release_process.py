import re
from lib.chat import Slack
from lib.local_repository import LocalRepository
from lib.private_repository import GitLabRepository
from lib.public_repository import GitHubRepository
from lib.sign_commits import setup_gpg_signing
from lib.version_management import JiraProject
from atlassian import Confluence


class ReleaseProcess:

    def __init__(
        self,
        project_name: str,
        gitlab_token: str,
        private_host_url: str,
        private_branch: str,
    ):
        self._private_branch = private_branch
        self._local_repo: LocalRepository | None = None
        self._remote_private_repo = GitLabRepository(
            private_host_url, gitlab_token, project_name
        )
        self._project_name = project_name

    def setup_project_management(self, url: str, user: str, password: str):
        assert self._jira is None
        self._jira = JiraProject(
            url,
            user,
            password,
            self._project_name,
        )

    def set_release_version_to_make(self, version: str):
        assert not self._new_version
        self._new_version = version
        self._version_v_prefixed = f"v{self._new_version}"
        self._jira.setup_release()

    def setup_chat(
        self,
        slack_token: str,
        slack_channel: str,
    ):
        # Chat has 2 purposes:
        # - request approvals for MRs (always in the same channel, only for SDK devs);
        # - make announcements in the given channel, if any.
        self._slack = Slack(slack_token)
        self._slack_channel = slack_channel

    def determine_version_for_next_release(self) -> str:
        assert self._jira is not None
        version = self._jira.get_next_version()
        return ".".join(map(str, version))

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
        assert self._local_repo is None
        self._local_repo = LocalRepository(private_remote_name, private_remote_url)
        self._get_branch_locally(private_remote_name, self._private_branch)
        self._change_version_in_file()
        self._push_to_new_branch(new_branch, private_remote_name)
        self._merge_local_changes(new_branch)

    def _get_branch_locally(
        self,
        remote_name: str,
        branch: str,
    ):
        assert self._local_repo is not None
        self._local_repo.check_for_uncommitted_changes()
        self._local_repo.switch_to_branch(branch)
        self._local_repo.sync_current_branch(remote_name)

    # Edit version file
    def _change_version_in_file(self):
        version = self._new_version.split(".")
        assert len(version) == 3, f"Invalid requested version: {self._new_version}"

        # read old version
        oldMajor = oldMinor = oldMicro = 0
        assert self._local_repo is not None
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
            self._new_version,
            flush=True,
        )

        # validate new version
        major = int(version[0])
        minor = int(version[1])
        micro = int(version[2])
        assert (major > oldMajor) or (
            major == oldMajor
            and (minor > oldMinor or (minor == oldMinor and micro > oldMicro))
        ), f"Invalid version: {oldMajor}.{oldMinor}.{oldMicro} -> {self._new_version}"

        # write new version
        self._local_repo.version_file.write_text("\n".join(lines))

    def _push_to_new_branch(
        self,
        new_branch: str,
        remote_name: str,
    ):
        assert self._local_repo is not None
        try:
            self._local_repo.commit_changes_to_new_branch(
                self._get_mr_title(), new_branch
            )
            print("v Changes committed to", new_branch, flush=True)
            self._local_repo.push_branch(remote_name, new_branch)
            print(f"v Branch {new_branch} pushed", flush=True)
        except AssertionError:
            self._local_repo.clean_version_changes(new_branch, self._private_branch)
            raise

        self._local_repo.clean_version_changes(new_branch, self._private_branch)

    def _get_mr_title(self) -> str:
        return f"Update version to {self._new_version}"

    # Merge new branch with changes in version file
    def _merge_local_changes(
        self,
        new_branch: str,
    ):
        mr_id, mr_url = self._remote_private_repo.open_mr(
            self._get_mr_title(),
            new_branch,
            self._private_branch,
            remove_source=True,
            squash=True,
        )
        if mr_id == 0:
            self._remote_private_repo.delete_branch(new_branch)
            raise ValueError("Failed to open MR with local changes")
        print(f"v MR {mr_id} opened (version upgrade):\n  {mr_url}")
        print("  **** Release process will continue after the MR has been approved.")
        print("       Cancel by manually closing the MR.", flush=True)

        # Send message to chat for MR approval
        self._request_mr_approval(
            f"`{self._project_name}` release `{self._new_version}`:\n{mr_url}"
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

    def _request_mr_approval(self, reason: str):
        if self._slack is None:
            print(
                f"You need to request MR approval yourself because chat is not available,\n{reason}",
                flush=True,
            )
        else:
            self._slack.post_message(
                "sdk-stuff-builders-team",
                # "sdk_devs_only",
                f"Hello @channel,\n\nPlease approve the MR for {reason}",
            )

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
    def create_rc_tag(self, rc_num: int):
        assert self._remote_private_repo is not None
        assert self._version_v_prefixed is not None
        assert self._release_branch is not None
        self._rc_tag = f"{self._version_v_prefixed}-rc.{rc_num}"

        print("Creating tag", self._rc_tag, flush=True)
        try:
            self._remote_private_repo.create_tag(self._rc_tag, self._release_branch)
        except Exception as e:
            self._remote_private_repo.delete_branch(self._release_branch)
            raise e
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
            self._get_mr_title(),
            self._release_branch,
            public_branch,
            remove_source=False,
            squash=False,
            labels="Release",
        )
        if mr_id == 0:
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
        self._jira.update_current_version(
            self._new_version,  # i.e. "X.Y.Z"
            apps,  # i.e. "iOS A.B / Android C.D / MEGAsync E.F.G"
        )

        self._jira.create_new_version()

    # STEP 8: Post release notes to Slack
    def post_notes(self, apps: list[str]):
        print("Generating release notes...", flush=True)
        assert self._rc_tag is not None
        assert self._jira is not None
        tag_url = self._remote_private_repo.get_tag_url(self._rc_tag)

        notes: str = (
            f"\U0001F4E3 \U0001F4E3 *New SDK version  -->  `{self._rc_tag}`* (<{tag_url}|Link>)\n\n"
        ) + self._jira.get_release_notes(apps)
        if not self._slack or not self._slack_channel:
            print("Enjoy:\n\n" + notes, flush=True)
        else:
            self._slack.post_message(self._slack_channel, notes)
            print(f"v Posted release notes to #{self._slack_channel}", flush=True)

    ####################
    ##  Close release
    ####################

    def setup_local_repo(
        self,
        private_remote_name: str,
        private_remote_url: str,
        public_remote_name: str,
        public_remote_url: str,
    ):
        assert self._local_repo is None
        self._local_repo = LocalRepository(private_remote_name, private_remote_url)
        self._local_repo.add_remote(
            public_remote_name, public_remote_url, fetch_is_optional=True
        )

    def setup_public_repo(
        self, public_repo_token: str, public_repo_owner: str, project_name: str
    ):
        self._public_repo = GitHubRepository(
            public_repo_token, public_repo_owner, project_name
        )

    def set_release_version_to_close(self, version: str):
        assert not self._new_version
        self._new_version = version
        self._version_v_prefixed = f"v{self._new_version}"
        self._jira.setup_release(self._version_v_prefixed)

    def confirm_all_earlier_versions_are_closed(self):
        # This could be implemented in multiple ways.
        # Relying on Jira looked fine as it's the last update done when closing a Release.
        assert self._jira is not None, "Init Jira connection first"
        self._jira.earlier_versions_are_closed()

    def setup_wiki(self, url: str, user: str, password: str):
        if url and user and password:
            self._wiki = Confluence(url=url, username=user, password=password)
            user_details = self._wiki.get_user_details_by_username(user)
            if isinstance(user_details, dict):
                self._user_key = user_details["userKey"]

    # STEP 1 (close): GitLab: Create tag "vX.Y.Z" from last commit of branch "release/vX.Y.Z"
    def create_release_tag(self):
        last_commit = self._remote_private_repo.get_last_commit_in_branch(
            self._release_branch
        )
        print("Creating tag", self._version_v_prefixed, flush=True)
        self._remote_private_repo.create_tag(self._version_v_prefixed, last_commit)
        print(
            "v Created tag",
            self._version_v_prefixed,
            "from commit",
            last_commit,
            flush=True,
        )

    # STEP 2 (close): GitLab: Create release "Version X.Y.Z" from tag "vX.Y.Z" plus release notes
    def create_release_in_private_repo(self):
        release_name = f"Version {self._new_version}"
        release_notes = self._jira.get_release_notes([])
        print("Creating release", release_name, flush=True)
        self._remote_private_repo.create_release(
            release_name, self._version_v_prefixed, release_notes
        )
        print("v Created release", release_name, flush=True)

    # STEP 3 (close): GitLab: Merge version upgrade MR into public branch (master)
    def merge_release_changes_into_public_branch(self, public_branch: str):
        mr_id = self._remote_private_repo._get_id_of_open_mr(
            self._get_mr_title(), self._release_branch, public_branch
        )
        assert mr_id > 0
        self._remote_private_repo.merge_mr(mr_id, 3600)  # must not delete source branch

    # STEP 4 (close): local git: Push public branch (master) to public remote (github)
    def push_to_public_repo(
        self, private_remote_name: str, public_branch: str, public_remote_name: str
    ):
        # get "master" branch locally, with latest changes
        self._get_branch_locally(private_remote_name, public_branch)

        # push stuff to public repo
        assert self._local_repo is not None
        self._local_repo.push_branch(  # "master" branch
            public_remote_name, public_branch
        )
        self._local_repo.push_branch(  # "vX.Y.Z" tag
            public_remote_name, self._version_v_prefixed
        )

    # STEP 5 (close): GitHub: Create release in public repo from new tag
    def create_release_in_public_repo(self, version: str):
        self._public_repo.create_release(version, self._jira.get_public_release_notes())

    # STEP 6 (close): Jira: mark version as Released, set release date
    def mark_version_as_released(self):
        assert self._jira is not None, "Init Jira connection first"
        self._jira.update_version_close_release()

    # STEP 7 (close): Confluence: Rotate own name to the end of the list of release captains
    def move_release_captain_last(self, page_id: str):
        if self._user_key is None:
            print("Wiki connection not available, rotate Release Captain yourself !")
        return

        # get page content
        page = self._wiki.get_page_by_id(page_id, expand="body.storage")
        if not isinstance(page, dict):
            print("Wiki page not available, rotate Release Captain yourself !")
            return
        content = page["body"]["storage"]["value"]

        # move current user last
        re_pattern = (
            "<h[1-6]>Release Captain schedule</h[1-6]>.*"
            "<ol(\\s.*?)?>"
            "(<li>.*?</li>)*"
            '(<li><ac:link><ri:user ri:userkey="' + self._user_key + '" />.*?</li>)'
            ".*(</ol>)"
        )
        match = re.search(re_pattern, content)
        if match is None:
            print(
                "Wiki page content missing current user, rotate Release Captain yourself !"
            )
            return

        my_user_start = match.start(3)
        my_user_end = match.end(3)
        list_end_tag_start = match.start(4)
        new_content = (
            content[:my_user_start]
            + content[my_user_end:list_end_tag_start]
            + content[my_user_start:my_user_end]
            + content[list_end_tag_start:]
        )
        self._wiki.update_page(page_id, page["title"], new_content)
        print("v Release Captain rotated")
