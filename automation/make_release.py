from lib.local_repository import LocalRepository
from lib.release_process import ReleaseProcess
import tomllib
import os
import sys

RELEASE_CANDIDATE_NUMBER = 1

# Check number of arguments
if len(sys.argv) < 2:
    print("Usage: python make_release.py <config_file.toml>")
    sys.exit(1)

config_file = sys.argv[1]

# runtime arguments
with open(config_file, "rb") as f:
    args = tomllib.load(f)["make_release"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    os.environ["GITLAB_TOKEN"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for making a release
release.setup_project_management(
    args["jira_url"],
    args["jira_user"],
    os.environ["JIRA_PASSWORD"],
)
release.set_release_version_to_make(args["release_version"])

if os.environ["SLACK_TOKEN"] and args["slack_channel"]:
    release.setup_chat(os.environ["SLACK_TOKEN"], args["slack_channel"])

if LocalRepository.has_version_file():
    # STEP 3: update version in local file
    release.update_version_in_local_file(
        os.environ["GPG_KEYGRIP"],
        os.environ["GPG_PASSWORD"],
        args["private_remote_name"],
        "task/update-sdk-version",
    )


# STEP 4: Create branch "release/vX.Y.Z"
release.create_release_branch()


# STEP 5: Create rc tag "vX.Y.Z-rc.1" from branch "release/vX.Y.Z"
release.create_rc_tag(RELEASE_CANDIDATE_NUMBER)


# STEP 6: Open MR from branch "release/vX.Y.Z" to public branch (don't merge)
release.open_mr_for_release_branch(args["public_branch"])


# STEP 7: Rename previous NextRelease version; create new NextRelease version
release.manage_versions(
    args["jira_url"],
    args["jira_user"],
    os.environ["JIRA_PASSWORD"],
    args["target_apps"],
)


# STEP 8: Post release notes to Slack
apps = [a.strip() for a in args["target_apps"].split("/")]
release.post_notes(apps)
