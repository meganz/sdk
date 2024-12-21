
# Extract a link from an email.
# Used by sdk/test/integration test SdkTest.SdkTestCreateAccount
import time
SCRIPT_EXECUTION_TIME = time.time()

import email
import imaplib
import os
import sys


class EmailProcessor:
    debug = False

    def __init__(self, user, password, host='mail.mega.co.nz', port=993):
        self.mail = imaplib.IMAP4_SSL(host, port)
        self.mail.login(user, password)
        self.mail.select('Inbox')

    def get_validation_link_from_email(self, to, intent, ref_time=None, delta=360.):
        """Get validation link from email."""
        link = None
        if ref_time is None:
            ref_time = time.time()
        messages = self.get_message_from_email(to, ref_time, delta)
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

    def get_message_from_email(self, to, ref_time, delta=360.):
        """Get message from email."""
        # sort all emails sent to the right address, newest first
        result, emails = self.mail.sort('REVERSE DATE', 'UTF-8', '(To "{}")'.format(to))
        if len(emails[0]) == 0:
            return None

        # get all potential emails
        messages = []
        for emailID in emails[0].split():
            message = self.process_email(ref_time, emailID, delta)
            if message:
                messages.append(message)
        return messages

    def process_email(self, ref_time, emailID, delta=360.):
        """Process each piece of an email to find the body and subject"""
        # Get email data from the ID.
        _, data = self.mail.fetch(emailID, "(RFC822)")

        for part in data:
            # Look for a tuple. It will contain the email body. We can ignore the rest.
            # It is usually in the first "part", but not always.
            if isinstance(part, tuple):
                # The body is the second in the tuple
                msg = email.message_from_bytes(part[1])

                # Process the subject
                subject, encoding = email.header.decode_header(msg['subject'])[0]
                if isinstance(subject, bytes):
                    subject = subject.decode(encoding)
                if self.debug: print("Subject: " + subject)

                # Discard if it is outdated
                dt = 0
                for item in msg['DKIM-Signature'].split(';'):
                    if 't=' in item:
                        dt = item.strip()[2:]
                        break
                else:
                    assert dt, 'timestamp not found from email header'
                elapsed = ref_time - float(dt)
                if elapsed > delta:
                    # Emails are sorted by time so no need to continue processing
                    break

                # Get the plain text from the body
                text = None
                for m in msg.walk():
                    content_type = m.get_content_type()
                    if content_type == 'text/plain':
                        text = m.get_payload(decode=True).decode('raw-unicode-escape')
                        break

                if subject and text:
                    return {subject: [emailID, text]}
        return None

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
link = ep.get_validation_link_from_email(to, intent, SCRIPT_EXECUTION_TIME, delta)
if link:
    print(link, end = '')
