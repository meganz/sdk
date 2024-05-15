[[_TOC_]]

# MEGA SDK - Automated release process


## Quick run

Provide sensitive information:

```sh
export MEGA_GITLAB_TOKEN=FOO   # private token required for remote access; always required
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
python3 ./make_release.py -r <release-version> -p <project-name> -l <private-git-host-url> -o <private-git-remote-name> -u <private-git-remote-url> -d <private-git-develop-branch> -m <public-git-target-branch> -j <project-management-url> -t <target-apps> -c <chat-channel>
```

\# no source file will be edited (so less mandatory args, and dealing with gpg stuff not required)
```sh
python3 ./make_release.py -r <release-version> -p <project-name> -l <private-git-host-url> -n -d <private-git-develop-branch> -m <public-git-target-branch> -j <project-management-url> -t <target-apps> -c <chat-channel>
```

Example:

```sh
# will edit source file(s) to update the version
python3 ./make_release.py -r 1.0.0 -p SDK -l https://code.foo.bar -o origin -u https://foo.bar/sdk/sdk.git -d develop -m master -j https://jira.foo.bar -t "Android 1.0.1 / iOS 1.2 / MEGAsync 9.9.9" -c sdk_devs_only

# no source file will be edited (so less mandatory args, and dealing with gpg stuff not required)
python3 ./make_release.py -r 1.0.0 -p MEGAchat -l https://code.foo.bar -n -d develop -m master -j https://jira.foo.bar -t "Android 1.0.1 / iOS 1.2 / MEGAsync 9.9.9" -c sdk_devs_only
```

Running the following will also provide complete information:
```sh
python3 ./make_release.py -h
```


## Prerequisites

These should only be needed once.


### python stuff
* Install `Python 3`. The script was written on top of Python 3.12.2, just in case an older version would fail to run it.
* Install `pip`. Something like `python3 -m ensurepip --upgrade` should work.
* Install module `python-gitlab`. It worked with `python3 -m pip install python-gitlab`. For other install methods it didn't see the module.
* Install module `jira`. It worked directly with `pip install jira`.
* Install module `slack_sdk`. It worked directly with `pip install slack_sdk`.

### gitlab stuff
* [Create a personal access token](https://docs.gitlab.com/ee/user/profile/personal_access_tokens.html#create-a-personal-access-token) with scopes `api`, `read_api`, `read_user`, `create_runner`, `read_repository`, `write_repository`.
* The token created there must be set in `MEGA_GITLAB_TOKEN` env var.
* Remember to check from time to time that the token has not expired.

### Slack stuff
* Slack requires an _app_ that will provide a token required for using its API.
  * Any Slack user can [create an app](https://api.slack.com/start/quickstart#creating), set `chat:write` to `User Token Scopes`, get `User OAuth Token`.
  * Or reuse a _distributed app_ created by someone else, and get whatever token they provide.
* Set the token in `MEGA_SLACK_TOKEN` env var.
* Update the env var when the token has expired.


### gpg stuff
* (Installing git, creating gpg key and other stuff required by any commit are not covered here)
* Find the KEYGRIP

```sh
gpg -k --with-keygrip
```

The output will be something like

```sh
/home/USER/.gnupg/pubring.kbx
--------------------------------
pub   rsa3072 2021-01-04 [SC]
      AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
      Keygrip = BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
uid           [ultimate] My Name (Ubuntu, xps) <my@email.nz>
sub   rsa3072 2021-01-04 [E]
      Keygrip = CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
```

Value `BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB` is the one needed by `MEGA_GPG_KEYGRIP` env var.


## Known issues

Setting up gpg does not work automatically on Windows.
In order to run this process on a Windows machine, either be prepared to introduce the passkey when requested by a commit, or run the following setup steps manually in `Git Bash` (!), before starting the release process:

```sh
# Caching passphrase must be allowed:
echo allow-preset-passphrase  >> $(gpgconf --list-dirs homedir)/gpg-agent.conf
# Cache passphrase for key (from 'nix shell, git-bash on Win etc.):
$(gpgconf --list-dirs libexecdir)/gpg-preset-passphrase -c BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB <<EOF
passphrase
EOF
```
