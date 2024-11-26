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
        self._new_version: str | None = None
        self._private_branch = private_branch
        self._jira: JiraProject | None = None
        self._local_repo: LocalRepository | None = None
        print("GitLab initializing", flush=True)
        self._remote_private_repo = GitLabRepository(
            private_host_url, gitlab_token, project_name
        )
        print("v GitLab initialized", flush=True)
        self._project_name = project_name
        self._version_v_prefixed = ""

    def setup_project_management(self, url: str, token: str):
        assert self._jira is None
        print("Jira initializing", flush=True)
        self._jira = JiraProject(
            url,
            token,
            self._project_name,
        )
        print("v Jira initialized", flush=True)

    def set_release_version_to_make(self, version: str):
        assert self._jira is not None
        self._jira.setup_release()
        if not version:
            v = self._jira.get_next_version()
            version = ".".join(map(str, v))
        self._new_version = version
        self._version_v_prefixed = f"v{self._new_version}"

    def setup_chat(
        self,
        slack_token: str,
        slack_channel_dev: str,
        slack_channel_announce: str = "",
        slack_thread_announce: str = "",
    ):
        # Chat has 2 purposes:
        # - request approvals for MRs (always in the same channel, only for SDK devs);
        # - make announcements in the given channel, if any.
        print("Slack initializing", flush=True)
        self._slack = Slack(slack_token)
        self._slack_channel_dev_requests = slack_channel_dev
        self._slack_channel_announce = slack_channel_announce
        self._slack_thread_announce = slack_thread_announce
        print("v Slack initialized", flush=True)

    # STEP 3: update version in local file
    def update_version_in_local_file(
        self,
        gpg_keygrip: str,
        gpg_password: str,
        private_remote_name: str,
        new_branch: str,
    ):
        setup_gpg_signing(gpg_keygrip, gpg_password)
        assert self._local_repo is None
        private_remote_url = self._remote_private_repo.get_url_to_private_repo()
        self._local_repo = LocalRepository(private_remote_name, private_remote_url)
        self._get_branch_locally(private_remote_name, self._private_branch)
        self._change_version_in_file()
        self._push_to_new_branch(new_branch, private_remote_name)
        self._merge_local_changes(new_branch, self._private_branch)

    def _get_branch_locally(
        self,
        remote_name: str,
        branch: str,
    ):
        assert self._local_repo is not None
        self._local_repo.check_for_uncommitted_changes()
        self._local_repo.switch_to_branch(remote_name, branch)
        self._local_repo.sync_current_branch(remote_name)

    # Edit version file
    def _change_version_in_file(self):
        assert self._new_version is not None
        version = self._new_version.split(".")
        assert len(version) == 3, f"Invalid requested version: {self._new_version}"

        # read old version
        oldMajor = oldMinor = oldMicro = 0
        assert LocalRepository.has_version_file()
        lines = LocalRepository.version_file.read_text().splitlines()
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
        LocalRepository.version_file.write_text("\n".join(lines))

    # Commit changes in version file
    def _push_to_new_branch(
        self,
        new_branch: str,
        remote_name: str,
    ):
        assert self._local_repo is not None
        try:
            self._local_repo.commit_changes_to_new_branch(
                self._get_mr_title_for_version_update(), new_branch
            )
            print("v Changes committed to", new_branch, flush=True)
            self._local_repo.push_branch(remote_name, new_branch)
            print(f"v Branch {new_branch} pushed", flush=True)
        except AssertionError:
            self._local_repo.clean_version_changes(new_branch, self._private_branch)
            raise

        self._local_repo.clean_version_changes(new_branch, self._private_branch)

    def _get_mr_title_for_version_update(self) -> str:
        return f"Update version to {self._new_version}"

    def _get_mr_title_for_release(self) -> str:
        return f"Release {self._new_version}"

    # Merge new branch with changes in version file
    def _merge_local_changes(
        self,
        new_branch: str,
        target_branch: str,
    ):
        mr_id, mr_url = self._remote_private_repo.open_mr(
            self._get_mr_title_for_version_update(),
            new_branch,
            target_branch,
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
        if self._slack is None or not self._slack_channel_dev_requests:
            print(
                f"You need to request MR approval yourself because chat is not available,\n{reason}",
                flush=True,
            )
        else:
            self._slack.post_message(
                self._slack_channel_dev_requests,
                "",
                f"Hello <!channel>,\n\nPlease approve the MR for {reason}",
            )

    # STEP 4: Create "release/vX.Y.Z" branch
    def create_release_branch(self):
        self._version_v_prefixed = f"v{self._new_version}"
        release_branch = self.get_new_release_branch()

        print("Creating branch", release_branch, flush=True)
        self._remote_private_repo.create_branch(release_branch, self._private_branch)
        print("v Created branch", release_branch)

    # STEP 5: Create rc tag "vX.Y.Z-rc.1" from branch "release/vX.Y.Z"
    def create_rc_tag(self, rc_num: int):
        assert self._remote_private_repo is not None
        assert self._version_v_prefixed
        self._rc_tag = f"{self._version_v_prefixed}-rc.{rc_num}"

        print("Creating tag", self._rc_tag, flush=True)
        try:
            self._remote_private_repo.create_tag(
                self._rc_tag, self.get_new_release_branch()
            )
        except Exception as e:
            print(
                "Creating tag",
                self._rc_tag,
                "for branch",
                self.get_new_release_branch(),
                "failed",
                flush=True,
            )
            raise e
        print("v Created tag", self._rc_tag)

    # STEP 6: Open MR to merge branch "release/vX.Y.Z" into public branch (don't merge)
    def open_mr_for_release_branch(self, public_branch: str):
        assert self._remote_private_repo is not None
        assert self._rc_tag is not None
        release_branch = self.get_new_release_branch()
        print(
            f"Opening MR to merge {release_branch} into {public_branch}",
            flush=True,
        )
        mr_id, _ = self._remote_private_repo.open_mr(
            self._get_mr_title_for_release(),
            release_branch,
            public_branch,
            remove_source=False,
            squash=False,
            labels="Release",
        )
        if mr_id == 0:
            self._remote_private_repo.delete_branch(release_branch)
            self._remote_private_repo.delete_tag(self._rc_tag)
            raise ValueError(
                f"Failed to open MR to merge {release_branch} into {public_branch}"
            )
        print(f"v Opened MR to merge {release_branch} into {public_branch}")
        print(
            "  **** Do NOT merge this MR until the release will be closed (dependent apps are live)!!",
            flush=True,
        )

    # STEP 7: Update and rename previous NextRelease version; create new NextRelease version
    def manage_versions(self, apps: str):
        assert self._new_version is not None
        assert self._jira is not None
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
            f"\U0001F4E3 \U0001F4E3 *New {self._project_name} version  -->  `{self._rc_tag}`* (<{tag_url}|Link>)\n\n"
        ) + self._jira.get_release_notes_for_slack(apps)
        if not self._slack or not self._slack_channel_announce:
            print("Enjoy:\n\n" + notes, flush=True)
        else:
            self._slack.post_message(
                self._slack_channel_announce, self._slack_thread_announce, notes
            )
            print(
                f"v Posted release notes to #{self._slack_channel_announce}", flush=True
            )

    ####################
    ##  Close release
    ####################

    def setup_local_repo(
        self,
        private_remote_name: str,
        public_remote_name: str,
        public_remote_url: str,
    ):
        print("Local Git repo initializing", flush=True)
        assert self._local_repo is None
        private_remote_url = self._remote_private_repo.get_url_to_private_repo()
        self._local_repo = LocalRepository(private_remote_name, private_remote_url)
        if public_remote_name and public_remote_url:
            self._local_repo.add_remote(
                public_remote_name, public_remote_url, fetch_is_optional=True
            )
        print("v Local Git repo initialized", flush=True)

    def setup_public_repo(self, public_repo_token: str, public_repo_owner: str):
        print("GitHub initializing", flush=True)
        self._public_repo = GitHubRepository(
            public_repo_token, public_repo_owner, self._project_name
        )
        print("v GitHub initialized", flush=True)

    def set_release_version_to_close(self, version: str):
        assert not self._new_version
        self._new_version = version
        self._version_v_prefixed = f"v{self._new_version}"
        self._release_branch = f"release/{self._version_v_prefixed}"
        assert self._jira is not None
        self._jira.setup_release(self._version_v_prefixed)

    def get_release_type_to_close(self, public_branch: str):
        assert self._jira is not None, "Init Jira connection first"
        mr_id, _ = self._remote_private_repo._get_open_mr(
            self._get_mr_title_for_release(),
            self.get_new_release_branch(),
            public_branch,
        )
        if mr_id == 0:
            return "hotfix"

        if self._jira.earlier_versions_are_closed():
            return "new_release"

        return "old_release"

    def confirm_all_earlier_versions_are_closed(self):
        # This could be implemented in multiple ways.
        # Relying on Jira looked fine as it's the last update done when closing a Release.
        assert self._jira is not None, "Init Jira connection first"
        self._jira.earlier_versions_are_closed()

    def setup_wiki(self, url: str, token: str):
        if url and token:
            print("Confluence initializing", flush=True)
            self._wiki = Confluence(url=url, token=token)
            print("v Confluence configured", flush=True)

    # STEP 1 (close): GitLab: Create tag "vX.Y.Z" from last commit of branch "release/vX.Y.Z"
    def create_release_tag(self):
        last_commit = self._remote_private_repo.get_last_commit_in_branch(
            self.get_new_release_branch()
        )
        print("Creating tag", self._version_v_prefixed, flush=True)
        assert (
            self._release_branch
        ), "Check that set_release_version_to_close() was called"
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
        assert self._jira is not None
        release_notes = self._jira.get_release_notes_for_gitlab([])
        print("Creating release", release_name, flush=True)
        self._remote_private_repo.create_release(
            release_name, self._version_v_prefixed, release_notes
        )
        print("v Created release", release_name, flush=True)

    # STEP 3 (close): GitLab, Slack: Merge version upgrade MR into public branch (master)
    def merge_release_changes_into_public_branch(self, public_branch: str):
        mr_id, mr_url = self._remote_private_repo._get_open_mr(
            self._get_mr_title_for_release(),
            self.get_new_release_branch(),
            public_branch,
        )
        assert mr_id > 0
        self._request_mr_approval(
            f"`{self._project_name}` close `{self._new_version}`:\n{mr_url}"
        )
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

    # STEP 4 (close): local git: Push release branch (release/vX.Y.Z) to public remote (github)
    def push_release_branch_to_public_repo(
        self, private_remote_name: str, public_remote_name: str
    ):
        release_branch = self.get_new_release_branch()

        # get hotfix branch locally, with latest changes
        self._get_branch_locally(private_remote_name, release_branch)

        # push stuff to public repo
        assert self._local_repo is not None
        self._local_repo.push_branch(public_remote_name, release_branch)
        self._local_repo.push_branch(
            public_remote_name, self._version_v_prefixed
        )  # "vX.Y.Z" tag

    # STEP 5 (close): GitHub: Create release in public repo from new tag
    def create_release_in_public_repo(self, version: str):
        assert self._jira is not None
        self._public_repo.create_release(
            version, self._jira.get_release_notes_for_github()
        )

    # STEP 6 (close): Jira: mark version as Released, set release date
    def mark_version_as_released(self):
        assert self._jira is not None, "Init Jira connection first"
        self._jira.update_version_close_release()

    # STEP 7 (close): Confluence: Rotate the first name to the end of the list of release captains
    def move_release_captain_last(self, page_id: str):
        if self._wiki is None:
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
            "<h[1-6]>Release Captain schedule</h[1-6]>.*?"
            "<ol(\\s.*?)?>"   # Opening tag
            "(<li>.*?</li>)"  # Match first item in the list. Current captain
            ".*?(</ol>)"      # Skip the rest of the list and matches closing tag.
        )
        match = re.search(re_pattern, content)
        if match is None:
            print(
                "Wiki page content missing current user, rotate Release Captain yourself !"
            )
            return

        completeMatch = match.group(0)
        captain = match.group(2)
        closingTag = match.group(3)

        new_content = (
            content[:match.start(0)]
            + completeMatch.replace(captain, "").replace(closingTag, captain)
            + closingTag
            + content[match.end(0):]
        )
        self._wiki.update_page(page_id, page["title"], new_content)
        print("v Release Captain rotated")

    ####################
    ##  Patch release
    ####################

    # Validation (patch): Jira, GitLab: Validate new and previous versions
    def set_release_version_after_patch(self, version: str) -> str:
        # validate the new version, ensure it didn't already exist
        major, minor, micro = (int(n) for n in version.split("."))
        assert micro > 0, f"Patched version must be higher than {version}"
        assert self._jira is not None
        [exists, _, _] = self._jira.get_version_info(version)
        assert not exists, f"Version {version} already exists!"

        # validate previous version, before patch
        previous_version = f"{major}.{minor}.{micro - 1}"
        [exists, was_released, app_descr] = self._jira.get_version_info(
            previous_version
        )
        assert exists, f"Could not find version {previous_version} before patch"
        assert was_released, "Attempting to patch a non-released version (RC)"

        assert not self._new_version
        self._new_version = version
        self._version_v_prefixed = f"v{self._new_version}"

        return app_descr

    def get_new_release_branch(self) -> str:
        assert self._version_v_prefixed
        return f"release/{self._version_v_prefixed}"

    # STEP 7 (patch): Jira: Manage versions
    def create_new_for_patch(self, for_apps: str):
        assert self._jira
        assert self._version_v_prefixed
        self._jira.create_new_version_for_patch(self._version_v_prefixed, for_apps)

    def add_fix_version_to_tickets(self, tickets: list[str]):
        assert self._jira
        self._jira.add_fix_version_to_tickets(tickets)

    # STEP 8 (patch): local git, GitLab, Slack: Update version in local file
    def update_version_in_local_file_from_branch(
        self,
        gpg_keygrip: str,
        gpg_password: str,
        private_remote_name: str,
        new_branch: str,
        target_branch: str,
    ):
        setup_gpg_signing(gpg_keygrip, gpg_password)
        self._get_branch_locally(private_remote_name, target_branch)
        self._change_version_in_file()
        assert self._local_repo is not None
        self._local_repo.commit_changes_to_new_branch(
            self._get_mr_title_for_version_update(), new_branch
        )
        self._local_repo.push_branch(private_remote_name, new_branch)
        print(f"v Branch {new_branch} pushed", flush=True)
        self._merge_local_changes(new_branch, target_branch)

    ####################
    ##  RC
    ####################

    def set_release_version_for_new_rc(self, version: str):
        assert not self._new_version
        assert self._jira is not None
        self._jira.setup_release(self._version_v_prefixed)
        [exists, was_released, app_descr] = self._jira.get_version_info(version)
        assert exists, f"Could not find version {version}, for a new RC"
        assert not was_released, "Cannot make a new RC for a released version"

        self._new_version = version
        self._version_v_prefixed = f"v{self._new_version}"

        return app_descr

    def create_branch_from_last_rc(self, remote_name: str, branch_name: str) -> int:
        rc = self._remote_private_repo.get_last_rc(self._version_v_prefixed)
        assert rc, f"No RC found for version {self._new_version}"

        print("Creating branch", branch_name, flush=True)
        self._remote_private_repo.create_branch(
            branch_name, f"{self._version_v_prefixed}-rc.{rc}"
        )
        print("v Created branch", branch_name, flush=True)
        self._get_branch_locally(remote_name, branch_name)
        print(f"Current branch is now {branch_name}.", flush=True)

        return rc

    def wait_for_local_changes_to_be_applied(self) -> bool:
        print("Apply changes locally.", flush=True)
        user_feedback = ""
        while user_feedback not in ("DONE!", "Cancel"):
            user_feedback = input(
                'Type "DONE!" or "Cancel" here when done and hit Enter: '
            )
        if user_feedback == "DONE!":
            return True

        print("Process canceled", flush=True)
        return False

    def push_branch(self, remote_name: str, branch_name: str):
        print("Pushing branch", branch_name, flush=True)
        assert self._local_repo is not None
        self._local_repo.push_branch(remote_name, branch_name)
        print("v Pushed branch", branch_name, flush=True)

    def open_private_mr(
        self,
        source_branch: str,
        target_branch: str,
        description: str,
        remove_source: bool,
    ) -> int:
        print(
            f"Opening MR to merge {source_branch} into {target_branch}",
            flush=True,
        )
        mr_id, mr_url = self._remote_private_repo.open_mr(
            description,
            source_branch,
            target_branch,
            remove_source,
            squash=False,
        )
        assert mr_id, f"Failed to open MR to merge {source_branch} into {target_branch}"
        print(f"v Opened MR to merge {source_branch} into {target_branch}", flush=True)

        # Request MR approval
        self._request_mr_approval(
            f"`{self._project_name}` patch `{source_branch}` to {target_branch}:\n{mr_url}"
        )

        return mr_id

    def merge_private_mr(self, mr_id: int):
        assert self._remote_private_repo.merge_mr(mr_id, 3600)
