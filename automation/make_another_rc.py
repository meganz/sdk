from lib.release_process import ReleaseProcess
import re
import tomllib
import os
import argparse

# Read configuration file path
parser = argparse.ArgumentParser(
    description="Make a new release candidate using the specified config file."
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
]

for var in required_env_vars:
    if os.getenv(var) is None:
        print(f"{var} environment variable is not defined.")

# runtime arguments
with open(args.config_file, "rb") as f:
    args = tomllib.load(f)["make_another_rc"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    os.environ["GITLAB_TOKEN"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for a new RC
release.setup_project_management(
    args["jira_url"], os.environ["JIRA_TOKEN"]
)
slack_token = os.environ.get("SLACK_TOKEN", "")
slack_channel_dev = args.get("slack_channel_dev_requests", "")
slack_channel_announce = args.get("slack_channel_announce", "")
if slack_token and (slack_channel_dev or slack_channel_announce):
    slack_thread_announce = args.get("slack_thread_announce", "")
    release.setup_chat(
        slack_token, slack_channel_dev, slack_channel_announce, slack_thread_announce
    )

assert args["release_version"]  # "1.0.0"
assert args["tickets"]
app_descr = release.set_release_version_for_new_rc(args["release_version"])

# Add Fix Version to tickets
tickets: list[str] = [t.strip() for t in args["tickets"].split(",")]
release.add_fix_version_to_tickets(tickets)


# STEP 1: GitLab: step #5 from make_release:
# Create new RC tag
release.setup_local_repo(args["private_remote_name"], "", "")
last_rc = release.get_last_rc()
release.create_rc_tag(last_rc + 1)


# STEP 2: Slack: step #8 from make_release:
# Post release notes to Slack
if args["target_apps"]:
    apps = [a.strip() for a in args["target_apps"].split("/")]
else:
    apps = [a.strip() for a in app_descr.split("/")]
release.post_notes(apps, releaseType="releaseCandidate", tickets=tickets)
