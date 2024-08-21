import os
from jira import JIRA, Issue
from jira.client import ResultList
from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError

# Jira configuration
JIRA_URL = "https://jira.developers.mega.co.nz"
JIRA_API_TOKEN = os.environ["JIRA_PERSONAL_ACCESS_TOKEN"]
JIRA_PROJECTS = ["SDK", "CHT"]

# Slack configuration
SLACK_TOKEN = os.environ["SLACK_BOT_TOKEN"]
SLACK_CHANNEL = "#test-slack-automation"

# Initialize Jira client
jira_client = JIRA(JIRA_URL, token_auth=JIRA_API_TOKEN)

# Initialize Slack client
slack_client = WebClient(token=SLACK_TOKEN)


def get_jira_query_for_issues_missing_fix_version(project_key: str):
    return f"project = {project_key} AND status = Resolved AND resolution = Done AND fixVersion is EMPTY"


def get_jira_query_for_issues_missing_release_number_affected(project_key: str):
    return f'project = {project_key} AND status = Resolved AND resolution = Done AND fixVersion is not EMPTY AND "Release number affected" is EMPTY'


def fetch_jira_issues(jql_query: str) -> ResultList[Issue]:
    issues = jira_client.search_issues(
        jql_query,
        fields="key, assignee",
        maxResults=200,
    )

    return issues


def get_slack_user_id_by_email(email: str) -> str | None:
    try:
        response = slack_client.users_lookupByEmail(email=email)
        return response["user"]["id"]
    except SlackApiError as e:
        print(f"Error fetching Slack user ID for {email}: {e.response['error']}")
        return None


def post_to_slack(issue_key: str, slack_user_id: str, missing_field: str):
    if not slack_user_id:
        user_mention = "<!channel>"
    else:
        user_mention = f"<@{slack_user_id}>"

    issue_url = f"{JIRA_URL}/browse/{issue_key}"
    message = (
        f"<{issue_url}|{issue_key}> is marked as *Resolved* but is missing *{missing_field}*.\n"
        f"CC: {user_mention}"
    )

    slack_client.chat_postMessage(channel=SLACK_CHANNEL, text=message)
    print(message)


def report_tickets_missing_fix_version():
    for project in JIRA_PROJECTS:
        issues = fetch_jira_issues(
            get_jira_query_for_issues_missing_fix_version(project)
        )
        for issue in issues:
            issue_key = issue.key
            assignee = issue.fields.assignee
            slack_user_id = (
                get_slack_user_id_by_email(assignee.emailAddress) if assignee else None
            )
            post_to_slack(issue_key, slack_user_id, "Fix Version")


def report_tickets_missing_release_number_affected():
    for project in JIRA_PROJECTS:
        issues = fetch_jira_issues(
            get_jira_query_for_issues_missing_release_number_affected(project)
        )
        for issue in issues:
            issue_key = issue.key
            assignee = issue.fields.assignee
            slack_user_id = (
                get_slack_user_id_by_email(assignee.emailAddress) if assignee else None
            )
            post_to_slack(issue_key, slack_user_id, "Release number affected")


report_tickets_missing_fix_version()
report_tickets_missing_release_number_affected()
