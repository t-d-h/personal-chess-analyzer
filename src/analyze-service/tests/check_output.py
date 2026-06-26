#!/usr/bin/env python3
"""Validate analyze-game output JSON against structural and range checks.

Usage: check_output.py <output.json>

Checks:
  1. JSON is valid and has 'moves' and 'playerSummaries' keys
  2. Each move has required numeric fields with sensible ranges
  3. winPercentLoss >= 0 for every move
  4. moveAccuracy in [0, 100]
  5. classification is one of the known values
  6. playerSummaries have correct totalMoves count
  7. accuracyPct in [0, 100]
"""

import json
import sys

VALID_CLASSIFICATIONS = {
    "book", "best", "excellent", "good",
    "inaccuracy", "mistake", "blunder", "brilliant",
}

def check_move(m, idx):
    errors = []
    for key in ("ply", "color", "san", "fenBefore", "fenAfter",
                "evalCpPlayed", "evalCpBest", "bestMoveUci",
                "winPercentLoss", "moveAccuracy", "classification", "engineDepth"):
        if key not in m:
            errors.append(f"move[{idx}]: missing key '{key}'")

    if errors:
        return errors

    if m["ply"] != idx + 1:
        errors.append(f"move[{idx}]: ply={m['ply']} expected {idx+1}")

    if m["color"] not in ("white", "black"):
        errors.append(f"move[{idx}]: invalid color '{m['color']}'")

    cp_played = m["evalCpPlayed"]
    if cp_played is not None and abs(cp_played) > 10000:
        errors.append(f"move[{idx}]: evalCpPlayed={cp_played} out of range")

    if m["winPercentLoss"] < -0.01:
        errors.append(f"move[{idx}]: winPercentLoss={m['winPercentLoss']} negative")

    acc = m["moveAccuracy"]
    if acc < -0.1 or acc > 100.1:
        errors.append(f"move[{idx}]: moveAccuracy={acc} out of [0,100]")

    if m["classification"] not in VALID_CLASSIFICATIONS:
        errors.append(f"move[{idx}]: invalid classification '{m['classification']}'")

    if m["engineDepth"] < 1:
        errors.append(f"move[{idx}]: engineDepth={m['engineDepth']} too low")

    return errors

def main():
    if len(sys.argv) < 2:
        print("Usage: check_output.py <output.json>", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1]) as f:
        data = json.load(f)

    errors = []

    if "moves" not in data:
        errors.append("Missing 'moves' key")
    if "playerSummaries" not in data:
        errors.append("Missing 'playerSummaries' key")

    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    moves = data["moves"]
    if len(moves) == 0:
        errors.append("No moves in output")

    for i, m in enumerate(moves):
        errors.extend(check_move(m, i))

    summaries = data["playerSummaries"]
    if len(summaries) != 2:
        errors.append(f"Expected 2 playerSummaries, got {len(summaries)}")

    for s in summaries:
        if s["color"] not in ("white", "black"):
            errors.append(f"Invalid summary color '{s['color']}'")
        if s["accuracyPct"] < -0.1 or s["accuracyPct"] > 100.1:
            errors.append(f"{s['color']}: accuracyPct={s['accuracyPct']} out of range")
        if s["acpl"] < -0.1:
            errors.append(f"{s['color']}: acpl={s['acpl']} negative")

    white_moves = [m for m in moves if m["color"] == "white"]
    black_moves = [m for m in moves if m["color"] == "black"]

    for s in summaries:
        if s["color"] == "white" and s["totalMoves"] != len(white_moves):
            errors.append(f"white totalMoves={s['totalMoves']} but found {len(white_moves)} white moves")
        if s["color"] == "black" and s["totalMoves"] != len(black_moves):
            errors.append(f"black totalMoves={s['totalMoves']} but found {len(black_moves)} black moves")

    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"VALID: {len(moves)} moves, white accuracy={summaries[0]['accuracyPct']:.1f}%, "
          f"black accuracy={summaries[1]['accuracyPct']:.1f}%")

if __name__ == "__main__":
    main()
