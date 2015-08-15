# Filesystem driver based on FUSE

Example implementation of a filesystem driver based on FUSE

This driver allows to:

- Mount any folder (including shared folders) in a local computer
- Navigate along the folder structure
- Create new folders
- Delete, rename and move files/folders
- Read data of files

File writes aren't supported yet. Also, the implementation doesn't cache data 
nor does any prefetching to improve read performance so the performance will be
lower than it could be.

## How to build and run the project:

To build and run the project, follow theses steps:

1. Build the SDK including the option `--with-fuse` in the `./configure` step
2. Run the `megafuse` binary that you will find in this folder

- If you run it without parameters, it will interactively request the required data 
- You can automate it providing additional parameters: 

  `megafuse [megauser megapassword localmountpoint [megamountpoint]]`
