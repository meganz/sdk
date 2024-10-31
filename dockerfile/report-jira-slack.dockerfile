# This is the container where `automation/report_jira_tickets_on_slack.py` should run

FROM debian:12

ARG AUTO_USER=automation
ARG AUTO_HOME=/var/lib/automation

RUN apt update && \
    apt install -y python3-full && \
    useradd -md $AUTO_HOME -s /bin/bash $AUTO_USER 

USER $AUTO_USER
WORKDIR $AUTO_HOME
ADD automation $AUTO_HOME

RUN python3 -m venv $AUTO_HOME/venv && \
    $AUTO_HOME/venv/bin/pip install -r requirements.txt

CMD ["venv/bin/python3", "report_jira_tickets_on_slack.py"]
