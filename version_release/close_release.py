from lib.release_process import ReleaseProcess
import tomllib

# runtime arguments
with open("config.toml", "rb") as f:
    args = tomllib.load(f)["close_release"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    args["gitlab_token"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for closing a release
release.setup_project_management(
    args["jira_url"],
    args["jira_user"],
    args["jira_password"],
)
release.set_release_version_to_close(args["release_version"])

release.setup_local_repo(
    args["private_remote_name"],
    args["public_remote_name"],
    args["github_push_remote_url"],
)
release.setup_public_repo(args["github_token"], args["github_repo_owner"])
release.confirm_all_earlier_versions_are_closed()
if args["slack_token"]:
    release.setup_chat(args["slack_token"], "")
release.setup_wiki(
    args["confluence_url"],
    args["confluence_user"],
    args["confluence_password"],
)

# STEP 1: GitLab: Create tag "vX.Y.Z" from last commit of branch "release/vX.Y.Z"
release.create_release_tag()

# STEP 2: GitLab: Create release "Version X.Y.Z" from tag "vX.Y.Z" plus release notes
release.create_release_in_private_repo()

# STEP 3: GitLab: Merge version upgrade MR into public branch (master)
release.merge_release_changes_into_public_branch(args["public_branch"])

# STEP 4: local git: Push public branch (master) to public remote (github)
release.push_to_public_repo(
    args["private_remote_name"],
    args["public_branch"],
    args["public_remote_name"],
)

# STEP 5: GitHub: Create release in public repo from new tag
release.create_release_in_public_repo(args["release_version"])

# STEP 6: Jira: mark version as Released, set release date
release.mark_version_as_released()

# STEP 7: Confluence: Rotate own name to the end of the list of release captains
release.move_release_captain_last(args["confluence_page_id"])
