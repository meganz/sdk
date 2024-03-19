
# Extact a link from an email.
# Used by sdk/test/integration test SdkTest.SdkTestCreateAccount

import email
import imaplib
import os
import sys
import time


class EmailProcessor:
    mail = None
    debug = False

    def __init__(self, user, password, host='mail.mega.co.nz', port=993):
        self.mail = imaplib.IMAP4_SSL(host, port)
        self.mail.login(user, password)
        self.mail.select('Inbox')

    def get_validation_link_from_email(self, to, intent, delta=360):
        """Get validation link from email."""
        link = None
        messages = self.get_message_from_email(to, delta)
        if not messages:
            return None
        for message in messages:
            sub = list(message)[0]
            text = message[sub]
            if not text[1]:
                continue
            for line in text[1].splitlines():
                if self.debug: 
                    if line.startswith('https://'):
                        print("line:" + line)
                if line.startswith('https://') and ('#' + intent) in line:
                    link = line.strip()
                    break
        self.mail.close()
        self.mail.logout()
        return link

    def get_message_from_email(self, to, delta=360):
        """Get message from email."""
        methodCallTimeStamp = time.time()
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
            if self.debug: print("subject: " + subject)
            text = None
            dt = 0
            for item in msg['DKIM-Signature'].split(';'):
                if 't=' in item:
                    dt = item.strip()[2:]
                    break
            else:
                assert dt, 'timestamp not found from email header'
            elapsed = methodCallTimeStamp - float(dt)
            if elapsed > delta:
                continue
            for m in msg.walk():
                content_type = m.get_content_type()
                if content_type == 'text/plain':
                    text = m.get_payload(decode=True).decode('raw-unicode-escape')
                    break
            if text:
                messages.append({subject: [n, text]})
        return messages

if len(sys.argv) == 1 or "--help" in sys.argv[1:] or (os.name == "nt" and "/?" in sys.argv[1:]):
    # no args, --help or /? on windows
    print("usage: " + sys.argv[0] + " email-user email-password to-email-address {confirm|cancel|recover|<other>} max-age-in-seconds")
    print("")
    print("e.g. python email_processor.py sdk-jenkins a-password sdk-jenkins+test-e-1@mega.co.nz recover 36000")
    print("$TEST_PASS can override email-password, but password placholder still requried on command line")
    exit(0);

user = os.getenv('TEST_USER') or sys.argv[1]
password = os.getenv('TEST_PASS') or sys.argv[2]
to = sys.argv[3]
intent = sys.argv[4]
if intent == "delete":
    # backwards compatible
    intent = "cancel"
    
delta = float(sys.argv[5])

ep = EmailProcessor(user, password)
link = ep.get_validation_link_from_email(to, intent, delta)
if link:
    print(link, end = '')
