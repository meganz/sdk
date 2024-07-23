from lib.local_repository import LocalRepository
from lib.release_process import ReleaseProcess
import tomllib

# runtime arguments
with open("config.toml", "rb") as f:
    args = tomllib.load(f)["make_release"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    args["gitlab_token"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for making a release
release.setup_project_management(
    args["jira_url"],
    args["jira_user"],
    args["jira_password"],
)
next_release_version = (
    args["release_version"] or release.determine_version_for_next_release()
)
release.set_release_version_to_make(next_release_version)

if args["slack_token"]:
    release.setup_chat(args["slack_token"], args["slack_channel"])

if LocalRepository.has_version_file():
    # STEP 3: update version in local file
    release.update_version_in_local_file(
        args["gpg_keygrip"],
        args["gpg_password"],
        args["private_remote_name"],
        "task/update-sdk-version",
    )


# STEP 4: Create branch "release/vX.Y.Z"
release.create_release_branch()


# STEP 5: Create rc tag "vX.Y.Z-rc.1" from branch "release/vX.Y.Z"
release.create_rc_tag(args["release_candidate"])


# STEP 6: Open MR from branch "release/vX.Y.Z" to public branch (don't merge)
release.open_mr_for_release_branch(args["public_branch"])


# STEP 7: Rename previous NextRelease version; create new NextRelease version
release.manage_versions(
    args["jira_url"],
    args["jira_user"],
    args["jira_password"],
    args["target_apps"],
)


# STEP 8: Post release notes to Slack
apps = [a.strip() for a in args["target_apps"].split("/")]
release.post_notes(apps)
