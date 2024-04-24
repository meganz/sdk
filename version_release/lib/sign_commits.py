import platform, subprocess
from pathlib import Path


def setup_gpg_signing(gpg_keygrip: str, gpg_password: str):
    if platform.system() == "Windows":
        print(
            "WARNING: Setup for gpg signing not implemented on Windows. Considering it done..."
        )
        return

    # Caching password must be allowed:
    # echo allow-preset-passphrase  >> $(gpgconf --list-dirs homedir)/gpg-agent.conf
    _check_gpg_pass_caching()

    # Cache password for key (from 'nix shell, git-bash on Win etc.):
    # $(gpgconf --list-dirs libexecdir)/gpg-preset-passphrase -c <keygrip> <<EOF
    # <password>
    # EOF
    byte_output = subprocess.check_output(["gpgconf", "--list-dirs", "libexecdir"])
    gpg_libexec = byte_output.decode("utf-8").strip()
    gpg_proc = subprocess.Popen(
        [gpg_libexec + "/gpg-preset-passphrase", "-c", gpg_keygrip],
        stdin=subprocess.PIPE,
    )
    assert gpg_proc.stdin is not None
    gpg_proc.stdin.write(gpg_password.encode())
    gpg_proc.stdin.close()


def _check_gpg_pass_caching():
    byte_output = subprocess.check_output(["gpgconf", "--list-dirs", "homedir"])
    gpg_home = byte_output.decode("utf-8").strip()
    gpg_conf = Path(gpg_home) / "gpg-agent.conf"
    allow_preset_passphrase = "allow-preset-passphrase"

    if gpg_conf.is_file():
        with gpg_conf.open("r+") as f:
            while line := f.readline():
                if line.strip() == allow_preset_passphrase:
                    return
            f.write(f"\n{allow_preset_passphrase}\n")
    else:
        gpg_conf.write_text(f"{allow_preset_passphrase}\n")

    assert subprocess.run(
        ["gpg-connect-agent", "reloadagent", "/bye"], stdout=subprocess.DEVNULL
    ), "Failed to restart gpg-connect-agent"
