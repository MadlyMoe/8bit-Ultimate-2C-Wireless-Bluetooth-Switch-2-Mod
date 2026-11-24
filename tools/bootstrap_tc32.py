"""Download a pinned TC32 Windows toolchain into private/; never touches a device.

The bootstrapper extracts only the files listed in the pinned manifest.
"""
import hashlib
import io
import json
import urllib.request
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def main():
    spec=json.loads((ROOT/'tools/tc32_files.json').read_text())
    dest=ROOT/'private'/'tc32'
    if dest.exists():
        for f in spec['files']:
            p=dest/Path(f['path']).relative_to('toolchain/win32')
            if not p.is_file(): raise SystemExit(f'Incomplete toolchain: {p}; keep it for diagnosis')
            b=p.read_bytes()
            digest=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
            if len(b)!=f['size'] or digest!=f['git_blob_sha1']:
                raise SystemExit(f'Existing toolchain hash mismatch: {p}')
        print('Pinned TC32 toolchain already verified; no changes.')
        return
    url=f'https://codeload.github.com/{spec["repository"]}/zip/{spec["commit"]}'
    print(f'Downloading compiler tools at pinned commit {spec["commit"]}...')
    with urllib.request.urlopen(url,timeout=60) as response:
        payload=response.read(100*1024*1024+1)
    if len(payload)>100*1024*1024: raise SystemExit('Archive exceeds size budget')
    archive=zipfile.ZipFile(io.BytesIO(payload))
    prefix=spec['repository'].split('/')[1]+'-'+spec['commit']+'/'
    checked=[]
    for f in spec['files']:
        info=archive.getinfo(prefix+f['path'])
        if info.file_size != f['size']: raise SystemExit('Unexpected archive member size')
        b=archive.read(info)
        digest=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
        if digest!=f['git_blob_sha1']: raise SystemExit('Git blob integrity failure: '+f['path'])
        rel=Path(f['path']).relative_to('toolchain/win32')
        target=(dest/rel).resolve()
        if not target.is_relative_to(dest.resolve()): raise SystemExit('Unsafe archive path')
        checked.append((target,b))
    for target,b in checked:
        target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream: stream.write(b)
    print(f'PASS: {len(checked)} pinned toolchain files verified in {dest}. No device accessed.')
if __name__=='__main__': main()
