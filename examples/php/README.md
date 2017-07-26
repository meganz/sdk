# Example app using PHP

This is a console app that uses the PHP bindings of the SDK.

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

1. Install `SWIG` in your system (it's required to generate PHP bindings)
2. Build the SDK including the option `--enable-php` in the `./configure` step
3. Install the required PHP dependendy `Symfony Console`, You can use `Composer` for that
3. Run the `megacli.php` file in this folder
 
If you you want to build your own app, just create your own `MegaApiPHP` object and use it. Please don't
directly use the `MegaApi` object, it's intended to be internally used by `MegaApiPHP`, that solves
all memory management issues for you.

Since the MEGA SDK uses a worker thread to send callbacks to apps, that could cause crashes if you
don't use a thread-safe version of PHP (ZTE). For that reason, The example app shows a warning if 
your version of PHP doesn't have ZTE enabled.

