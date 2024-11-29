from slack_sdk import WebClient


class Slack:  # use Slack API
    """
    Functionality:
    - send message with mrkdwn to a channel
    """

    def __init__(self, auth_token: str):
        self._client = WebClient(token=auth_token)

    def post_message(self, channel: str, thread: str, message: str):
        self._client.chat_postMessage(
            channel=channel,
            thread_ts=thread,
            text=message,
            mrkdwn=True,
            reply_broadcast=bool(thread),
        )
