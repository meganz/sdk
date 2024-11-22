[[_TOC_]]

# MEGA SDK - Release management

The following processes have been automated:

## Make a new Release
Fill the details in `[make_release]` section of your `config.toml` local copy of [config.toml.template](config.toml.template).

You will also need to set the following environment variables to make the script work:
- `GITLAB_TOKEN`
- `JIRA_TOKEN`
- `SLACK_TOKEN`
- `GPG_KEYGRIP`
- `GPG_PASSWORD`

> Note that the version for a new release will be automatically determined unless one was explicitly passed. To explicitly pass one, fill `release_version` argument in this section.

> Note that version-file will be updated in the process, where applicable ([version.h](../include/mega/version.h) in SDK). For projects that don't have such a file, `gpg_keygrip` and `gpg_password` will be ignored and can be left empty.

From a directory in the repo (!) for which we intend to make a release, run:

```sh
python3 path/to/make_release.py path/to/config.toml
```

## Close a new Release
Fill the details in `[close_release]` section of your `config.toml` local copy of [config.toml.template](config.toml.template).

You will also need to set the following environment variables to make the script work:
- `GITLAB_TOKEN`
- `JIRA_TOKEN`
- `SLACK_TOKEN`
- `GITHUB_TOKEN`
- `CONFLUENCE_USER`
- `CONFLUENCE_PASSWORD`


From a directory in the repo (!) for which we intend to close a release, run:

```sh
python3 path/to/close_release.py path/to/config.toml
```

## Patch a Release
> Note that this process can become very complex when multiple releases need to be patched. Because of that, the automation is done for step 7 and further.

Fill the details in `[patch_release]` section of your `config.toml` local copy of [config.toml.template](config.toml.template).

You will also need to set the following environment variables to make the script work:
- `GITLAB_TOKEN`
- `JIRA_TOKEN`
- `SLACK_TOKEN`
- `GPG_KEYGRIP`
- `GPG_PASSWORD`

From a directory in the repo (!) for which we intend to patch a release, run:

```sh
python3 ./patch_release.py path/to/config.toml
```

## Make another RC
Fill the details in `[make_another_rc]` section of your `config.toml` local copy of [config.toml.template](config.toml.template).

You will also need to set the following environment variables to make the script work:
- `GITLAB_TOKEN`
- `JIRA_TOKEN`
- `SLACK_TOKEN`

> Note that the number of the new RC will be automatically determined, from the last RC already existing for that Release plus 1.

From a directory in the repo (!) for which we intend to make a release, run:

```sh
python3 path/to/make_another_rc.py path/to/config.toml
```


## Prerequisites

These should only be needed once.


### Python stuff
* Install `Python 3`. The scripts were written using `Python 3.12.2`, just in case an older version would fail to run them.
* Install `pip`. Something like `python3 -m ensurepip --upgrade` should work.
  However, Ubuntu apparently is "special" and Python from its repo comes without `ensurepip`. So try `sudo apt install python3-pip`.
* Install required modules with `pip install -r requirements.txt` (and upgrade all later with `pip install -U -r requirements.txt`).

### GitLab stuff
* [Create a personal access token](https://docs.gitlab.com/ee/user/profile/personal_access_tokens.html#create-a-personal-access-token) with scopes `api`, `read_api`, `read_user`, `create_runner`, `read_repository`, `write_repository`.
* The token created there must be set in `gitlab_token` argument(s) inside `config.toml`.
* Remember to check from time to time that the token has not expired.

### GitHub stuff
* [Create a personal access token](https://github.com/settings/tokens/new) with scope `repo` and all its sub-scopes.
  * fill in `Note` and `Expiration` with what feels appropriate
* The token created there must be set in `github_token` argument(s) inside `config.toml`.
* Remember to check from time to time that the token has not expired.

### Slack stuff

##### Setup app

* Slack requires an _app_ that will provide a token required for using its API.
  * Any Slack user can create an app, configure the scope for it, and get the necessary token:
    * [Go to Your Apps](https://api.slack.com/apps)
      * click **Create New App** -> **From scratch**
      * set a name for the new app and set **MEGA** workspace for it
      * click **Create App**.
    * Go to **OAuth & Permissions** (on the left side, under _Features_)
      * Find **Scopes** section -> **User Token Scopes** -> click **Add an OAuth Scope** -> choose `chat:write`
      * Find **OAuth Tokens for Your Workspace** section -> click **Install to Workspace** -> review the permissions listed there -> click **Allow**
      * From the same **OAuth Tokens for Your Workspace** section -> copy **User OAuth Token**
  * Or reuse a _distributed app_ created by someone else, and get whatever token they provide.
* Set the token in `slack_token` argument(s) inside `config.toml`.
* Update the env var when the token has expired.

##### Find channel and thread to post to

* Channel can be passed by name, as in `#foo_bar` without the `#` prefix. Another option is to pass it by id, which can be obtained from the web url. The web url of a channel (or direct message) looked like `https://app.slack.com/client/AAAAAAAAA/BBBBBBBBBBB`. The `BBBBBBBBBBB` component is the channel id.
* Thread id can be obtained from the web url of the message in a channel, considered the root of a thread. The web url of such a message looked like `https://megaconz.slack.com/archives/BBBBBBBBBBB/pTTTTTTTTTTTTTTTT`. The `TTTTTTTTTTTTTTTT` component needs to be split by a `.` into a 10.6 string. The result is the thread id, as `TTTTTTTTTT.TTTTTT`.


### Confluence stuff
* This is optional. If Confluence details are not provided, the rotation of Release Captain will not be executed.
* Obtain `wiki-page-id`: open "Release management" page in Confluence, then `...` -> `Page information` -> from the url copy the value after `pageId=`.
* Set the id in `confluence_page_id` argument(s) inside `config.toml`.

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

Value `BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB` is the one needed by `gpg_keygrip` argument(s) inside `config.toml`.

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
