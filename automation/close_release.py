from lib.release_process import ReleaseProcess
import tomllib
import os
import argparse

# Read configuration file path
parser = argparse.ArgumentParser(
    description="Close a release using the specified config file."
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
    "GITHUB_TOKEN",
    "CONFLUENCE_TOKEN",
]

for var in required_env_vars:
    if os.getenv(var) is None:
        print(f"{var} environment variable is not defined.")

# runtime arguments
with open(args.config_file, "rb") as f:
    args = tomllib.load(f)["close_release"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    os.environ["GITLAB_TOKEN"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for closing a release
release.setup_project_management(
    args["jira_url"],
    os.environ["JIRA_TOKEN"],
)
release.set_release_version_to_close(args["release_version"])
type_of_release = release.get_release_type_to_close(args["public_branch"])

release.setup_local_repo(
    args["private_remote_name"],
    args["public_remote_name"],
    args["github_push_remote_url"],
)
release.setup_public_repo(os.environ["GITHUB_TOKEN"], args["github_repo_owner"])
release.confirm_all_earlier_versions_are_closed()
slack_token = os.environ.get("SLACK_TOKEN", "")
slack_channel_dev = args.get("slack_channel_dev_requests", "")
if slack_token and slack_channel_dev:
    release.setup_chat(slack_token, slack_channel_dev)
release.setup_wiki(
    args["confluence_url"],
    os.environ["CONFLUENCE_TOKEN"],
)

# STEP 1: GitLab: Create tag "vX.Y.Z" from last commit of branch "release/vX.Y.Z"
release.create_release_tag()

# STEP 2: GitLab: Create release "Version X.Y.Z" from tag "vX.Y.Z" plus release notes
release.create_release_in_private_repo()

if type_of_release == "new_release":
    # STEP 3: GitLab, Slack: Merge version upgrade MR into public branch (master)
    release.merge_release_changes_into_public_branch(args["public_branch"])

    # STEP 4: local git: Push public branch (master) to public remote (github)
    release.push_to_public_repo(
        args["private_remote_name"],
        args["public_branch"],
        args["public_remote_name"],
    )

elif type_of_release == "hotfix" or type_of_release == "old_release":
    # STEP 4: local git: Push release branch (release/vX.Y.Z) to public remote (github)
    release.push_release_branch_to_public_repo(
        args["private_remote_name"], args["public_remote_name"]
    )

# STEP 5: GitHub: Create release in public repo from new tag
release.create_release_in_public_repo(args["release_version"])

# STEP 6: Jira: mark version as Released, set release date
release.mark_version_as_released()

# STEP 7: Confluence: Rotate the first name to the end of the list of release captains
# Disabled, we roteate the release captain once a month now
# release.move_release_captain_last(args["confluence_page_id"])
