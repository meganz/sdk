# SDK Build Directory

This directory contains all necessary scripts and templates for building the SDK application across multiple distributions, including Debian, RPM, and Arch Linux.

## Directory Contents

- **create_tarball.sh**: A script to generate the tarball for the SDK package.
- **Debian-specific files**:
  - `debian.postinst`: Post-installation script for the Debian package.
  - `debian.prerm`: Pre-removal script for the Debian package.
  - `debian.postrm`: Post-removal script for the Debian package.
  - `debian.rules`: Defines the Debian package build steps using `cmake`.
  - `debian.changes`: Log file containing details of changes between Debian package versions.
  - `debian.install`: Specifies which files to install and their destination directories during package installation.

- **RPM-specific template**:
  - `megasdk.spec`: RPM spec file template that contains the build instructions using `cmake`.

- **Arch-specific template**:
  - `PKGBUILD`: Build script for creating the Arch Linux package.

- **Common template**:
  - `megasdk.dsc`: Template for the Debian source control file.

Currently, these packages are not in use and serve no immediate practical purpose. However, in the future, they may become useful. The goal is to ensure that we are using the same build system as the SDK clients, so that we can debug locally and respond quickly in case of a failure.

## Building the SDK

To build the SDK, the process varies slightly based on the distribution. Below are the commands used in both `megasdk.spec` (RPM) and `debian.rules` (Debian) to configure and initiate the build:

```bash
cmake ${vcpkg_root} -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B %{_builddir}/build_dir
```

Explanation of CMake Command:

    ${vcpkg_root}: Path to the vcpkg installation or root directory.
    -DCMAKE_VERBOSE_MAKEFILE=ON: Enables verbose output for makefile generation, helpful for debugging.
    -DCMAKE_BUILD_TYPE=RelWithDebInfo: Builds the SDK in Release mode with Debug information included.
    -S .: Specifies the source directory for the build (the current directory).
    -B %{_builddir}/build_dir: Specifies the build directory where all compilation artifacts will be placed.
    

## Usage Instructions

    - Clone the SDK repository.
    - Modify the templates (megasdk.dsc, megasdk.spec, PKGBUILD) as necessary for your distribution.
    - Run the create_tarball.sh script to package the SDK.
    - Build the package using the appropriate packaging system (Debian, RPM, or Arch) by following their respective guidelines.

For further information, refer to the specific files or the documentation related to your distribution's packaging system.