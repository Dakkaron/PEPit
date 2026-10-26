"""Prepare a PEPit data directory (virtual SD card) for the lua games.

The factory seed in PEPitCompat/shim/filesystem.cpp provisions only the
legacy monster game. For the lua games we stage the real SDCardContent:

  games/farm, games/miner, games/plane   (the active lua games)
  gfx/                                   (shared system graphics)
  profiles.ini                           (real profiles, e.g. "PEP S")
  systemConfig.ini                       (firmware expects camelCase; the
                                          real card ships lowercase
                                          systemconfig.ini which FAT hides
                                          but a Linux data dir does not)

The legacy games (race, horserace, monster) are intentionally left out —
they are being phased out. Anything still missing after we stage our files
is filled by the compat factory seed at boot.
"""

import os
import shutil

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SDCARD_CONTENT = os.path.join(REPO_ROOT, "SDCardContent")

LUA_GAMES = ("farm", "miner", "plane")

# Firmware expects these exact keys (readSystemConfig). Values tuned for
# automated play: no execution logging (that would hang on the interactive
# date/time screen in a session without WiFi), right-handed controls.
SYSTEM_CONFIG = (
    "[system]\n"
    "wifiSSID=\n"
    "wifiPassword=\n"
    "wifiSSID2=\n"
    "wifiPassword2=\n"
    "wifiSSID3=\n"
    "wifiPassword3=\n"
    "trampolineIp=\n"
    "logExecutions=false\n"
    "timezoneOffset=0\n"
    "touchScreenSensitivity=50\n"
    "simulateTrampoline=false\n"
    "simulateBlowing=false\n"
    "simulateInhalation=false\n"
    "debugLogBlowPressure=0\n"
    "debugLogTrampoline=0\n"
    "leftHandMode=0\n"
    "manometerMode=0\n"
)


def prepare_lua_datadir(dest: str, force: bool = False) -> str:
    """Build (or refresh) the lua-games data directory; returns its path."""
    if not os.path.isdir(os.path.join(SDCARD_CONTENT, "games")):
        raise FileNotFoundError(f"SDCardContent not found at {SDCARD_CONTENT}")

    if force and os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest, exist_ok=True)

    for game in LUA_GAMES:
        src = os.path.join(SDCARD_CONTENT, "games", game)
        dst = os.path.join(dest, "games", game)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)

    src_gfx = os.path.join(SDCARD_CONTENT, "gfx")
    dst_gfx = os.path.join(dest, "gfx")
    if os.path.isdir(dst_gfx):
        shutil.rmtree(dst_gfx)
    shutil.copytree(src_gfx, dst_gfx)

    for name in ("profiles.ini",):
        src = os.path.join(SDCARD_CONTENT, name)
        if os.path.isfile(src):
            shutil.copyfile(src, os.path.join(dest, name))

    with open(os.path.join(dest, "systemConfig.ini"), "w") as f:
        f.write(SYSTEM_CONFIG)

    return dest


if __name__ == "__main__":
    import sys
    dest = sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/pepit_data_lua"
    print(prepare_lua_datadir(dest, force=True))
