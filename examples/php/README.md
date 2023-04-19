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

To build and run the project with PHP bindings, please refer to this section: 
[Build/Install PHP Bindings](https://github.com/meganz/sdk/tree/develop/bindings/php)

After that, having a terminal open in this folder run `composer install` to install dependencies.

Finally, open the example console app running `php megacli.php`.

