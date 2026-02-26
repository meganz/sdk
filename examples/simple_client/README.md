# simple_client

C++ console app using the public API of MEGA SDK ([include/megaapi.h](../../include/megaapi.h)).

This example logs in to your MEGA account, gets your MEGA filesystem, shows the files/folders in your root folder,
uploads an image and optionally starts a local HTTP proxy server to browse you MEGA account.

[simple_client.cpp](simple_client.cpp) must be edited to replace values of `MEGA_EMAIL` and `MEGA_PASSWORD` with real credentials.

Optionally in the same file, more MegaApi calls can be added after the comment `// Add code here to exercise MegaApi`.
