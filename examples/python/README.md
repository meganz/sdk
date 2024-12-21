# Example app using Python

There are two basic examples contained here. A Mega Command Line Interface
client (`megacli`), and a simple CRUD example (create, read, update, delete
a file).

## Mega Command Line Interface

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

Prerequisite:

- Installed Mega SDK with Python bindings and the Python module `cmd` (or `cmd2`).

To start the client just run `megacli.py` from this folder.


## Mega CRUD Example

Just run `crud_example.py` from this folder.

To avoid typing in credentials, you may create a `credentials.json` file with
the following content:

```
{
    "user": "your.email@provider.org",
    "password": "your_supersecret_password"
}
```


## To Keep In Mind

If you want to create your own Python app, you can use the `MegaApi`
object. However, these bindings are still a work in progress and have
some inconveniences:

- You can't use callback parameters out of the callbacks. Instead, you can use
  the `copy()` method of `MegaRequest`, `MegaTransfer` and `MegaError`
  to preserve them and to use them later.
- You can't use objects returned by `MegaNodeList`,
  `MegaTransferList`, `MegaShareList` or `MegaUserList` after losing
  the reference to the original list. You can use the `copy()` method
  commented above to preserve those objects.
- You must keep a reference in your app to listeners used to receive
  events, otherwise the SDK will crash when it tries to use them.

We plan to create a `MegaApiPython` wrapper on top of `MegaApi` to
take care of these issues for you.

## How to build and run the project:

To build and run the project with Python bindings, please refer to this section: 
[Build/Install Python Bindings](https://github.com/meganz/sdk/tree/master/bindings/python)

