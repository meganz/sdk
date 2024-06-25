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
    help="Version to be closed (i.e. 1.0.0)",
    type=str,
    required=True,
)
parser.add_argument(
    "-l",
    "--private-git-host-url",
    help="URL of private repository (i.e. https://code.foo.bar)",
    type=str,
    required=True,
)
parser.add_argument(
    "-o",
    "--private-git-remote-name",
    help="Name of private repository's git remote (default: origin)",
    type=str,
    default="origin",
)
parser.add_argument(
    "-u",
    "--private-git-remote-url",
    help="URL of private repository's git remote (i.e. git@foo.bar:proj/proj.git)",
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
    "-j",
    "--project-management-url",
    help="URL of project management tool (i.e. https://jira.foo.bar)",
    type=str,
    required=True,
)
parser.add_argument(
    "-c",
    "--chat-channel",
    help="Chat channel where MR announcements will be posted (i.e. sdk_devs). Print to console if missing",
    type=str,
    default="",
)
parser.add_argument(
    "-v",
    "--public-git-remote-url",
    help="URL of remote for public repository (i.e. git@github.com:owner/proj.git)",
    type=str,
    required=True,
)
parser.add_argument(
    "-b",
    "--public-git-remote-name",
    help="Name of remote for public repository (default: public)",
    type=str,
    default="public",
)
parser.add_argument(
    "-z",
    "--public-git-repo-owner",
    help="Owner of public repository (default: meganz)",
    type=str,
    default="meganz",
)
parser.add_argument(
    "-w",
    "--wiki-url",
    help="URL project wiki (i.e. https://confluence.foo.bar)",
    type=str,
    default="",
)
parser.add_argument(
    "-i",
    "--wiki-page-id",
    help="ID of project wiki page (i.e. 1234567)",
    type=str,
    default="",
)
args = parser.parse_args()

# environment variables
mega_env_vars = get_mega_env_vars(
    "MEGA_GITLAB_TOKEN",
    "MEGA_JIRA_USER",
    "MEGA_JIRA_PASSWORD",
    "MEGA_GITHUB_TOKEN",
)
mega_env_vars["MEGA_CONFLUENCE_USER"] = get_mega_env_var("MEGA_CONFLUENCE_USER")
mega_env_vars["MEGA_CONFLUENCE_PASSWORD"] = get_mega_env_var("MEGA_CONFLUENCE_PASSWORD")
slack_token = get_mega_env_var("MEGA_SLACK_TOKEN")

# create Release process and do common init
release = ReleaseProcess(
    args.project_name,
    mega_env_vars["MEGA_GITLAB_TOKEN"],
    args.private_git_host_url,
    args.private_git_develop_branch,
)

# prerequisites for closing a release
release.setup_project_management(
    args.project_management_url,
    mega_env_vars["MEGA_JIRA_USER"],
    mega_env_vars["MEGA_JIRA_PASSWORD"],
)
release.set_release_version_to_close(args.release_version)

release.setup_local_repo(
    args.private_git_remote_name,
    args.private_git_remote_url,
    args.public_git_remote_name,
    args.public_git_remote_url,
)
release.setup_public_repo(
    mega_env_vars["MEGA_GITHUB_TOKEN"], args.public_git_repo_owner, args.project_name
)
release.confirm_all_earlier_versions_are_closed()
if slack_token and args.chat_channel:
    release.setup_chat(slack_token, args.chat_channel)
release.setup_wiki(
    args.wiki_url,
    mega_env_vars["MEGA_CONFLUENCE_USER"],
    mega_env_vars["MEGA_CONFLUENCE_PASSWORD"],
)

# STEP 1: GitLab: Create tag "vX.Y.Z" from last commit of branch "release/vX.Y.Z"
release.create_release_tag()

# STEP 2: GitLab: Create release "Version X.Y.Z" from tag "vX.Y.Z" plus release notes
release.create_release_in_private_repo()

# STEP 3: GitLab: Merge version upgrade MR into public branch (master)
release.merge_release_changes_into_public_branch(args.public_git_target_branch)

# STEP 4: local git: Push public branch (master) to public remote (github)
release.push_to_public_repo(
    args.private_git_remote_name,
    args.public_git_target_branch,
    args.public_git_remote_name,
)

# STEP 5: GitHub: Create release in public repo from new tag
release.create_release_in_public_repo(args.release_version)

# STEP 6: Jira: mark version as Released, set release date
release.mark_version_as_released()

# STEP 7: Confluence: Rotate own name to the end of the list of release captains
release.move_release_captain_last(args.wiki_page_id)
