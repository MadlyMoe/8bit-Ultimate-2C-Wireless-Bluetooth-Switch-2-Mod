"""Package the full experimental integration from a pinned private backup.

Never opens a device. Run build.py and link_firmware.py first. No proprietary
base image is embedded in the patch manifest or flashing utility.
"""
import json,struct
from pathlib import Path
from image import backup,sha,BOOT_SHA,STOCK_SHA
from audit_integration import audit
ROOT=Path(__file__).resolve().parents[1]

def main():
    evidence=audit()
    linked=json.loads((ROOT/'build/target/integration-link.json').read_text())
    if linked['elf_sha256']!=evidence['elf_sha256']:raise ValueError('Stale link manifest')
    if not linked.get('source_sha256') or any(sha((ROOT/p).read_bytes())!=digest
                                            for p,digest in linked['source_sha256'].items()):
        raise ValueError('Source differs from linked build; rerun link_firmware.py')
    raw=(ROOT/'private/controller-original.bin').read_bytes();backup(raw)
    target=bytearray(raw);edits=linked['edits'][:];used=set()
    code_end=max(int(x['address'],16)+len(bytes.fromhex(x['after'])) for x in edits)
    # Release 62d50b64 already occupied the sector ending at 0x1e000.
    # Include it even if new code shrinks so upgrade and restore clear the tail.
    end=max(0x1e000,(code_end+0xfff)&~0xfff)
    if not 0x19000<end<=0x24000:raise ValueError('Unexpected extension end')
    edits.append({'section':'bin_size','address':'0x6018','before':raw[0x6018:0x601c].hex(),
                  'after':struct.pack('<I',code_end-0x6000).hex()})
    for edit in edits:
        a=int(edit['address'],16);old=bytes.fromhex(edit['before']);new=bytes.fromhex(edit['after'])
        if len(old)!=len(new) or a<0x6000 or a+len(new)>end or raw[a:a+len(old)]!=old:
            raise ValueError('Invalid patch preimage')
        span=set(range(a,a+len(new)))
        if span&used:raise ValueError('Overlapping edits')
        used|=span;target[a:a+len(new)]=new
    assert target[:0x6000]==raw[:0x6000] and target[end:]==raw[end:]
    assert target[0x6008:0x6018]==raw[0x6008:0x6018]
    payload=bytes(target[0x6000:end]);out=ROOT/'build/release';out.mkdir(exist_ok=True)
    (out/'switch22c-301b-v109-full.bin').write_bytes(payload)
    sources={str(p.relative_to(ROOT)).replace('\\','/'):sha(p.read_bytes())
             for folder in ['firmware/include','firmware/src','firmware/target','firmware/vendor/tiny-aes']
             for p in sorted((ROOT/folder).glob('*')) if p.is_file()}
    report={'format':'switch22c-full-301b-v1','status':'experimental-full-integration',
      'base':0x6000,'end':end,'stock_end':0x18400,'bond_base':0x3d000,'bond_end':0x3f000,
      'boot_sha256':BOOT_SHA,'stock_sha256':STOCK_SHA,'target_sha256':sha(payload),
      'elf_sha256':linked['elf_sha256'],'audit_sha256':sha((ROOT/'build/target/integration-audit.json').read_bytes()),
      'hardware_verified':False,'edits':edits,'sources':sources}
    (ROOT/'firmware/full-301b-v1.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Full experimental candidate: {len(payload)} bytes, SHA256 {sha(payload)}')
    print('No device accessed. Hardware behavior is unverified.')

if __name__=='__main__':main()
