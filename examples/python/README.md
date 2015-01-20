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

To build and run the project, follow these steps:

1. Install SWIG in your system (it's required to generate Python bindings)
2. Build the SDK including the option `--enable-python` in the `./configure` step
3. Install the required Python dependendy `cmd`.
4. Run the `megacli.py` file in this folder

If you want to create your own Python app. You can use the `MegaApi` object. However, these bindings are still
a work in progress and have some inconveniences:
- You can't use callback parameters out of the callbacks
You can use the `copy()` method of `MegaRequest`, `MegaTransfer` and `MegaError` to preserve them and to use them later.

- You can't use objects returned by `MegaNodeList`, `MegaTransferList`, `MegaShareList` or `MegaUserList` after losing the
reference to the original list. You can use the `copy()` method commented above to preserve those objects.

- You must keep a reference in your app to listeners used to receive events, otherwise the SDK will crash when it tries
to use them.

We plan to create a `MegaApiPython` wrapper on top of `MegaApi` to take care of these issues for you.
