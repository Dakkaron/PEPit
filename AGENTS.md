# PEPit repository guidelines

## Game Lua files: comment policy

Game scripts (`SDCardContent/games/**/*.lua`, `WIPSDCardContent/games/**/*.lua`) run on the device: the firmware caches the source and the Lua interpreter parses it on the PEPit, so every byte of comment costs flash, cache space and parse time at runtime.

- No comments in game `.lua` files unless absolutely essential for understanding the code.
- An essential comment must be as short as possible.
- Never re-add stripped comments. Development knowledge about the firmware API and game conventions belongs in `.opencode/skills/`, not in shipped Lua.
- The existing scripts were comment-stripped with `scripts/strip_lua_comments.py`, which token-verifies that stripping changed no code; use it for any future bulk stripping.

## Working on games

Before creating or modifying a game under `SDCardContent/games/` or `WIPSDCardContent/games/`, load the `create-game` skill: it documents the game directory layout, `gameconfig.ini` keys, firmware-injected globals, blow/jump semantics, Prefs persistence and how to test in the PEPitCompat emulator.
