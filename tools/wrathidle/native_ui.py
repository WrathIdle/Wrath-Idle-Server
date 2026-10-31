"""Build/install the native build-12340 interface. Original game resources stay local."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import time


class Storm:
    def __init__(self, path):
        self.dll = c.WinDLL(str(path), use_last_error=True)
        signatures = {
            'SFileOpenArchive': [c.c_wchar_p, c.c_uint32, c.c_uint32, c.POINTER(c.c_void_p)],
            'SFileCloseArchive': [c.c_void_p],
            'SFileOpenFileEx': [c.c_void_p, c.c_char_p, c.c_uint32, c.POINTER(c.c_void_p)],
            'SFileGetFileSize': [c.c_void_p, c.POINTER(c.c_uint32)],
            'SFileReadFile': [c.c_void_p, c.c_void_p, c.c_uint32, c.POINTER(c.c_uint32), c.c_void_p],
            'SFileCloseFile': [c.c_void_p],
            'SFileCreateArchive': [c.c_wchar_p, c.c_uint32, c.c_uint32, c.POINTER(c.c_void_p)],
            'SFileAddFileEx': [c.c_void_p, c.c_wchar_p, c.c_char_p, c.c_uint32, c.c_uint32, c.c_uint32],
        }
        for name, args in signatures.items():
            getattr(self.dll, name).argtypes = args
            getattr(self.dll, name).restype = c.c_uint32 if name == "SFileGetFileSize" else c.c_bool

    def open(self, path):
        handle = c.c_void_p()
        if not self.dll.SFileOpenArchive(str(path), 0, 0x100, c.byref(handle)):
            raise OSError(f'Cannot open archive: {path}')
        return handle

    def read(self, archive, name):
        handle = c.c_void_p()
        if not self.dll.SFileOpenFileEx(archive, name.encode('ascii'), 0, c.byref(handle)):
            return None
        try:
            high = c.c_uint32()
            size = self.dll.SFileGetFileSize(handle, c.byref(high)) & 0xffffffff
            if high.value or size > 16 * 1024 * 1024:
                raise ValueError(f'Unexpected interface resource size: {name}')
            buf = c.create_string_buffer(size)
            read = c.c_uint32()
            if not self.dll.SFileReadFile(handle, buf, size, c.byref(read), None) or read.value != size:
                raise OSError(f'Cannot read {name}')
            return buf.raw
        finally:
            self.dll.SFileCloseFile(handle)


def digest(data):
    return hashlib.sha256(data).hexdigest()


# Original instruction windows checked as a whole. No blind writes to arbitrary executables.
# Mechanism reference: suprsokr/allow-custom-gluexml-mithril (build 12340).
PATCHES = [
    (0x1f41bf, bytes.fromhex('74 39 56 68 38 d7 a1 00'), bytes.fromhex('eb 39 56 68 38 d7 a1 00')),
    (0x415a25, bytes.fromhex('75 05 5e 8b e5 5d c3 81'), bytes.fromhex('eb 05 5e 8b e5 5d c3 81')),
    (0x415a3f, bytes.fromhex('01 00 00 00 5e 8b e5 5d'), bytes.fromhex('03 00 00 00 5e 8b e5 5d')),
    (0x415a95, bytes.fromhex('01 00 00 00 5e 8b e5 5d'), bytes.fromhex('03 00 00 00 5e 8b e5 5d')),
    (0x415b46, bytes.fromhex('7f 12 83 c8 ff f7 d8 1b'), bytes.fromhex('eb 12 83 c8 ff f7 d8 1b')),
    (0x415b5f, bytes.fromhex('83 c0 03 5e 8b e5 5d c3'), bytes.fromhex('b8 03 00 00 00 eb ed c3')),
]


def patch_executable(data):
    output = bytearray(data)
    for offset, before, after in PATCHES:
        current = data[offset:offset + len(before)]
        if current not in (before, after):
            raise ValueError(f'Unknown executable instruction window at {offset:#x}')
        output[offset:offset + len(after)] = after
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--storm', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    storm = Storm(args.storm)
    data = args.client / 'Data'
    locales = [p for p in data.iterdir() if p.is_dir() and (p / 'locale-{}.MPQ'.format(p.name)).exists()]
    if len(locales) != 1:
        raise ValueError('Expected one installed locale')
    locale = locales[0]
    # A last-sorting managed locale patch takes precedence over the existing M patch.
    target = locale / f'patch-{locale.name}-W.MPQ'
    def priority(path):
        name = path.name.lower()
        if name.startswith('patch'):
            stem = path.stem.lower().replace('patch-' + locale.name.lower(), 'patch')
            suffix = stem.removeprefix('patch').lstrip('-')
            return (10, int(suffix) if suffix.isdigit() else (0 if not suffix else 100), suffix)
        return (0, {'common': 0, 'common-2': 1, 'locale-' + locale.name.lower(): 2,
                    'expansion': 3, 'expansion-locale-' + locale.name.lower(): 3,
                    'lichking': 4, 'lichking-locale-' + locale.name.lower(): 4}.get(path.stem.lower(), -1), name)
    archives = sorted(data.glob('*.MPQ'), key=priority)
    archives += sorted((p for p in locale.glob('*.MPQ') if p != target and not p.name.lower().startswith('backup')),
                       key=priority)
    handles = [(p, storm.open(p)) for p in archives]
    provenance = {}

    def effective(name):
        for path, handle in reversed(handles):
            content = storm.read(handle, name)
            if content is not None:
                provenance[name] = {'archive': str(path), 'sha256': digest(content)}
                return content
        raise FileNotFoundError(name)

    try:
        toc_name = 'Interface\\FrameXML\\FrameXML.toc'
        toc = effective(toc_name)
        resources = {toc_name: toc.rstrip() + b'\r\nWrathIdle\\Protocol.lua\r\nWrathIdle\\Interface.lua\r\n'}
        source = Path(__file__).resolve().parents[2] / 'modules/mod-wrathidle/native/Interface/FrameXML/WrathIdle'
        for path in source.glob('*.lua'):
            resources[f'Interface\\FrameXML\\WrathIdle\\{path.name}'] = path.read_bytes()
        if len(resources) < 3:
            raise ValueError('Native interface sources are missing')
        executable = (args.client / 'Wow.exe').read_bytes()
        patched = patch_executable(executable)
        args.output.mkdir(parents=True, exist_ok=True)
        output = args.output / target.name
        staged = args.output / 'native-ui.building.mpq'
        if staged.exists():
            staged.unlink()
        handle = c.c_void_p()
        if not storm.dll.SFileCreateArchive(str(staged), 0, 64, c.byref(handle)):
            raise OSError('Cannot create native UI archive')
        try:
            with tempfile.TemporaryDirectory() as scratch:
                for index, (name, content) in enumerate(resources.items()):
                    path = Path(scratch) / str(index)
                    path.write_bytes(content)
                    if not storm.dll.SFileAddFileEx(handle, str(path), name.encode('ascii'), 0x80000200, 2, 2):
                        raise OSError(f'Cannot package {name}')
        finally:
            storm.dll.SFileCloseArchive(handle)
        handle = storm.open(staged)
        try:
            for name, content in resources.items():
                if storm.read(handle, name) != content:
                    raise ValueError(f'Archive verification failed: {name}')
        finally:
            storm.dll.SFileCloseArchive(handle)
        staged.replace(output)
        (args.output / 'Wow.exe').write_bytes(patched)
        manifest = {'archive': str(target), 'archive_sha256': digest(output.read_bytes()),
                    'executable_before': digest(executable), 'executable_after': digest(patched),
                    'resources': {name: digest(content) for name, content in resources.items()},
                    'original_resources': provenance, 'installed': False}
        if args.install:
            backup = args.output / ('backup-' + time.strftime('%Y%m%d-%H%M%S'))
            backup.mkdir()
            shutil.copy2(args.client / 'Wow.exe', backup / 'Wow.exe')
            if target.exists():
                shutil.copy2(target, backup / target.name)
            # Check exclusive access before changing either live file.
            kernel = c.WinDLL('kernel32', use_last_error=True)
            kernel.CreateFileW.argtypes = [c.c_wchar_p, c.c_uint32, c.c_uint32, c.c_void_p,
                                          c.c_uint32, c.c_uint32, c.c_void_p]
            kernel.CreateFileW.restype = c.c_void_p
            kernel.CloseHandle.argtypes = [c.c_void_p]
            probes = []
            try:
                for path in [args.client / 'Wow.exe'] + ([target] if target.exists() else []):
                    probe = kernel.CreateFileW(str(path), 0x80000000, 0, None, 3, 0, None)
                    if probe == c.c_void_p(-1).value:
                        raise OSError(f'File in use; staged output is ready: {path}')
                    probes.append(probe)
            finally:
                for probe in probes:
                    kernel.CloseHandle(probe)
            try:
                shutil.copy2(output, target)
                (args.client / 'Wow.exe').write_bytes(patched)
                if digest(target.read_bytes()) != manifest['archive_sha256']:
                    raise ValueError('Installed archive hash mismatch')
                if digest((args.client / 'Wow.exe').read_bytes()) != manifest['executable_after']:
                    raise ValueError('Installed executable hash mismatch')
                addon = args.client / 'Interface/AddOns/WrathIdle'
                if addon.exists():
                    shutil.move(str(addon), str(backup / 'WrathIdle-addon'))
            except Exception:
                shutil.copy2(backup / 'Wow.exe', args.client / 'Wow.exe')
                if (backup / target.name).exists():
                    shutil.copy2(backup / target.name, target)
                elif target.exists():
                    target.unlink()
                raise
            manifest.update(installed=True, backup=str(backup))
        (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(json.dumps({'archive': str(output), 'installed': manifest['installed']}))
    finally:
        for _, handle in handles:
            storm.dll.SFileCloseArchive(handle)


if __name__ == '__main__':
    main()
