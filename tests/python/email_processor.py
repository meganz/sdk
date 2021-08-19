import email
import imaplib
import os
import sys


class EmailProcessor():
    mail = None

    def __init__(self, user, password, host='mail.mega.co.nz', port=993):
        self.mail = imaplib.IMAP4_SSL(host, port)
        self.mail.login(user, password)
        self.mail.select('Inbox')

    def get_validation_link_from_email(self, to, intent, delete=False):
        """Get validation link from email."""
        link = None
        messages = self.get_message_from_email(to)
        if not messages:
            return None
        for message in messages:
            sub = list(message)[0]
            text = message[sub]
            if not text[1]:
                continue
            for line in text[1].splitlines():
                if line.startswith('https://') and ('#' + intent) in line:
                    link = line.strip()
                    break
            if link:  # already found it, quit
                #if delete:
                    # This should move the email to Trash! But it doesn't work
                    #self.mail.store(text[0], '+X-GM-LABELS', '\\Trash')
                break
        self.mail.close()
        self.mail.logout()
        return link

    def get_message_from_email(self, to):
        """Get message from email."""

        # sort all emails sent to the right address, newest first
        result, data = self.mail.sort('REVERSE DATE', 'UTF-8', '(To "{}")'.format(to))
        if len(data[0]) == 0:
            return None

        # get all potential emails
        messages = []
        for n in data[0].split():
            _, d = self.mail.fetch(n, "(RFC822)")
            body = d[0][1]
            msg = email.message_from_string(body.decode('utf-8'))
            subject = msg['subject']
            text = None
            for m in msg.walk():
                content_type = m.get_content_type()
                if content_type == 'text/plain':
                    text = m.get_payload(decode=True).decode('raw-unicode-escape')
                    break
            if text:
                messages.append({subject: [n, text]})
        return messages

user = os.getenv('TEST_USER') or sys.argv[1]
password = os.getenv('TEST_PASS') or sys.argv[2]
to = sys.argv[3]
intent = ""
if sys.argv[4] == "confirm":
    intent = "confirm"
elif sys.argv[4] == "delete":
    intent = "cancel"
ep = EmailProcessor(user, password)
link = ep.get_validation_link_from_email(to, intent, delete=True)
if link:
    print(link, end = '')