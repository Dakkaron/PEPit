"""PEPit policy runner — a fast "System 1" loop that plays the game for us.

Layered under the MCP control channel (see ../server.py):

  ControlChannel  : rename-protocol control file + screenshot channel
  Extractor       : deterministic numpy pixel scan of 320x240 frames
  GameSpec        : per-game knowledge (lua games: miner / plane / farm)
  PolicyRunner    : navigate -> play loop (extract -> decide -> act) -> finish

Decision sources (pluggable):
  rulebot : deterministic heuristics (default baseline)
  llm     : LM Studio /v1/systemone decision endpoint (text and/or image)
"""
