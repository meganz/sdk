from lib.local_repository import LocalRepository
from lib.release_process import ReleaseProcess
import tomllib

# runtime arguments
with open("config.toml", "rb") as f:
    args = tomllib.load(f)["patch_release"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    args["gitlab_token"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for patching a release
release.setup_local_repo(args["private_remote_name"], "", "")

release.setup_project_management(
    args["jira_url"],
    args["jira_user"],
    args["jira_password"],
)

if args["slack_token"] and args["slack_channel"]:
    release.setup_chat(args["slack_token"], args["slack_channel"])

assert args["tickets"]


# Validation: Jira, GitLab: Validate new and previous versions
version = args["release_version"]  # "1.0.1"
app_descr = release.set_release_version_after_patch(version)


# STEP 7: Jira: Manage versions
# Create new release
#   Name: vX.Y.Z (same as for new release branch)
#   Start date: today
#   Description: copy from version being patched
# Set the new release as Fix Version for the included ticket(s).
new_release_id = release.create_new_for_patch(app_descr)
tickets: list[str] = [t.strip() for t in args["tickets"].split(",")]
release.add_fix_version_to_tickets(tickets)


if LocalRepository.has_version_file():
    # STEP 8: local git, GitLab: update version in local file
    release.update_version_in_local_file_from_branch(
        args["gpg_keygrip"],
        args["gpg_password"],
        args["private_remote_name"],
        "task/update-sdk-version",
        release.get_new_release_branch(),
    )

# STEP 9: step #5 from make_release: GitLab:
# Create a new release candidate (rc) tag with the format vX.Y.Z-rc.1 from the release/vX.Y.Z branch.
release.create_rc_tag(1)


# STEP 10: step #8 from make_release: Slack:
# Post release notes to Slack
apps = [a.strip() for a in app_descr.split("/")]
release.post_notes(apps)
