from jira import JIRA
from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError

# Jira configuration
JIRA_URL = 'https://jira.developers.mega.co.nz'
JIRA_API_TOKEN = 'your-jira-api-token'
JIRA_PROJECTS = ['SDK', 'CHT']

# Slack configuration
SLACK_TOKEN = 'your-slack-bot-token'
SLACK_CHANNEL = '#channel-name'

# Initialize Jira client
jira_client = JIRA(JIRA_URL, token_auth=JIRA_API_TOKEN)

# Initialize Slack client
slack_client = WebClient(token=SLACK_TOKEN)

def fetch_jira_issues(project_key):
    jql_query = f'project = {project_key} AND status = Resolved AND fixVersion is EMPTY'
    issues = jira_client.search_issues(
        jql_query, fields='key,summary,assignee'
    )

    return issues

def get_slack_user_id_by_email(email):
    try:
        response = slack_client.users_lookupByEmail(email=email)
        return response['user']['id']
    except SlackApiError as e:
        print(f"Error fetching Slack user ID for {email}: {e.response['error']}")
        return None

def post_to_slack(issue_key, summary, assignee_email):
    if not assignee_email:
        user_mention = "<!channel>"
    else:
        slack_user_id = get_slack_user_id_by_email(assignee_email)
        user_mention = f"<@{slack_user_id}>"
    
    issue_url = f"{JIRA_URL}/browse/{issue_key}"
    message = f"<{issue_url}|{issue_key}> is marked as *Resolved* but is missing a *Fix Version*.\n" \
              f"CC: {user_mention}"

    try:
        response = slack_client.chat_postMessage(channel=SLACK_CHANNEL, text=message)
    except SlackApiError as e:
        print(f"Error posting to Slack: {e.response['error']}")

def main():
    for project in JIRA_PROJECTS:
        issues = fetch_jira_issues(project)
        for issue in issues:
            issue_key = issue.key
            summary = issue.fields.summary
            assignee = issue.fields.assignee
            assignee_email = assignee.emailAddress if assignee else None
            post_to_slack(issue_key, summary, assignee_email)

if __name__ == "__main__":
    main()