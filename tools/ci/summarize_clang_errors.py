#!/usr/bin/env python3
"""Summarise compiler and linker errors from the clang survey logs.

Reads every *.log in the folder given as the argument (configure, ninja output)
and writes summary.md beside them. Errors are split into those in the repo's own
files and those inside system headers (Windows SDK, MSVC, LLVM): a header error
repeats once per file that includes it, so each location is counted once.
"""
import collections
import pathlib
import re
import sys

out = pathlib.Path(sys.argv[1])
err_re = re.compile(r'^(?P<file>.*?):(?P<line>\d+):(?P<col>\d+): (?:fatal )?error: (?P<msg>.*)$')
link_re = re.compile(r'(?:lld-link|ld\.lld|link)\s*: (?:fatal )?error: (?P<msg>.*)$', re.I)
failed_re = re.compile(r'^FAILED: ')
progress_re = re.compile(r'^\[(\d+)/(\d+)\]')
area_re = re.compile(r'(decomp/libs/[^/]+|decomp/src|build-clang/cp932/[^/]+/[^/]+|build-clang/patched/[^/]+|platform/[^/]+|src|ext/[^/]+)/')
SYSTEM = ('Windows Kits', 'Visual Studio', '/LLVM/', '/clang/', '/Llvm/')


def norm(msg):
    msg = re.sub(r"'[^']*'", "'X'", msg)
    msg = re.sub(r'"[^"]*"', '"X"', msg)
    return re.sub(r'\d+', 'N', msg).strip()


repo = collections.defaultdict(set)      # message -> {file:line}
system = collections.defaultdict(set)
areas = collections.defaultdict(set)
link_errors = collections.Counter()
failed = 0
total = 0
for log in sorted(out.glob('*.log')):
    for raw in log.read_text(errors='replace').splitlines():
        line = raw.strip()
        if failed_re.match(line):
            failed += 1
        m = progress_re.match(line)
        if m:
            total = max(total, int(m.group(2)))
        m = err_re.match(line)
        if m:
            f = m.group('file').replace('\\', '/')
            where = '%s:%s' % (f, m.group('line'))
            key = norm(m.group('msg'))
            if any(s in f for s in SYSTEM):
                system[key].add(where)
            else:
                repo[key].add(where)
                a = area_re.search(f + '/')
                areas[a.group(1) if a else 'other'].add(where + key)
            continue
        m = link_re.search(line)
        if m:
            link_errors[norm(m.group('msg'))] += 1

lines = ['# clang survey', '',
         'Failed build steps: %d of %d planned' % (failed, total),
         'Error locations in the repo\'s own files: %d (%d distinct messages)'
         % (sum(len(v) for v in repo.values()), len(repo)),
         'Error locations inside system headers: %d (%d distinct messages)'
         % (sum(len(v) for v in system.values()), len(system)), '']
lines += ['## Errors in the repo\'s files, by number of locations', '']
for msg, wh in sorted(repo.items(), key=lambda kv: -len(kv[1]))[:40]:
    lines.append('- %d x `%s` (e.g. %s)' % (len(wh), msg, sorted(wh)[0]))
lines += ['', '## Repo errors by area (distinct locations)', '']
for area, wh in sorted(areas.items(), key=lambda kv: -len(kv[1])):
    lines.append('- %s: %d' % (area, len(wh)))
lines += ['', '## Errors inside system headers (usually one root cause, like the wrong C++ standard)', '']
for msg, wh in sorted(system.items(), key=lambda kv: -len(kv[1]))[:12]:
    lines.append('- %d x `%s` (e.g. %s)' % (len(wh), msg, sorted(wh)[0]))
if link_errors:
    lines += ['', '## Linker errors', '']
    for msg, n in link_errors.most_common(25):
        lines.append('- %d x `%s`' % (n, msg))
(out / 'summary.md').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))
