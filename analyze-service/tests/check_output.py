import sys
import json

def compare_values(actual, expected, tolerance=0.0):
    if actual is None and expected is None:
        return True
    if actual is None or expected is None:
        return False
    return abs(actual - expected) <= tolerance

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 check_output.py <expected_json_file>")
        sys.exit(1)

    expected_file = sys.argv[1]
    with open(expected_file, 'r') as f:
        expected = json.load(f)

    try:
        actual = json.load(sys.stdin)
    except Exception as e:
        print(f"Failed to parse actual JSON from stdin: {e}")
        sys.exit(1)

    # Check moves length
    if len(actual.get('moves', [])) != len(expected.get('moves', [])):
        print(f"Moves count mismatch: actual={len(actual.get('moves', []))}, expected={len(expected.get('moves', []))}")
        sys.exit(1)

    for i, (act_move, exp_move) in enumerate(zip(actual['moves'], expected['moves'])):
        ply = act_move.get('ply')
        # Exact checks
        for key in ['ply', 'color', 'san', 'classification', 'evalMate', 'bestMoveSan']:
            if act_move.get(key) != exp_move.get(key):
                print(f"Move mismatch at ply {ply} for '{key}': actual={act_move.get(key)}, expected={exp_move.get(key)}")
                sys.exit(1)
        
        # Numeric checks with tolerance
        if not compare_values(act_move.get('evalCpPlayed'), exp_move.get('evalCpPlayed'), 20):
            print(f"Move mismatch at ply {ply} for 'evalCpPlayed': actual={act_move.get('evalCpPlayed')}, expected={exp_move.get('evalCpPlayed')}")
            sys.exit(1)

        if not compare_values(act_move.get('evalCpBest'), exp_move.get('evalCpBest'), 20):
            print(f"Move mismatch at ply {ply} for 'evalCpBest': actual={act_move.get('evalCpBest')}, expected={exp_move.get('evalCpBest')}")
            sys.exit(1)

        if not compare_values(act_move.get('winPercentLoss'), exp_move.get('winPercentLoss'), 3.0):
            print(f"Move mismatch at ply {ply} for 'winPercentLoss': actual={act_move.get('winPercentLoss')}, expected={exp_move.get('winPercentLoss')}")
            sys.exit(1)

        if not compare_values(act_move.get('moveAccuracy'), exp_move.get('moveAccuracy'), 3.0):
            print(f"Move mismatch at ply {ply} for 'moveAccuracy': actual={act_move.get('moveAccuracy')}, expected={exp_move.get('moveAccuracy')}")
            sys.exit(1)

    # Check playerSummaries
    act_summs = actual.get('playerSummaries', [])
    exp_summs = expected.get('playerSummaries', [])
    if len(act_summs) != len(exp_summs):
        print(f"Player summaries count mismatch: actual={len(act_summs)}, expected={len(exp_summs)}")
        sys.exit(1)

    for act_s, exp_s in zip(act_summs, exp_summs):
        color = act_s.get('color')
        if act_s.get('color') != exp_s.get('color'):
            print(f"Player summary color mismatch: actual={act_s.get('color')}, expected={exp_s.get('color')}")
            sys.exit(1)

        # Exact checks
        for key in ['bestCount', 'excellentCount', 'goodCount', 'inaccuracyCount', 'mistakeCount', 'blunderCount', 'brilliantCount']:
            if act_s.get(key) != exp_s.get(key):
                print(f"Player summary '{key}' mismatch for {color}: actual={act_s.get(key)}, expected={exp_s.get(key)}")
                sys.exit(1)

        # Tolerant checks
        if not compare_values(act_s.get('accuracyPct'), exp_s.get('accuracyPct'), 3.0):
            print(f"Player summary 'accuracyPct' mismatch for {color}: actual={act_s.get('accuracyPct')}, expected={exp_s.get('accuracyPct')}")
            sys.exit(1)

        if not compare_values(act_s.get('acpl'), exp_s.get('acpl'), 20):
            print(f"Player summary 'acpl' mismatch for {color}: actual={act_s.get('acpl')}, expected={exp_s.get('acpl')}")
            sys.exit(1)

    print("All checks passed successfully!")

if __name__ == '__main__':
    main()
