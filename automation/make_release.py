from lib.local_repository import LocalRepository
from lib.release_process import ReleaseProcess
import tomllib
import os
import argparse

RELEASE_CANDIDATE_NUMBER = 1

# Read configuration file path
parser = argparse.ArgumentParser(
    description="Make a release using the specified config file."
)
parser.add_argument(
    "config_file", type=str, help="Path to the configuration file (TOML)"
)
args = parser.parse_args()

# Check for required environment variables
required_env_vars = [
    "GITLAB_TOKEN",
    "JIRA_TOKEN",
    "SLACK_TOKEN",
    "GPG_KEYGRIP",
    "GPG_PASSWORD",
]

for var in required_env_vars:
    if os.getenv(var) is None:
        print(f"{var} environment variable is not defined.")

# runtime arguments
with open(args.config_file, "rb") as f:
    args = tomllib.load(f)["make_release"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    os.environ["GITLAB_TOKEN"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for making a release
release.setup_project_management(args["jira_url"], os.environ["JIRA_TOKEN"])
release.set_release_version_to_make(args["release_version"])

slack_token = os.environ.get("SLACK_TOKEN", "")
slack_channel_dev = args.get("slack_channel_dev_requests", "")
slack_channel_announce = args.get("slack_channel_announce", "")
if slack_token and (slack_channel_dev or slack_channel_announce):
    slack_thread_announce = args.get("slack_thread_announce", "")
    release.setup_chat(
        slack_token, slack_channel_dev, slack_channel_announce, slack_thread_announce
    )

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
release.manage_versions(args["target_apps"])


# STEP 8: Post release notes to Slack
apps = [a.strip() for a in args["target_apps"].split("/")]
release.post_notes(apps)
