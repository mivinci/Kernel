#!/usr/bin/env python3
"""Explicit retention of generated run directories; preview unless --apply."""

import argparse
from collections import Counter, defaultdict, deque
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import time
from run import run_guard
from build import BOOT_CASES

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = re.compile(r'(?:suite-)?[0-9a-f]{32}\Z')
PATH = re.compile(r'(?:[^\s"\'<>]*?/)?build/[\w./-]+')


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, child in value.items():
            yield str(key)
            yield from strings(child)
    elif isinstance(value, list):
        for child in value:
            yield from strings(child)


def read(path):
    return json.loads(path.read_text())


def directories(build):
    """Only known run containers; never traverse artifacts, boards or host stages."""
    containers = [build / 'host/runs']
    for arch in ('riscv', 'arm64'):
        base = build / arch
        if base.is_symlink():
            raise ValueError('architecture directory must not be a symlink: ' + str(base))
        containers.extend(base / name for name in ('runs', 'smoke', 'bootpkg',
                          'packageboot', 'earlyconsole', 'cacheaudit', 'cpus'))
        containers.extend(base / name / 'runs' for name in (*BOOT_CASES, 'sdk'))
    result = []
    for container in sorted(set(containers)):
        parents = [container] + [p for p in container.parents if build in p.parents]
        if any(p.is_symlink() for p in parents) or not container.is_dir():
            continue
        for p in container.iterdir():
            if IDENTITY.fullmatch(p.name) and p.is_dir() and not p.is_symlink():
                result.append(p)
    return sorted(set(result))


def jsonfiles(directory):
    for base, dirs, files in os.walk(directory, followlinks=False):
        for name in files:
            if name.endswith('.json'):
                p = Path(base) / name
                if not p.is_symlink():
                    yield p


def completion(directory):
    """Separate failure evidence from incomplete or successful runs."""
    records = list(jsonfiles(directory))
    if any((directory / name).is_symlink() for name in ('result.json', 'suite.json')):
        return 'incomplete'
    if any(p.name == 'failure.json' for p in records):
        return 'failed'
    for p in records:
        if p.name not in ('commands.json', 'result.json', 'suite.json'):
            continue
        try:
            value = read(p)
        except (ValueError, OSError):
            return 'unreadable'
        items = value if isinstance(value, list) else [value]
        if (items and any(not isinstance(item, dict) or item.get('ok') is False or
                          any(item.get(name) not in (None, '') for name in ('error', 'cleanup_error'))
                          for item in items)):
            return 'failed'
        if (not items or any(not isinstance(item, dict) or item.get('ok') is not True
                             for item in items)):
            return 'failed-or-incomplete'
    final = directory / 'suite.json' if (directory / 'suite.json').is_file() else directory / 'result.json'
    if not final.is_file():
        return 'incomplete'
    try:
        value = read(final)
    except (ValueError, OSError):
        return 'unreadable'
    items = value if isinstance(value, list) else [value]
    if (items and any(not isinstance(item, dict) or item.get('ok') is False or
                      any(item.get(name) not in (None, '') for name in ('error', 'cleanup_error'))
                      for item in items)):
        return 'failed'
    if not items or any(not isinstance(item, dict) or item.get('ok') is not True for item in items):
        return 'failed-or-incomplete'
    if final.name == 'suite.json':
        for item in items:
            result = directory / str(item.get('result', ''))
            if result.is_symlink() or not result.is_file():
                return 'failed-or-incomplete'
            try:
                result.relative_to(directory)
            except ValueError:
                return 'failed-or-incomplete'
    # A run directory cannot skip its top-level result merely because it contains
    # success-looking fixture results or an incremental command journal.
    return None


def inventory(directory):
    size = files = 0
    pending = [directory]
    while pending:
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                info = entry.stat(follow_symlinks=False)
                # Allocated size is more useful than sparse pmemsave logical length.
                size += info.st_blocks * 512
                if stat.S_ISDIR(info.st_mode):
                    pending.append(entry.path)
                else:
                    files += 1
    return dict(bytes=size, files=files)


def plan(root, keep=20, grace_hours=24, now=None, measure=True, progress=None):
    root = root.resolve()
    build = root / 'build'
    if build.is_symlink():
        raise ValueError('build must not be a symlink')
    if keep < 0 or not math.isfinite(grace_hours) or grace_hours < 0:
        raise ValueError('keep and grace-hours must be nonnegative')
    now = time.time() if now is None else now
    if (build / 'host').is_symlink():
        raise ValueError('host directory must not be a symlink')
    candidates = directories(build)
    reasons = {p: set() for p in candidates}
    pending = deque()
    visited = set()
    if progress:
        progress(f'Checking retention for {len(candidates)} run directories')

    def protect(p, why):
        if not reasons[p]:
            pending.extend(jsonfiles(p))
        reasons[p].add(why)

    # Current manifests/last-run and every stage evidence are roots of the
    # reference graph. Retained result snapshots in turn pin their build logs.
    pending.extend((build / 'host').glob('stage*/evidence.json'))
    for arch in ('riscv', 'arm64'):
        base = build / arch
        for directory in [base] + ([p for p in base.iterdir()
                                   if p.is_dir() and not p.is_symlink()] if base.is_dir() else []):
            for name in ('manifest.json', 'last-run.json'):
                p = directory / name
                if p.is_file():
                    pending.append(p)
    groups = defaultdict(list)
    for p in candidates:
        info = p.stat()
        groups[p.parent].append((info.st_mtime_ns, p, info.st_mtime))
    for group in groups.values():
        group.sort(reverse=True)
        for _, p, _ in group[:keep]:
            protect(p, 'recent-count')
        for _, p, modified in group:
            if now - modified < grace_hours * 3600:
                protect(p, 'recent-time')
            state = completion(p)
            if state:
                protect(p, state)

    # Prefix lookup avoids comparing every reference to every run directory.
    bypath = {p.relative_to(root).as_posix(): p for p in candidates}
    while pending:
        source = pending.popleft()
        if source in visited:
            continue
        visited.add(source)
        if source.is_symlink():
            raise ValueError('reference record is a symlink: ' + str(source))
        try:
            value = read(source)
        except (ValueError, OSError) as exc:
            raise ValueError('cannot read retention reference ' + str(source)) from exc
        for text in strings(value):
            if 'build/' not in text:
                continue
            text = text.replace(str(root) + '/build/', 'build/')
            for match in PATH.finditer(text):
                path = match.group()
                if path.startswith(str(root) + '/'):
                    path = path[len(str(root)) + 1:]
                elif not path.startswith('build/'):
                    continue
                parts = Path(path).parts
                if '..' in parts:
                    raise ValueError('parent traversal in reference: ' + path)
                for length in range(2, len(parts) + 1):
                    target = bypath.get('/'.join(parts[:length]))
                    if target:
                        protect(target, 'reference:' + str(source.relative_to(root)))
                        break
    if progress and measure:
        progress(f'Measuring {sum(not why for why in reasons.values())} eligible run directories')
    rows = []
    for p in candidates:
        rows.append(dict(path=str(p.relative_to(root)), keep=bool(reasons[p]),
                         reasons=sorted(reasons[p]) or ['unreferenced-old-success'],
                         **(inventory(p) if measure and not reasons[p] else
                            dict(bytes=None if reasons[p] else 0, files=None if reasons[p] else 0))))
    return dict(keep=keep, grace_hours=grace_hours, directories=rows,
                reclaim_bytes=sum(r['bytes'] for r in rows if not r['keep']),
                reclaim_files=sum(r['files'] for r in rows if not r['keep']))


def apply(root, report):
    # Re-plan immediately before deletion: new evidence may have pinned an old
    # directory since preview. Never broaden the original deletion set.
    current = plan(root, report['keep'], report['grace_hours'], measure=False)
    allowed = {r['path'] for r in current['directories'] if not r['keep']}
    actual = []
    for row in report['directories']:
        if row['keep'] or row['path'] not in allowed:
            continue
        p = root / row['path']
        if p.is_symlink() or any(parent.is_symlink() for parent in p.parents):
            raise ValueError('symlink in deletion path: ' + str(p))
        shutil.rmtree(p)
        actual.append(row)
    report['reclaim_bytes'] = sum(row['bytes'] for row in actual)
    report['reclaim_files'] = sum(row['files'] for row in actual)
    return [row['path'] for row in actual]


def execute(args):
    def progress(message):
        print('prune: ' + message, file=sys.stderr, flush=True)

    report = plan(ROOT, args.keep, args.grace_hours, progress=progress)
    counts = Counter()
    for row in report['directories']:
        if row['keep']:
            references = {reason for reason in row['reasons']
                          if reason.startswith('reference:') and
                          not reason.partition(':')[2].startswith(row['path'] + '/')}
            counts.update({reason.partition(':')[0] for reason in row['reasons']
                           if not reason.startswith('reference:')})
            if references:
                counts['reference'] += 1
        size = '' if row['keep'] else ' (%d bytes)' % row['bytes']
        print('%s %s%s: %s' % ('KEEP' if row['keep'] else 'DELETE',
              row['path'], size, ', '.join(row['reasons'])))
    if counts:
        print('Retained by reason (counts may overlap): ' + ', '.join(
              f'{reason}={count}' for reason, count in sorted(counts.items())))
    print('%s reclaim: %d bytes, %d files' % ('Reclaim' if args.apply else 'Preview',
          report['reclaim_bytes'], report['reclaim_files']))
    report['dry_run'] = not args.apply
    if args.apply:
        progress('Rechecking retention before deletion')
        report['deleted'] = apply(ROOT, report)
        print('Deleted: %d bytes, %d files' % (report['reclaim_bytes'],
              report['reclaim_files']))
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keep', type=int, default=20)
    parser.add_argument('--grace-hours', type=float, default=24)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        with run_guard(ROOT, exclusive=args.apply):
            execute(args)
    except BlockingIOError:
        parser.exit(1, 'prune: a build or test is active; cleanup was not started\n')
    except (OSError, ValueError) as exc:
        parser.exit(1, 'prune: %s\n' % exc)


if __name__ == '__main__':
    main()
