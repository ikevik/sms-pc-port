#!/usr/bin/env python3
"""Summarise compiler and linker errors from the clang survey logs.

Reads every *.log in the folder given as the argument (configure, ninja output)
and writes summary.md beside them: the failed unit count, the most common error
messages with an example, and the errors by source area.
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
area_re = re.compile(r'(decomp/libs/[^/]+|decomp/src|platform/[^/]+|src|ext/[^/]+)/')


def norm(msg):
    msg = re.sub(r"'[^']*'", "'X'", msg)
    msg = re.sub(r'"[^"]*"', '"X"', msg)
    return re.sub(r'\d+', 'N', msg).strip()


messages = collections.Counter()
example = {}
areas = collections.Counter()
link_errors = collections.Counter()
failed = 0
total = 0
files = set()
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
            key = norm(m.group('msg'))
            messages[key] += 1
            example.setdefault(key, '%s:%s' % (m.group('file').replace('\\', '/'), m.group('line')))
            f = m.group('file').replace('\\', '/')
            files.add(f)
            a = area_re.search(f + '/')
            areas[a.group(1) if a else 'other'] += 1
            continue
        m = link_re.search(line)
        if m:
            link_errors[norm(m.group('msg'))] += 1

lines = ['# clang survey', '',
         'Failed build steps: %d of %d planned' % (failed, total),
         'Distinct files with compiler errors: %d' % len(files),
         'Compiler errors: %d (%d distinct messages)' % (sum(messages.values()), len(messages)), '']
lines += ['## Most common compiler errors', '']
for msg, n in messages.most_common(30):
    lines.append('- %d x `%s` (e.g. %s)' % (n, msg, example[msg]))
lines += ['', '## Errors by area', '']
for area, n in areas.most_common():
    lines.append('- %s: %d' % (area, n))
if link_errors:
    lines += ['', '## Linker errors', '']
    for msg, n in link_errors.most_common(20):
        lines.append('- %d x `%s`' % (n, msg))
(out / 'summary.md').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))
