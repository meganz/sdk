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
    "JIRA_USERNAME",
    "JIRA_PASSWORD",
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
release.setup_project_management(
    args["jira_url"],
    os.environ["JIRA_USERNAME"],
    os.environ["JIRA_PASSWORD"],
)
release.set_release_version_to_make(args["release_version"])

if os.environ["SLACK_TOKEN"] and args["slack_channel_announce"]:
    release.setup_chat(os.environ["SLACK_TOKEN"], args["slack_channel_announce"])

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
    os.environ["JIRA_USERNAME"],
    os.environ["JIRA_PASSWORD"],
    args["target_apps"],
)


# STEP 8: Post release notes to Slack
apps = [a.strip() for a in args["target_apps"].split("/")]
release.post_notes(apps)
