#!/usr/bin/env python3
import glob
import os
import re
import sys

LOG_GLOB = os.path.join(sys.argv[1] if len(sys.argv)>1 else 'logs', '**', 'server*.log')
winner_re = re.compile(r'^Winner:\s*(\w+)', re.IGNORECASE)

files = sorted(glob.glob(LOG_GLOB, recursive=True))
if not files:
    print('# No log files found for pattern:', LOG_GLOB)
    sys.exit(0)

for path in files:
    xwins = 0
    owins = 0
    draws = 0
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = winner_re.match(line.strip())
                if m:
                    w = m.group(1).upper()
                    if w == 'X':
                        xwins += 1
                    elif w == 'O':
                        owins += 1
                    elif w == 'DRAW' or w == 'TIE':
                        draws += 1
        rel = os.path.relpath(path)
        print(f"{rel}\tX:{xwins}\tO:{owins}\tD:{draws}")
    except Exception as e:
        print(f"# Failed to read {path}: {e}", file=sys.stderr)
