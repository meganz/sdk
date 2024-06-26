[[_TOC_]]

# MEGA SDK - Make a new Release


## Quick run

Provide sensitive information:

For GitLab
```sh
export MEGA_GITLAB_TOKEN=FOO   # GitLab authentication token
```
For signing commits, GPG needs the following
```sh
export MEGA_GPG_KEYGRIP=BAR    # KEYGRIP of your gpg key (see below how to find it); required only when a source file will be edited
export MEGA_GPG_PASSWORD=BAZZ  # password corresponding to the KEYGRIP (the one used to sign any commit); required only when a source file will be edited
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

From current directory run one of the following:

\# will edit [../include/mega/version.h](../include/mega/version.h) to update the version
```sh
python3 ./make_release.py -p <project-name> -l <private-git-host-url> -u <private-git-remote-url> -j <project-management-url> -t <target-apps> -c <chat-channel>
```

\# no source file will be edited (so less mandatory args, and dealing with gpg stuff not required)
```sh
python3 ./make_release.py -p <project-name> -l <private-git-host-url> -n -j <project-management-url> -t <target-apps> -c <chat-channel>
```

Example:

```sh
# will edit source file(s) to update the version
python3 ./make_release.py -p SDK -l https://code.foo.bar -u git@foo.bar:sdk/sdk.git -j https://jira.foo.bar -t "Android 1.0.1 / iOS 1.2 / MEGAsync 9.9.9" -c sdk

# no source file will be edited (so less mandatory args, and dealing with gpg stuff not required)
python3 ./make_release.py -p MEGAchat -l https://code.foo.bar -n -j https://jira.foo.bar -t "Android 1.0.1 / iOS 1.2 / MEGAsync 9.9.9" -c sdk
```

Version for the new release will be automatically determined. To explicitly pass a release version pass `-r <release-version>` to the script (ex: `-r 1.0.0`).

Running the following will also provide complete information, including other arguments not mentioned above that have default values:
```sh
python3 ./make_release.py -h
```
