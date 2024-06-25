[[_TOC_]]

# MEGA SDK - Close a new Release


## Quick run

Provide sensitive information:

For GitLab
```sh
export MEGA_GITLAB_TOKEN=FOO   # token required for remote access to private repo
```
For GitHub
```sh
export MEGA_GITHUB_TOKEN=BAR   # token required for remote access to public repo
```
For Jira
```sh
export MEGA_JIRA_USER=Fred     # Jira user
export MEGA_JIRA_PASSWORD=Thud # Jira password
```
For Slack (optional)
```sh
export MEGA_SLACK_TOKEN=Qux    # Slack authentication token
```
For Confluence (optional)
```sh
export MEGA_CONFLUENCE_USER=Alice   # Confluence user
export MEGA_CONFLUENCE_PASSWORD=Bob # Confluence password
```

From current directory run:
```sh
python3 ./close_release.py -p <project-name> -r <release-version> -l <private-git-host-url> -u <private-git-remote-url> -j <project-management-url> -v <public-git-remote-url> -w <wiki-url> -i <wiki-page-id>
```

Example:
```sh
python3 ./close_release.py -p SDK -r 1.0.0 -l https://code.foo.bar -u git@foo.bar:proj/proj.git -j https://jira.foo.bar -v git@github.com:owner/proj.git -w https://confluence.foo.bar -i 1234567
```

Running the following will also provide complete information, including other arguments not mentioned above that have default values:
```sh
python3 ./close_release.py -h
```
