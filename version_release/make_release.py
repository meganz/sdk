import argparse
from lib.utils import get_mega_env_vars
from lib.release_process import ReleaseProcess


# runtime arguments
parser = argparse.ArgumentParser()
parser.add_argument(
    "-r",
    "--release-version",
    help="Version to be created and released (i.e. 1.0.0)",
    required=True,
)
parser.add_argument(
    "-p",
    "--project-name",
    help="Project name (i.e. SDK)",
    required=True,
)
parser.add_argument(
    "-l",
    "--private-git-host-url",
    help="URL of private repository (i.e. https://foo.bar)",
    required=True,
)
parser.add_argument(
    "-d",
    "--private-git-develop-branch",
    help="Name of private develop branch (i.e. develop)",
    required=True,
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
    help="Name of private repository's git remote (i.e. origin). Ignored if -n was given",
)
parser.add_argument(
    "-u",
    "--private-git-remote-url",
    help="URL of private repository's git remote (i.e. git@foo.bar:proj:proj.git). Ignored if -n was given",
)
args = parser.parse_args()

if not args.no_file_update:
    assert args.private_git_remote_name is not None, "  -o argument missing"
    assert args.private_git_remote_url is not None, "  -u argument missing"

# environment variables
mega_env_vars = get_mega_env_vars(
    "MEGA_GITLAB_TOKEN",
)


# start Release process
release = ReleaseProcess(
    args.project_name,
    mega_env_vars["MEGA_GITLAB_TOKEN"],
    args.private_git_host_url,
    args.private_git_develop_branch,
)


# STEP 2: update version in local file
release.update_version_in_local_file(
    mega_env_vars["MEGA_GPG_KEYGRIP"],
    mega_env_vars["MEGA_GPG_PASSWORD"],
    args.private_git_remote_name,
    args.private_git_remote_url,
    "task/update-sdk-version",
    args.release_version,
)
