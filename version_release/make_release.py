from argparse import ArgumentParser
from lib.utils import get_mega_env_vars
from lib.release_process import ReleaseProcess


# runtime arguments
parser = ArgumentParser()
parser.add_argument(
    "-r", "--release-version", help="Version to be created and released (i.e. 1.0.0)"
)
parser.add_argument("-p", "--project-name", help="Project name (i.e. SDK)")
parser.add_argument(
    "-l", "--private-git-url", help="URL of private repository (i.e. https://foo.bar)"
)
parser.add_argument(
    "-o",
    "--private-git-remote-name",
    help="Name of private repository git remote (i.e. origin)",
)
parser.add_argument(
    "-u",
    "--private-git-remote-url",
    help="URL of private repository git remote (i.e. git@foo.bar:proj:proj.git)",
)
parser.add_argument(
    "-d",
    "--private-git-target-branch",
    help="Name of private git target branch (i.e. develop)",
)
args = parser.parse_args()


# environment variables
mega_env_vars = get_mega_env_vars(
    "MEGA_GITLAB_TOKEN",
    "MEGA_GPG_KEYGRIP",
    "MEGA_GPG_PASSWORD",
)


# start Release process
release = ReleaseProcess(
    args.project_name,
    mega_env_vars["MEGA_GITLAB_TOKEN"],
    args.private_git_url,
    args.private_git_target_branch,
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
