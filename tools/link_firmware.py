"""Build exact-image TC32 integration artifacts. OFFLINE, never opens a device.

The link report deliberately does not authorize flashing. A complete image
requires separate boot checksum/extension/stack/ownership/recovery evidence.
"""
import json
import os
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
from image import backup, sha, STOCK_SHA

ROOT=Path(__file__).resolve().parents[1]

def main():
    os.chdir(ROOT)
    original=(ROOT/'private/controller-original.bin').read_bytes();backup(original)
    tc=ROOT/'private/tc32/bin';out=ROOT/'build/target';out.mkdir(parents=True,exist_ok=True)
    def run(name,args,output=None):
        result=subprocess.run([str(tc/f'tc32-elf-{name}.exe'),*map(str,args)],
                              text=True,capture_output=True,check=True)
        if result.stderr: print(result.stderr)
        if output: Path(output).write_text(result.stdout,encoding='utf-8')
        return result.stdout
    flags=['-Os','-g','-std=c99','-ffreestanding','-fno-builtin','-fno-common',
           '-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror',
           '-DCBC=0','-DCTR=0','-DECB=1','-Ifirmware/include','-Ifirmware/vendor/tiny-aes']
    sources=sorted(Path('firmware/src').glob('*.c'))+sorted(Path('firmware/target').glob('*.c'))
    sources += [Path('firmware/vendor/tiny-aes/aes.c'),Path('firmware/target/hooks_301b.S')]
    inputs=sources+list(Path('firmware/include').glob('*.h'))+list(Path('firmware/target').glob('*.h'))
    inputs += [Path('firmware/vendor/tiny-aes/aes.h'),Path('firmware/target/link_301b.ld'),Path(__file__).relative_to(ROOT)]
    source_hashes={p.as_posix():sha(p.read_bytes()) for p in sorted(set(inputs))}
    objects=[]
    for source in sources:
        obj=out/(source.stem+'.o')
        run('gcc',[*flags,'-c',source,'-o',obj]);objects.append(obj)
    elf=out/'switch22c-integration.elf'
    run('ld',['--gc-sections','-T','firmware/target/link_301b.ld',
              '-Map',out/'switch22c-integration.map',*objects,'-o',elf])
    undefined=run('nm',['-u',elf]).strip()
    if undefined: raise ValueError('Unresolved symbols: '+undefined)
    run('objdump',['-d',elf],out/'integration.disasm.txt')
    run('nm',['-n','-S',elf],out/'symbols.txt')
    run('size',['-A',elf],out/'size.txt')
    expected={'.hook.init':(0xa4bc,4),'.hook.loop':(0xa4a4,4),'.hook.after_loop':(0xa4a8,2),
              '.hook.pair':(0xb8bc,8),'.hook.connect_packet':(0x119f4,4),
              '.hook.channel':(0x1ce8,6),'.hook.phy_instant':(0x121e6,4),
              '.hook.phy_dispatch':(0x1274e,4)}
    edits=[];seen=set();sections=[]
    with elf.open('rb') as f:
        ef=ELFFile(f)
        for s in ef.iter_sections():
            if not (s['sh_flags']&2) or not s['sh_size']: continue
            a=s['sh_addr'];n=s['sh_size'];sections.append(dict(name=s.name,address=hex(a),size=n))
            if s['sh_type']=='SHT_NOBITS':
                if not s.name.startswith('.overlay.'): raise ValueError('Unexpected BSS')
                continue
            if s.name in expected:
                if (a,n)!=expected[s.name]: raise ValueError('Hook geometry changed: '+s.name)
                seen.add(s.name)
                physical=a+0x6000 if s.name=='.hook.channel' else a
            elif s.name=='.extension':
                if a!=0x19000 or a+n>0x24000 or original[a:a+n]!=b'\xff'*n:
                    raise ValueError('Extension is not inside exact erased reservation')
                physical=a
            else: raise ValueError('Unexpected allocated section '+s.name)
            edits.append(dict(section=s.name,address=hex(physical),before=original[physical:physical+n].hex(),after=s.data().hex()))
    if seen!=set(expected): raise ValueError('Missing hook')
    if any(sha(Path(p).read_bytes())!=digest for p,digest in source_hashes.items()):
        raise ValueError('Source changed while compiling')
    # Keep interval's original upper limit while lowering its minimum to 5 ms.
    for a,before,after in [(0x11938,'8aef','0aef'),(0x11a00,'7a0c0000','7c0c0000')]:
        if original[a:a+len(bytes.fromhex(before))]!=bytes.fromhex(before): raise ValueError('Interval preimage mismatch')
        edits.append(dict(section='interval',address=hex(a),before=before,after=after))
    # Both the stock branch following the motor clear and the new branch
    # immediately after the BLE poll must reach the same mode-selection code.
    if original[0xa4a8:0xa4b4]!=bytes.fromhex('00a000a100a20590db9af087'):
        raise ValueError('BLE motor-clear preimage mismatch')
    after_loop=next(e for e in edits if e['section']=='.hook.after_loop')
    if after_loop['after']!='f587':
        raise ValueError('BLE poll must branch directly from 0xa4a8 to 0xa496')
    report=dict(format='switch22c-integration-link-v1',stock_sha256=STOCK_SHA,
        elf_sha256=sha(elf.read_bytes()),source_sha256=source_hashes,installable=False,
        pending=['boot extension/checksum proof','SRAM writer and stack audit',
                 'hardware completion and recovery writer validation'],sections=sections,edits=edits)
    (out/'integration-link.json').write_text(json.dumps(report,indent=2)+'\n')
    print((out/'size.txt').read_text())
    print('Linked TC32 integration artifact; NOT an installable image. No device accessed.')

if __name__=='__main__':
    try: main()
    except subprocess.CalledProcessError as e:
        print(e.stdout,e.stderr);raise SystemExit(e.returncode)
