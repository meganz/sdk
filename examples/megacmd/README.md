# MEGACMD - Command Line Interactive and Scriptable Application

MegaCMD provides non UI access to MEGA services. It intends to offer all the functionality with your MEGA account via shell interaction. It features 2 modes of interaction: 

- interactive. A shell to query your actions
- scriptable. A way to execute commands from a shell/an script/another program.

## Requirements

The same as those for the sdk (`cryptopp, zlib, sqlite3, cares, libuv, ssl, curl, sodium`) and of course `readline`. Also, it is recommended to include `pcre` to
have support for regular expressions.

Plus, `python` is also required.

* For convenience here is a list of packages for ubuntu 16.04: `autoconf libtool 
g++ libcrypto++-dev libz-dev sqlite3-dev libsqlite3-dev libssl-dev libcurl4-openssl-dev 
libreadline-dev libpcre++-dev libsodium-dev`

## Building

For platforms with Autotools, the generic way to build and install it is:

    sh autogen.sh
    ./configure --enable-megacmd
    make
    make install
    
* You will need to run `make install` as root

`Note`: if you use a prefix in configure, autocompletion from non-interactive usage won't work. You would need to `source /YOUR/PREFIX/etc/bash_completion.d/megacmd_completion.sh` (or link it from /etc/bash_completion.d)

## Usage

Before explaining the two ways of interaction, it is important to understand how MegaCMD works. When you login with MegaCMD, your session, the list of synced folders, and some cache database are stored in your local home folder. MegaCMD also stores some other configuration in that folder. Closing it does not delete those and restarting your computer will restore your previous session (the same as megasync won't ask for user/password once you restart your computer). You will need to `logout` properly in order to clean your data.

### Interactively:

Execute `mega-cmd`:
This opens an interactive shell. You can enter your commands here, with their arguments and flags.
You can list all the available commands with `help`. And obtain useful information about a command with `command --help`

First you would like to log in into your account. Again: notice that doing this stores the session and other stuff in your home folder. A complete logout is required if you want to end you session permanently and clean any traces (see `logout --help` for further info).

### Non-interactively:

`mega-cmd` acts as a server. It will be listening for client commands.
Use the different `mega-*` commands available.
`mega-help` will list all these commands (you will need to prepend "mega-")
You can obtain further info with `mega-command --help`

Those commands will have an output value != 0 in case of failure. See `megacmd.h` to view the existing error codes.

### Autocompletion:

MegaCMD features autocompletion in both interactive and non-interactive (only for bash) mode. It will help completing both local and remote (Mega Cloud) files, flags for commands, values for flags/access levels, even contacts.  

### Verbosity

There are two different kinds of logging messages:
- SDK based: those messages reported by the sdk and dependent libraries.
- MegaCMD based: those messages reported by MegaCMD itself.

You can adjust the level of logging for those kinds with `log` command.
However, for non interactive commands, passing `-v` (`-vv`, `-vvv`, and so on for a more verbose output) will use higher level of verbosity to an specific command.

### Regular Expressions
If you have compiled MegaCMD with PCRE (enabled by default), you can use PCRE compatible expressions. Otherwise, if compiled with c++11, c++11 regular expressions will be used. If non of the above is the case, you can only use "*" for any number of characters or "?" for a single unknown character.
You can check the regular expressions compatibility with `find --help`. e.g:
```
find --help
...
Options:
 --pattern=PATTERN	Pattern to match (Perl Compatible Regular Expressions)
```

Notice: if you use MegaCMD in non interactive mode, notice that shell pattern will take precedence. You will need to either escape symbols like `*` (`\*`) or surround them between quotes ("*")
