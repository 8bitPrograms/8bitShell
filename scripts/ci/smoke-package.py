#!/usr/bin/env python3
"""Extract a local CI-built portable package and start it without the Qt SDK."""
import argparse
import ctypes
import os
import re
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='bitshell-package-') as temporary:
        root = Path(temporary)
        package = root / 'package'
        package.mkdir()
        if zipfile.is_zipfile(args.archive):
            with zipfile.ZipFile(args.archive) as archive:
                for name in archive.namelist():
                    path = PurePosixPath(name.replace('\\', '/'))
                    if path.is_absolute() or '..' in path.parts or ':' in name:
                        raise ValueError('Unsafe package entry')
                archive.extractall(package)
        else:
            with tarfile.open(args.archive) as archive:
                archive.extractall(package, filter='data')
        binaries = list(package.rglob('bitshell.exe' if os.name == 'nt' else 'bitshell'))
        binaries = [path for path in binaries if path.is_file() and path.parent.name == 'bin']
        if len(binaries) != 1:
            raise ValueError('Expected one packaged application')
        executable = binaries[0]
        if os.name != 'nt':
            dynamic = subprocess.run(['readelf', '-d', executable], check=True, capture_output=True, text=True).stdout
            entries = re.findall(r'\((?:RUNPATH|RPATH)\).*?\[(.*?)\]', dynamic)
            if entries != ['$ORIGIN/../lib']:
                raise ValueError(f'Unexpected portable loader path: {entries}')
        env = os.environ.copy()
        for key in ('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'LD_LIBRARY_PATH'):
            env.pop(key, None)
        env['PATH'] = os.pathsep.join(part for part in env.get('PATH', '').split(os.pathsep)
                                      if 'qt-sdk' not in part.lower() and '/qt/' not in part.lower().replace('\\', '/'))
        env['XDG_CONFIG_HOME'] = str(root / 'config')
        env['XDG_DATA_HOME'] = str(root / 'data')
        profile = root / 'config/bitshell'
        profile.mkdir(parents=True)
        (profile / 'settings.yaml').write_text('version: 1\nkind: bitshell/settings\nlogging:\n  enabledByDefault: false\n', encoding='utf-8')
        env['QT_QPA_PLATFORM'] = 'offscreen'
        env['QT_QUICK_BACKEND'] = 'software'
        if os.name != 'nt':
            dependencies = subprocess.run(['ldd', executable], env=env, check=True, capture_output=True, text=True).stdout
            qt_paths = re.findall(r'libQt6\S+ => (\S+)', dependencies)
            if not qt_paths or 'not found' in dependencies or any(not Path(path).resolve().is_relative_to(package.resolve()) for path in qt_paths):
                raise ValueError(f'Qt dependencies escaped the portable directory: {dependencies}')
        result = subprocess.run([executable, '--version'], env=env, cwd=root,
                                capture_output=True, text=True, timeout=20)
        if os.name == 'nt' and ctypes.windll.shell32.IsUserAnAdmin():
            # GitHub's Windows hosts are elevated with UAC disabled. The product
            # must STILL refuse them. A normal-user VM smoke is a release gate,
            # not replaced by this negative privilege test.
            if result.returncode == 0 or 'normal desktop user' not in result.stderr:
                raise ValueError('Packaged app did not reject the elevated runner as expected')
            print('PASS: elevated launch refused. Normal-user Windows UI acceptance remains required.')
            return
        if result.returncode or '8bitShell ' not in result.stdout:
            raise ValueError(f'Packaged startup failed: {result.stdout} {result.stderr}')
        screenshot = root / 'smoke.png'
        command = [executable, '--screenshot', screenshot, '--panel', 'hidden', '--window-size', '1280x800']
        # No user shell profiles, credentials, aliases or login hooks in smoke tests.
        if os.name != 'nt':
            command += ['--', '/bin/sh']
        else:
            command += ['--', str(Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'), '-NoLogo', '-NoProfile']
        result = subprocess.run(command,
                                env=env, cwd=root, capture_output=True, text=True, timeout=25)
        if result.returncode or not screenshot.is_file() or screenshot.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n':
            raise ValueError(f'Packaged QML rendering failed: {result.stdout} {result.stderr}')
        print('PASS: packaged startup and offscreen QML rendering without SDK search paths')


if __name__ == '__main__':
    main()
