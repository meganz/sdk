[[_TOC_]]

# MEGA SDK - Release management

The following processes have been automated:

* [Create a new Release](README_create_release.md)
* [Close a new Release](README_close_release.md)


## Prerequisites

These should only be needed once.


### Python stuff
* Install `Python 3`. The script was written on top of Python 3.12.2, just in case an older version would fail to run it.
* Install `pip`. Something like `python3 -m ensurepip --upgrade` should work.
  However, Ubuntu apparently is "special" and Python from its repo comes without `ensurepip`. So try `sudo apt install python3-pip`.
* Install required modules with `pip install -r requirements.txt` (and upgrade all later with `pip install -U -r requirements.txt`).

### GitLab stuff
* [Create a personal access token](https://docs.gitlab.com/ee/user/profile/personal_access_tokens.html#create-a-personal-access-token) with scopes `api`, `read_api`, `read_user`, `create_runner`, `read_repository`, `write_repository`.
* The token created there must be set in `MEGA_GITLAB_TOKEN` env var.
* Remember to check from time to time that the token has not expired.

### GitHub stuff
* [Create a personal access token](https://github.com/settings/tokens/new) with scope `repo` and all its sub-scopes.
  * fill in `Note` and `Expiration` with what feels appropriate
* The token created there must be set in `MEGA_GITHUB_TOKEN` env var.
* Remember to check from time to time that the token has not expired.

### Slack stuff
* Slack requires an _app_ that will provide a token required for using its API.
  * Any Slack user can [create an app](https://api.slack.com/start/quickstart#creating), set `chat:write` to `User Token Scopes`, get `User OAuth Token`.
  * Or reuse a _distributed app_ created by someone else, and get whatever token they provide.
* Set the token in `MEGA_SLACK_TOKEN` env var.
* Update the env var when the token has expired.

### Confluence stuff
* This is optional. If Confluence details are not provided, the rotation of Release Captain will not be executed.
* `wiki-page-id`: open "Release management" page in Confluence, then `...` -> `Page information` -> from the url copy the value after `pageId=`.

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

#### Known gpg issues

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
