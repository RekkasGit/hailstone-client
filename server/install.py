#!/usr/bin/env python3
"""Install (or remove) Hailstone client support on an EQEmu server.

    install.py <server dir>            copy quests/ to <server dir>/hailstone/quests and add it to quest_paths
    install.py <server dir> --remove   take it out of quest_paths and delete <server dir>/hailstone

<server dir> holds eqemu_config.json. EQEmu reads every folder in "quest_paths" and runs global/script_init.lua from
each, so this adds a folder after the server's own quests/ and edits no script of the server's. Restart the zones
(or #reloadquest) afterwards.
"""
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def write_like(path, text):
    """Replaces path with text, keeping its mode and, where allowed, its owner: eqemu_config.json holds the
    database password, and a file made at the umask's mode (often world-readable) would lose an owner's 0600."""
    st = os.stat(path)
    tmp = path + ".tmp"
    try:
        os.remove(tmp)
    except FileNotFoundError:
        pass
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, st.st_mode & 0o777)
    try:
        with os.fdopen(fd, "w") as f:
            if hasattr(os, "fchmod"):
                os.fchmod(f.fileno(), st.st_mode & 0o777)  # os.open's mode is cut by the umask
            if hasattr(os, "fchown"):
                try:
                    os.fchown(f.fileno(), st.st_uid, st.st_gid)
                except PermissionError:
                    pass  # not root: the file is the runner's, still at the old mode
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    remove = "--remove" in sys.argv
    if len(args) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    server = args[0]
    config_path = os.path.join(server, "eqemu_config.json")
    with open(config_path) as f:
        config = json.load(f)

    dest = os.path.join(server, "hailstone", "quests")
    ours = "hailstone/quests/"
    dirs = config["server"]["directories"]
    # Without "quest_paths" EQEmu uses the single "quests" folder; keep it first so the server's scripts win.
    paths = dirs.get("quest_paths") or [dirs.get("quests", "quests/")]
    paths = [p for p in paths if os.path.normpath(p) != os.path.normpath(ours)]
    if remove:
        shutil.rmtree(os.path.join(server, "hailstone"), ignore_errors=True)
    else:
        shutil.copytree(os.path.join(HERE, "quests"), dest, dirs_exist_ok=True)
        paths.append(ours)
    dirs["quest_paths"] = paths

    write_like(config_path, json.dumps(config, indent=2) + "\n")
    print(f"quest_paths: {paths}")
    print("Restart the zones (or #reloadquest) to load it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
