from lib.release_process import ReleaseProcess
import re
import tomllib

# runtime arguments
with open("config.toml", "rb") as f:
    args = tomllib.load(f)["make_another_rc"]

# create Release process and do common init
release = ReleaseProcess(
    args["project_name"],
    args["gitlab_token"],
    args["gitlab_url"],
    args["private_branch"],
)

# prerequisites for a new RC
release.setup_local_repo(args["private_remote_name"], "", "")
release.setup_project_management(
    args["jira_url"], args["jira_user"], args["jira_password"]
)
if args["slack_token"] and args["slack_channel"]:
    release.setup_chat(args["slack_token"], args["slack_channel"])

assert args["mr_description"]
assert args["tickets"]


# STEP 1: Jira, GitLab: Create new branch (fix/SDK-1234_My_fix) from the last RC tag.
version = args["release_version"]  # "1.0.0"
app_descr = release.set_release_version_for_new_rc(version)
name_of_new_branch = "fix/" + re.sub(r"[^\w-]+", "_", args["mr_description"])
last_rc = release.create_branch_from_last_rc(
    args["private_remote_name"], name_of_new_branch
)


# STEP 2: local git: Wait for code changes and push them.
if not release.wait_for_local_changes_to_be_applied():
    raise RuntimeError("Process aborted")
release.push_branch(args["private_remote_name"], name_of_new_branch)


# STEP 3: GitLab: Create MR from new branch to release branch (do NOT set to delete automatically after merge).
mr_to_release = release.open_private_mr(
    name_of_new_branch,
    release.get_new_release_branch(),
    args["mr_description"] + " (release)",
    remove_source=False,
)


# STEP 4: GitLab: Open MR from new branch to develop (set to delete automatically after merge).
mr_to_develop = release.open_private_mr(
    name_of_new_branch,
    args["private_branch"],
    args["mr_description"],
    remove_source=True,
)


# STEP 5: GitLab, Jira: Merge MRs after being approved; add Fix Version to tickets
release.merge_private_mr(mr_to_release)  # Keep this order of execution!
release.merge_private_mr(mr_to_develop)
tickets: list[str] = [t.strip() for t in args["tickets"].split(",")]
release.add_fix_version_to_tickets(tickets)


# STEP 6: GitLab: step #5 from make_release:
# Create new RC tag
release.create_rc_tag(last_rc + 1)


# STEP 7: Slack: step #8 from make_release:
# Post release notes to Slack
apps = [a.strip() for a in app_descr.split("/")]
release.post_notes(apps)
