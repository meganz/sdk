import argparse
from lib.release_process import ReleaseProcess
from lib.utils import get_mega_env_vars, get_mega_env_var

# runtime arguments
parser = argparse.ArgumentParser()
parser.add_argument(
    "-p",
    "--project-name",
    help="Project name (i.e. SDK)",
    type=str,
    required=True,
)
parser.add_argument(
    "-r",
    "--release-version",
    help="Version to be created and released (i.e. 1.0.0). Autodetect it if missing.",
    type=str,
    default="",
)
parser.add_argument(
    "-l",
    "--private-git-host-url",
    help="URL of private repository (i.e. https://code.foo.bar)",
    type=str,
    required=True,
)
parser.add_argument(
    "-d",
    "--private-git-develop-branch",
    help="Name of private develop branch (default: develop)",
    type=str,
    default="develop",
)
parser.add_argument(
    "-m",
    "--public-git-target-branch",
    help="Name of public target branch (default: master)",
    type=str,
    default="master",
)
parser.add_argument(
    "-n",
    "--no-file-update",
    action=argparse.BooleanOptionalAction,
    help="No file update (i.e. for projects that do not store version in a file)",
)
parser.add_argument(
    "-o",
    "--private-git-remote-name",
    help="Name of private repository's git remote (default: origin). Ignored if -n was given",
    type=str,
    default="origin",
)
parser.add_argument(
    "-j",
    "--project-management-url",
    help="URL of project management tool (i.e. https://jira.foo.bar)",
    type=str,
    required=True,
)
parser.add_argument(
    "-t",
    "--target-apps",
    help='Apps and versions that use this release (i.e. "Android 1.0.1 / iOS 1.2 / MEGAsync 9.9.9")',
    type=str,
    required=True,
)
parser.add_argument(
    "-c",
    "--chat-channel",
    help="Chat channel where release notes will be posted (i.e. sdk). Print to console if missing",
    type=str,
    default="",
)
parser.add_argument(
    "-q",
    "--rc-number",
    help="Release-candidate number (default: 1)",
    type=int,
    default=1,
)
args = parser.parse_args()

# environment variables
mega_env_vars = get_mega_env_vars(
    "MEGA_GITLAB_TOKEN",
    "MEGA_JIRA_USER",
    "MEGA_JIRA_PASSWORD",
)

slack_token = ""
if args.chat_channel == "":
    print("Release notes will be printed to console. Post them yourself.", flush=True)
else:
    slack_token = get_mega_env_var("MEGA_SLACK_TOKEN")
    if slack_token == "":
        print("MEGA_SLACK_TOKEN env var missing")
        print(
            "Release notes will be printed to console. Post them yourself.", flush=True
        )

# create Release process and do common init
release = ReleaseProcess(
    args.project_name,
    mega_env_vars["MEGA_GITLAB_TOKEN"],
    args.private_git_host_url,
    args.private_git_develop_branch,
)

# prerequisites for making a release
release.setup_project_management(
    args.project_management_url,
    mega_env_vars["MEGA_JIRA_USER"],
    mega_env_vars["MEGA_JIRA_PASSWORD"],
)
next_release_version = (
    args.release_version or release.determine_version_for_next_release()
)
release.set_release_version_to_make(next_release_version)

if slack_token:
    release.setup_chat(slack_token, args.chat_channel)

if not args.no_file_update:
    # STEP 3: update version in local file
    mega_env_vars |= get_mega_env_vars(
        "MEGA_GPG_KEYGRIP",
        "MEGA_GPG_PASSWORD",
    )

    release.update_version_in_local_file(
        mega_env_vars["MEGA_GPG_KEYGRIP"],
        mega_env_vars["MEGA_GPG_PASSWORD"],
        args.private_git_remote_name,
        "task/update-sdk-version",
    )


# STEP 4: Create branch "release/vX.Y.Z"
release.create_release_branch()


# STEP 5: Create rc tag "vX.Y.Z-rc.1" from branch "release/vX.Y.Z"
release.create_rc_tag(args.rc_number)


# STEP 6: Open MR from branch "release/vX.Y.Z" to public branch (don't merge)
release.open_mr_for_release_branch(args.public_git_target_branch)


# STEP 7: Rename previous NextRelease version; create new NextRelease version
release.manage_versions(
    args.project_management_url,
    mega_env_vars["MEGA_JIRA_USER"],
    mega_env_vars["MEGA_JIRA_PASSWORD"],
    args.target_apps.strip('"'),
)


# STEP 8: Post release notes to Slack
apps = [a.strip() for a in args.target_apps.strip('"').split("/")]
release.post_notes(apps)
