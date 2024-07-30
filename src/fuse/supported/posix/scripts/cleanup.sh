#!/usr/bin/env bash

# A pipeline only succeeds if all of its parts succeed.
set -o pipefail

# User didn't specify where we should search for mounts.
ERROR_BAD_ARGUMENTS=1

# Couldn't unmount one or more filesystems.
ERROR_COULDNT_UNMOUNT_FILESYSTEM=2

# Couldn't locate (or access) a directory we need.
ERROR_DIRECTORY_NOT_FOUND=3

# Couldn't locate a program that we need.
ERROR_PROGRAM_NOT_FOUND=4

# Everything executed successfully.
ERROR_SUCCESS=0

# Where can we can find libfuse's control filesystem?
FUSE_CONTROL_PREFIX=/sys/fs/fuse/connections

# die(message, result)
#
# Print MESSAGE to the standard error output and terminate the
# program, returning RESULT to the shell.
die()
{
    local message="$1"
    local result=$2

    printf "%s: %s\n" ERROR "$message" >&2

    exit $result
}

# ensure_directory(path)
#
# Checks that PATH exists and denotes a directory. If not, this function
# terminates the program and returns the error ERROR_DIRECTORY_NOT_FOUND to
# the shell.
ensure_directory()
{
    local path="$1"

    test -d "$path" \
      || die "Couldn't access directory \"$path\"." \
             ${ERROR_DIRECTORY_NOT_FOUND}
}

# ensure_program(program)
#
# Checks that PROGRAM is present in the shell's search path and if not,
# terminates the program and returns the error ERROR_PROGRAM_NOT_FOUND to
# the shell.
ensure_program()
{
    local program="$1"

    command -v "$program" &> /dev/null \
      || die "Couldn't find required program \"$program.\"" \
             ${ERROR_PROGRAM_NOT_FOUND}
}

# ensure_programs()
#
# Checks that all programs required by this script are present in the
# shell's search path. If any program we need isn't present, this function
# will terminate the program and return the error ERROR_PROGRAM_NOT_FOUND to
# the shell.
ensure_programs()
{
    # So we can parse mount's output.
    ensure_program cut
    ensure_program grep

    # So we can determine which FUSE mounts are active on the system.
    ensure_program mount

    # So we can unmount a FUSE mount.
    ensure_program fusermount

    # So we can determine which FUSE device is associated with a given mount.
    ensure_program stat
}

# main(arguments)
#
# Main entry point.
main()
{
    # Make sure the programs we need are in the shell's search path.
    ensure_programs

    # Make sure libfuse's control filesystem is mounted.
    ensure_directory "$FUSE_CONTROL_PREFIX"

    # Clarity
    local path="$1"

    # The user hasn't specified where we should search for mounts.
    test $# -eq 1 -a -n "$path" \
      || die "You didn't specify where we should search for mounts." \
             ${ERROR_BAD_ARGUMENTS}

    # Make sure we can access the path the user's specified.
    ensure_directory "$path"

    # Try and umount all MEGA-FS filesystems under PATH.
    unmount_all "$path"
}

# mounts(path)
#
# Retrieve a list of all MEGA-FS mounts under PATH.
mounts()
{
    local path="$1"

    # Retrieve a list of all active mounts.
    # Filter that list to contain only MEGA-FS filesystems.
    # Filter that list to consider only those below PATH.
    # Return the paths of those filesystems.
    mount | grep megafs | grep -F "$path" | cut -d ' ' -f 3
}

# unmount(path)
#
# Try and unmount the MEGA-FS filesystem mounted at PATH.
unmount()
{
    local path="$1"

    # Compute the filesystem's device number.
    local device_no=$(stat --cached=always --format=%d "$path")

    # Couldn't determine the filesystem's device number.
    test -z "$device_no" && return

    # Compute filesystem's control path.
    local control_path="$FUSE_CONTROL_PREFIX/$device_no/abort"

    # Try and unmount the filesystem.
    while test -f "$control_path"; do
        # We were able to unmount the filesystem.
        fusermount -u "$path" &> /dev/null && return

        # Filesystems busy: try and abort the mount.
        echo 1 > "$control_path" 2> /dev/null || true

        # Give the system a little time to process the abort.
        sleep 0.25
    done

    # Make sure the filesystem's been unmounted.
    ! test -d "$path"
}

# unmount_all(path)
#
# Try and unmount all MEGA-FS filesystems under PATH.
unmount_all()
{
    local path="$1"
    local result=$ERROR_SUCCESS

    # Try and unmount all MEGA-FS filesystems under PATH.
    mounts "$path" | while read path; do
        if unmount "$path"; then
            printf "Successfully unmounted \"%s\".\n" "$path"
        else
            printf "Unable to unmount \"%s\"\." "$path"
            result=$ERROR_COULDNT_UNMOUNT_FILESYSTEM
        fi
    done

    # Let the caller know if all the filesystems have been unmounted.
    return $result
}

# Transfer control to our entry point.
main "$@"

