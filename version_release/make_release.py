from argparse import ArgumentParser
from lib.utils import get_mega_env_vars
from lib.release_process import ReleaseProcess


# runtime arguments
parser = ArgumentParser()
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
