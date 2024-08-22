import os
from jira import JIRA, Issue
from jira.client import ResultList
from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError


class JiraTicketsReporter:

    def __init__(
        self,
        jira_url: str,
        jira_token: str,
        jira_project_key: str,
        slack_token: str,
        slack_channel: str,
    ):
        self._jira_url = jira_url
        self._jira_client = JIRA(self._jira_url, token_auth=jira_token)
        self._jira_project_key = jira_project_key
        self._slack_client = WebClient(token=slack_token)
        self._slack_channel = slack_channel

    def get_jira_query_for_issues_missing_fix_version(self, project_key: str):
        return f"project = {project_key} AND status = Resolved AND resolution = Done AND fixVersion is EMPTY"

    def get_jira_query_for_issues_missing_release_number_affected(
        self, project_key: str
    ):
        return f'project = {project_key} AND status = Resolved AND resolution = Done AND fixVersion is not EMPTY AND "Release number affected" is EMPTY'

    def fetch_jira_issues(self, jql_query: str) -> ResultList[Issue]:
        issues = self._jira_client.search_issues(
            jql_query,
            fields="key, assignee",
            maxResults=200,
        )

        return issues

    def get_slack_user_id_by_email(self, email: str) -> str | None:
        try:
            response = self._slack_client.users_lookupByEmail(email=email)
            return response["user"]["id"]
        except SlackApiError as e:
            print(f"Error fetching Slack user ID for {email}: {e.response['error']}")
            return None

    def post_to_slack(self, issue_key: str, slack_user_id: str, missing_field: str):
        if not slack_user_id:
            user_mention = "<!channel>"
        else:
            user_mention = f"<@{slack_user_id}>"

        issue_url = f"{self._jira_url}/browse/{issue_key}"
        message = (
            f"<{issue_url}|{issue_key}> is marked as *Resolved* but is missing *{missing_field}*.\n"
            f"CC: {user_mention}"
        )

        self._slack_client.chat_postMessage(channel=self._slack_channel, text=message)
        print(message)

    def report_tickets_missing_fix_version(self):
        issues = self.fetch_jira_issues(
            self.get_jira_query_for_issues_missing_fix_version(self._jira_project_key)
        )
        for issue in issues:
            issue_key = issue.key
            assignee = issue.fields.assignee
            slack_user_id = (
                self.get_slack_user_id_by_email(assignee.emailAddress)
                if assignee
                else None
            )
            self.post_to_slack(issue_key, slack_user_id, "Fix Version")

    def report_tickets_missing_release_number_affected(self):
        issues = self.fetch_jira_issues(
            self.get_jira_query_for_issues_missing_release_number_affected(
                self._jira_project_key
            )
        )
        for issue in issues:
            issue_key = issue.key
            assignee = issue.fields.assignee
            slack_user_id = (
                self.get_slack_user_id_by_email(assignee.emailAddress)
                if assignee
                else None
            )
            self.post_to_slack(issue_key, slack_user_id, "Release number affected")


# Get attribute values from the environment
JIRA_URL = os.environ["JIRA_URL"]
JIRA_PERSONAL_ACCESS_TOKEN = os.environ["JIRA_PERSONAL_ACCESS_TOKEN"]
JIRA_PROJECT_KEY = os.environ["JIRA_PROJECT_KEY"]
SLACK_BOT_TOKEN = os.environ["SLACK_BOT_TOKEN"]
SLACK_CHANNEL = os.environ["SLACK_CHANNEL"]

# Instantiate JiraTicketsReporter
jiraTicketsReporter = JiraTicketsReporter(
    jira_url=JIRA_URL,
    jira_token=JIRA_PERSONAL_ACCESS_TOKEN,
    jira_project_key=JIRA_PROJECT_KEY,
    slack_token=SLACK_BOT_TOKEN,
    slack_channel=SLACK_CHANNEL,
)

# Report issues
jiraTicketsReporter.report_tickets_missing_fix_version()
jiraTicketsReporter.report_tickets_missing_release_number_affected()
