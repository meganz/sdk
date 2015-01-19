# Example app using Python

This is a console app that uses the Python bindings of the SDK.

It shows a shell like the one in the `megacli` example that allows to:

- Log in to a MEGA account (`login`, `logout`) 
- Browse folders (`cd`, `ls`, `pwd`)
- Create new folders (`mkdir`)
- Show the inbound shared folders (`mount`)
- Upload/download files (`put`, `get`)
- Delete, rename and move files/folders (`rm`, `mv`)
- Import files and export files/folders (`import`, `export`)
- Change the password of the account (`passwd`)
- Get info about the account (`whoami`)

## How to build and run the project:

To build and run the project, follow theses steps:

1. Build the SDK including the option `--enable-python` in the `./configure` step
2. Run the `megacli.py` file in this folder

