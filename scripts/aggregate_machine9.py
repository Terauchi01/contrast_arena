#!/usr/bin/env python3
"""
Aggregate results for machine9-* runs.

Scans logs/machine9*/server*.log and prints counts for the fixed match list
in the requested order. For each match it prints: X:<wins> O:<wins> D:<draws> Total:<games>

Usage: python3 scripts/aggregate_machine9.py [logs_dir_prefix]
If no arg is given, defaults to 'logs'.
"""
import re
import glob
import os
import sys
from collections import defaultdict

BASE_LOG_DIR = sys.argv[1] if len(sys.argv) > 1 else 'logs'
# match locations like logs/machine9, logs/machine9-1, logs/machine9-4, etc.
LOG_GLOB = os.path.join(BASE_LOG_DIR, 'machine9*', 'server*.log')

# fixed ordered matches (left vs right)
MATCH_ORDER = [
    ('alphabeta', 'alphazero'),
    ('alphabeta', 'alphabeta'),
    ('alphabeta', 'mcts'),
    ('alphabeta', 'ntuple'),
    ('alphabeta', 'rulebased2'),
    ('rulebased2', 'alphazero'),
    ('rulebased2', 'rulebased2'),
    ('rulebased2', 'alphabeta'),
    ('rulebased2', 'mcts'),
]

winner_re = re.compile(r'^Winner:\s*(\w+)', re.IGNORECASE)
score_re = re.compile(r'\(([^)]+)\s+vs\s+([^)]+)\)')
role_suffix_re = re.compile(r'[_\-](X|O)$', re.IGNORECASE)

def norm_name(name):
    # remove role suffix if present, normalize common patterns
    if '_' in name:
        base, suffix = name.rsplit('_', 1)
        m = re.match(r'^([OX])(\d+)$', suffix, re.IGNORECASE)
        if m:
            return base
        # remove trailing digits
        suffix_norm = re.sub(r'\d+', '', suffix)
        return base
    return name

# stats keyed by (left, right)
stats = defaultdict(lambda: {'X': 0, 'O': 0, 'D': 0, 'games': 0})

for path in glob.glob(LOG_GLOB):
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            pending_winner = None
            for line in f:
                line = line.strip()
                m = winner_re.match(line)
                if m:
                    pending_winner = m.group(1).upper()
                    continue
                s = score_re.search(line)
                if s and pending_winner:
                    left = s.group(1).strip()
                    right = s.group(2).strip()
                    left_base = norm_name(role_suffix_re.sub('', left))
                    right_base = norm_name(role_suffix_re.sub('', right))

                    key = (left_base, right_base)
                    stats[key]['games'] += 1
                    if pending_winner == 'X':
                        stats[key]['X'] += 1
                    elif pending_winner == 'O':
                        stats[key]['O'] += 1
                    else:
                        stats[key]['D'] += 1
                    pending_winner = None
    except Exception as e:
        print(f'# failed to read {path}: {e}', file=sys.stderr)

# Now produce CSV output for MATCH_ORDER
print('left,right,X_wins,O_wins,Draws,Total')
for left, right in MATCH_ORDER:
    key = (left, right)
    entry = stats.get(key, {'X': 0, 'O': 0, 'D': 0, 'games': 0})
    print(f'{left},{right},{entry["X"]},{entry["O"]},{entry["D"]},{entry["games"]}')
