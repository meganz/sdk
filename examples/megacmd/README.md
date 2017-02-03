# MEGACMD - Command Line Interactive and Scriptable Application

MegaCMD provides non UI access to MEGA services. It intends to offer all the 
functionality with your MEGA account via commands. 

Available packages for MEGAcmd in all supported platforms should be found 
[here](https://mega.nz/cmd). 

It features 2 modes of interaction: 

- interactive. A shell to query your actions
- scriptable. A way to execute commands from a shell/a script/another program.

In order to provide those 2 modes, it features one main application 
and several client commands.
For using the interactive mode you will need to launch the main application 
and start writting your commands there. Also, besides this interactive mode, 
this application will act as a server. That is, it will be listening to petitions,
from the client commands. 

# Building MEGAcmd
If you wish to build megacmd using this repository, here are a list of 
requirements and building instructions.

## Requirements

The same as those for the sdk (`cryptopp, zlib, sqlite3, cares, libuv, ssl, curl,
sodium`) and of course `readline`. Also, it is recommended to include `pcre` to
have support for regular expressions.

* For convenience here is a list of packages for ubuntu 16.04: `autoconf libtool 
g++ libcrypto++-dev libz-dev sqlite3-dev libsqlite3-dev libssl-dev libcurl4-openssl-dev 
libreadline-dev libpcre++-dev libsodium-dev`

## Building and installing

For platforms with Autotools, MegaCMD is included in the generic compilation 
of the sdk. To build and install:

    sh autogen.sh
    ./configure
    make
    make install
    
* You will need to run `make install` as root

* To disable megacmd use `configure` with `--disable-megacmd`

`Note`: if you use a prefix in configure, autocompletion from non-interactive usage
won't work. You would need to `source /YOUR/PREFIX/etc/bash_completion.d/megacmd_completion.sh` 
(or link it at /etc/bash_completion.d)

## Usage

Before explaining the two ways of interaction, it is important to understand how 
MegaCMD works. When you login with MegaCMD, your session, the list of synced folders,
and some cache database are stored in your local home folder. MegaCMD also stores
some other configuration in that folder. Closing it does not delete those and 
restarting your computer will restore your previous session (the same as megasync 
won't ask for user/password once you restart your computer). 
You will need to `logout` properly in order to clean your data.

### Interactively:

Execute MEGAcmd server.
This opens an interactive shell. You can enter your commands here, 
with their arguments and flags.
You can list all the available commands with `help`. 
And obtain useful information about a command with `command --help`

First you would like to log in into your account. 
Again: notice that doing this stores the session and other stuff in your home folder. 
A complete logout is required if you want to end you session permanently 
and clean any traces (see `logout --help` for further info).

### Non-interactively:

Have MEGAcmd server running. It will be listening for client commands.
Use the different `mega-*` commands available.
`mega-help` will list all these commands (you will need to prepend "mega-").
You can obtain further info with `mega-command --help`

Those commands will have an output value != 0 in case of failure. 
See [megacmd.h](megacmd.h) to view the existing error codes.

Ideally, you would like to have these commands in your PATH 
(See `Platforms` for more info).

#Platforms

## Linux
If you install using one of the available packages at [here](https://mega.nz/cmd), 
or have it built without `--prefix`, both the server (`mega-cmd`) and 
the different client commands (`mega-*`) will be in your PATH 
(you might need to open your shell again). If you are using bash, 
you should also have autocompletion for client commands working. 
If that is not you case, include the location for the binaries in your path.

## Windows
Once you have MEGAcmd installed, you just need to execute it to open the shell. 
For non-interactive usage, there are several `mega-*.bat`  client commands you can 
use writting their absolute paths, or including their location into your environment PATH
 and execute them normally (`mega-*`).
If you use PowerShell and you have installed the official MEGAcmd, 
you can do that simply with:

```
$env:PATH += ";$env:LOCALAPPDATA\MEGAcmd"
```

Client commands completion requires bash, hence, it is not available for Windows.

### Security caveats
For the current Windows version of MEGAcmd, the server will be 
using network sockets for attending client commands. This socket is open for 
petitions on localhost, hence, you should not use it in a multiuser environment.

## MacOS 
For MacOS, after installing the dmg, you can launch the server using MEGAcmd
in Applications. If you wish to use the client commands from MacOS Terminal, open
the Terminal and include the installation folder in the PATH. Typically:

```
export PATH=/Applications/MEGAcmd.app/Contents/MacOS:$PATH
```

And for bash completion, source `megacmd_completion.sh` :

```
source /Applications/MEGAcmd.app/Contents/MacOS/megacmd_completion.sh
```

#Features:

## Autocompletion:

MegaCMD features autocompletion in both interactive and non-interactive 
(only for bash) mode. It will help completing both local and remote (Mega Cloud) 
files, flags for commands, values for flags/access levels, even contacts.  

## Verbosity

There are two different kinds of logging messages:
- SDK based: those messages reported by the sdk and dependent libraries.
- MegaCMD based: those messages reported by MegaCMD itself.

You can adjust the level of logging for those kinds with `log` command.
However, for non interactive commands, passing `-v` (`-vv`, `-vvv`, and so on 
for a more verbose output) will use higher level of verbosity to an specific command.

## Regular Expressions
If you have compiled MegaCMD with PCRE (enabled by default), 
you can use PCRE compatible expressions. Otherwise, if compiled with c++11, 
c++11 regular expressions will be used. If non of the above is the case, 
you can only use "*" for any number of characters or "?" for a single unknown character.
You can check the regular expressions compatibility with `find --help`. e.g:
```
find --help
...
Options:
 --pattern=PATTERN	Pattern to match (Perl Compatible Regular Expressions)
```

Notice: if you use MegaCMD in non interactive mode, notice that shell pattern will 
take precedence. You will need to either escape symbols like `*` (`\*`) 
or surround them between quotes (e.g: "*.txt")


# Known Bugs
- Currently there are certain discrepancies with PATHS when loggin into a public folder.
For instance, imagine a folder named `toshare` with a subfolder named `x`. If we login
into `toshare` and execute `find /x`, this will be the output:

```
/toshare/x
```
Whereas if we execute `find /toshare/x`, we receive an error, since folder absolute path
refers to `/` as root path. 
```
[err: 12:21:51] Couldn't find /toshare/x
```

It might better be refered as /toshare/x

