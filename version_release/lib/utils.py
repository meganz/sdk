import os


def get_mega_env_vars(*var_names: str) -> dict[str, str]:
    env_vars: dict[str, str] = {}
    for n in var_names:
        env_vars[n] = os.getenv(n, "")

    assert not any(
        value == "" for value in env_vars.values()
    ), f'Missing mandatory env vars: {", ".join(var_names)}'

    return env_vars


def get_mega_env_var(var_name: str, mandatory: bool = False) -> str:
    env_var = os.getenv(var_name, "")
    assert env_var != "" or not mandatory, f"Missing mandatory env var {var_name}"
    return env_var
