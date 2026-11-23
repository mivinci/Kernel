"""SDK headers: allowlisted source roots, exact names and contents."""
from pathlib import Path
import shutil

from build import BuildError, digest

# Keep established include paths while discovering every header below each root.
PRIVATE_ROOTS = (('lib/libsys/private', ''), ('lib/libc/private', ''),
                 ('lib/libvirtio/private', 'virtio'))


def private_headers(root):
    result = {}
    for directory, prefix in PRIVATE_ROOTS:
        source_root = root / directory
        if not source_root.is_dir() or source_root.is_symlink():
            raise BuildError('SDK private source directory missing or symlink: ' + directory)
        for source in sorted(source_root.rglob('*')):
            if source.is_symlink():
                raise BuildError('SDK private source symlink: ' + str(source))
            if not source.is_file() or source.suffix != '.h':
                continue
            name = (Path(prefix) / source.relative_to(source_root)).as_posix()
            if name in result:
                raise BuildError('SDK private header collision: ' + name + ' from ' +
                                 str(result[name]) + ' and ' + str(source))
            result[name] = source
    return result


def check_private_headers(root, sdk):
    return check_headers(root, sdk / 'private', 'private', private_headers(root))


def public_headers(root):
    result = {}
    for directory, patterns in (('include', ('types.h', 'abi/**/*.h', 'proto/*.h', 'abi/*.inc')),
                                ('lib/libsys/include', ('sys/*.h',)),
                                ('lib/libc/include', ('**/*.h',))):
        source_root = root / directory
        if not source_root.is_dir() or source_root.is_symlink():
            raise BuildError('SDK public source directory missing or symlink: ' + directory)
        for pattern in patterns:
            for source in sorted(source_root.glob(pattern)):
                relative = source.relative_to(source_root)
                if any((source_root / path).is_symlink() for path in (relative, *relative.parents)):
                    raise BuildError('SDK public source symlink: ' + str(source))
                name = relative.as_posix()
                if not source.is_file() or name in result:
                    raise BuildError('invalid or duplicate SDK public header: ' + name)
                result[name] = source
    return result


def check_public_headers(root, sdk):
    return check_headers(root, sdk / 'include', 'public', public_headers(root))


def check_headers(root, directory, kind, expected):
    if directory.is_symlink():
        raise BuildError(f'SDK {kind} export directory is a symlink: {directory}')
    actual = {}
    if directory.exists():
        for target in sorted(directory.rglob('*')):
            if target.is_symlink():
                raise BuildError(f'SDK {kind} export symlink: {target}')
            if target.is_file():
                actual[target.relative_to(directory).as_posix()] = target
    missing = sorted(expected.keys() - actual.keys())
    extra = sorted(actual.keys() - expected.keys())
    changed = sorted(name for name in expected.keys() & actual.keys()
                     if expected[name].read_bytes() != actual[name].read_bytes())
    differences = [('missing', missing), ('extra', extra), ('changed', changed)]
    if any(names for _, names in differences):
        raise BuildError(f'SDK {kind} header mismatch: ' + '; '.join(
            label + '=' + ','.join(names) for label, names in differences if names))
    return {name: dict(source=str(source.relative_to(root)), sha256=digest(source))
            for name, source in sorted(expected.items())}


def export_private_headers(root, sdk):
    return export_headers(root, sdk / 'private', 'private', private_headers(root))


def export_public_headers(root, sdk):
    return export_headers(root, sdk / 'include', 'public', public_headers(root))


def export_headers(root, directory, kind, expected):
    # Validate the full source mapping before removing any generated files.
    if directory.is_symlink():
        raise BuildError(f'SDK {kind} export directory is a symlink: {directory}')
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    for name, source in expected.items():
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    return check_headers(root, directory, kind, expected)
