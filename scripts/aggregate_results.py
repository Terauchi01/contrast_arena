#!/usr/bin/env python3
import re
import glob
import os
import sys
from collections import defaultdict

# Usage: python3 scripts/aggregate_results.py [logs_dir]
# Scans logs/**/server*.log and summarizes results into a matrix.

LOG_GLOB = os.path.join(sys.argv[1] if len(sys.argv) > 1 else 'logs', '**', 'server*.log')
TYPES_ORDER = ['alphabeta', 'alphazero', 'mcts', 'ntuple', 'rulebased2']

winner_re = re.compile(r'^Winner:\s*(\w+)', re.IGNORECASE)
score_re = re.compile(r'\(([^)]+)\s+vs\s+([^)]+)\)')
role_suffix_re = re.compile(r'[_\-](X|O)$', re.IGNORECASE)

# wins[a][b] holds stats from perspective of player a against player b
# each entry: {'as_X': int, 'as_O': int, 'draws': int, 'games': int}
def make_stats():
    return {'as_X': 0, 'as_O': 0, 'draws': 0, 'games': 0}
matrix = defaultdict(lambda: defaultdict(lambda: make_stats()))
seen_names = set()

for path in glob.glob(LOG_GLOB, recursive=True):
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            pending_winner = None
            for line in f:
                line = line.strip()
                m = winner_re.match(line)
                if m:
                    pending_winner = m.group(1)
                    continue
                s = score_re.search(line)
                if s and pending_winner:
                    left = s.group(1).strip()
                    right = s.group(2).strip()
                    # detect explicit role suffix _X/_O if present, otherwise infer by position
                    lm = role_suffix_re.search(left)
                    rm = role_suffix_re.search(right)
                    if lm:
                        left_role = lm.group(1).upper()
                        left_base_raw = role_suffix_re.sub('', left)
                    else:
                        left_role = 'X'
                        left_base_raw = left
                    if rm:
                        right_role = rm.group(1).upper()
                        right_base_raw = role_suffix_re.sub('', right)
                    else:
                        right_role = 'O'
                        right_base_raw = right
                    seen_names.add(left_base_raw)
                    seen_names.add(right_base_raw)
                    # normalize to simple tokens (keep original if not matching known types)
                    def norm(name):
                        # Normalize by handling suffixes after the last underscore.
                        # If there is an underscore, keep base as-is and normalize suffix:
                        #  - If suffix matches O\d+ or X\d+ -> keep O or X
                        #  - Otherwise remove digits from suffix (keep letters like A/B)
                        # If there is no underscore, leave name unchanged (preserve bases like rulebased2)
                        if '_' in name:
                            base, suffix = name.rsplit('_', 1)
                            m = re.match(r'^([OX])(\d+)$', suffix, re.IGNORECASE)
                            if m:
                                suffix_norm = m.group(1).upper()
                            else:
                                suffix_norm = re.sub(r'\d+', '', suffix)
                            return base + '_' + suffix_norm
                        return name
                    L = norm(left_base_raw)
                    R = norm(right_base_raw)
                    # decide winner and update per-player-perspective stats
                    w = pending_winner.upper()
                    # increment games for both perspectives
                    matrix[L][R]['games'] += 1
                    matrix[R][L]['games'] += 1
                    if w == 'X':
                        # X won: increment winner's as_X
                        if left_role == 'X':
                            matrix[L][R]['as_X'] += 1
                        else:
                            matrix[R][L]['as_X'] += 1
                    elif w == 'O':
                        if left_role == 'O':
                            matrix[L][R]['as_O'] += 1
                        else:
                            matrix[R][L]['as_O'] += 1
                    else:
                        matrix[L][R]['draws'] += 1
                        matrix[R][L]['draws'] += 1
                    pending_winner = None
    except Exception as e:
        print(f'# Failed to read {path}: {e}', file=sys.stderr)

# Prepare rows/cols in requested order plus any extra discovered names
all_types = []
for t in TYPES_ORDER:
    if t in seen_names or True:
        all_types.append(t)
# also append any seen names not in TYPES_ORDER
for n in sorted(seen_names):
    if n not in all_types:
        all_types.append(n)

# Print table: each cell shows wins when row-player plays X and when row-player plays O against column-player
print('\t' + '\t'.join(all_types))
for r in all_types:
    row = [r]
    for c in all_types:
        stats = matrix[r][c]
        cell = f"X:{stats['as_X']} O:{stats['as_O']} ({stats['games']})"
        row.append(cell)
    print('\t'.join(row))

print("\n# Note: rows = player A, columns = player B. Cell shows A's wins as X and as O versus B (total games between them)")
