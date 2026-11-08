# PEPit

## Game .lua comment policy

`SDCardContent/games/**/*.lua` and `WIPSDCardContent/games/**/*.lua` are cached on the device and parsed by the Lua interpreter at runtime: comments cost flash, cache and parse time. No comments unless absolutely essential; then the shortest possible. Never re-add stripped comments. Dev knowledge goes to `TASK_INFO/` notes, never to shipped Lua and never by editing the `create-game` skill (curated; edit only when asked). Bulk-strip with `scripts/strip_lua_comments.py` (token-verified).

## Games

Before creating or modifying a game, load the `create-game` skill.
