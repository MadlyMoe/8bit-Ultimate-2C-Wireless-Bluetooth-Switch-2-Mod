"""Offline TC32 disassembly and relocatable-object build. NO device access."""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from image import backup, APP_BASE, APP_END

ROOT=Path(__file__).resolve().parents[1]

def disassemble_blob(executable, path, base, size):
    """Keep undecodable halfwords explicit; old binutils aborts on literal pools."""
    cursor, output, gaps = base, [], []
    data = path.read_bytes()
    while cursor < base + size:
        command = [str(executable), '-D', '-b', 'binary', '-m', 'tc32',
                   '-M', 'force-thumb', f'--adjust-vma={base}',
                   f'--start-address={cursor}', str(path)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            output.append(result.stdout)
            break
        # The vendor decoder aborts before printing bytes for invalid long-branch
        # pairs. Do not turn other tool failures into a successful analysis.
        match = re.search(r'\n\s*([0-9a-f]+):\s*\Z', result.stdout)
        if result.returncode != 3 or result.stderr.strip() or not match:
            raise subprocess.CalledProcessError(result.returncode, command, result.stdout, result.stderr)
        address = int(match[1], 16)
        offset = address - base
        if address < cursor or address % 2 or offset + 4 > size:
            raise RuntimeError('Disassembler did not make bounded progress')
        first = int.from_bytes(data[offset:offset+2], 'little')
        second = int.from_bytes(data[offset+2:offset+4], 'little')
        if first & 0xf800 != 0x9000 or second & 0xf800 == 0x9800:
            raise RuntimeError(f'Unexpected decoder failure at 0x{address:x}')
        output.append(result.stdout[:match.start()] +
                      f'\n    {address:x}:\t{first:04x}\t.hword 0x{first:04x} ; UNDECODED invalid branch pair\n')
        gaps.append(address)
        cursor = address + 2
    return ''.join(output), gaps

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-only',action='store_true',help='Reuse existing stock analysis; still verify stock hashes')
    args=parser.parse_args()
    os.chdir(ROOT)
    tc=ROOT/'private/tc32/bin'
    build=ROOT/'build/tc32';build.mkdir(parents=True,exist_ok=True)
    original=(ROOT/'private/controller-original.bin').read_bytes();backup(original)
    stock=original[APP_BASE:APP_END]
    def run(tool,args,out=None):
        command=[str(tc/f'tc32-elf-{tool}.exe'),*map(str,args)]
        if out:
            with Path(out).open('w',encoding='utf-8') as f:
                subprocess.run(command,stdout=f,stderr=subprocess.PIPE,check=True)
        else:subprocess.run(command,check=True)
    for name,data,base in [('stock-app',stock,0x6000),('bootloader',original[:0x6000],0)]:
        if args.compile_only: continue
        path=build/f'{name}.bin';path.write_bytes(data)
        listing=build/f'{name}.disasm.txt'
        # Raw blobs lack ELF mapping symbols. TC32's 16-bit instruction mode
        # must be forced; the default 32-bit mode prints plausible nonsense.
        text, gaps = disassemble_blob(tc/'tc32-elf-objdump.exe', path, base, len(data))
        if name=='stock-app' and not re.search(r'6000:.*tj.*60b4',text):
            raise SystemExit('Disassembly sanity check failed: stock entry must branch to cstartup 0x60b4')
        temporary=listing.with_suffix('.tmp')
        temporary.write_text(text,encoding='utf-8');temporary.replace(listing)
        print(f'{name}: {len(data)} bytes covered; {len(gaps)} undecodable halfwords explicitly marked (raw listing is not a control-flow map).')
    flags=['-std=c99','-Os','-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections',
           '-Wall','-Wextra','-DCBC=0','-DCTR=0','-DECB=1','-Ifirmware/include','-Ifirmware/vendor/tiny-aes']
    objects=[]
    for name in ['firmware/src/s2_protocol.c','firmware/src/s2_transport.c',
                 'firmware/src/s2_rumble.c','firmware/src/s2_bond_store.c','firmware/src/s2_input_301b.c',
                 'firmware/src/s2_csa2.c','firmware/src/s2_att.c','firmware/src/s2_l2cap.c',
                 'firmware/src/s2_virtual_flash.c',
                 'firmware/target/stock_301b.c','firmware/target/pro2_gatt.c',
                 'firmware/vendor/tiny-aes/aes.c']:
        obj=build/(Path(name).stem+'.o')
        run('gcc',flags+['-c',name,'-o',obj]);objects.append(obj)
    # Relocatable object only: no memory reservation or stock hooks are asserted.
    run('ld',['-r',*objects,'-o',build/'s2-core.o'])
    run('size',[build/'s2-core.o'],build/'size.txt')
    run('nm',['-u',build/'s2-core.o'],build/'undefined.txt')
    run('objdump',['-dr',build/'s2-core.o'],build/'s2-core.disasm.txt')
    print((build/'size.txt').read_text())
    print('PASS: TC32 relocatable core in build/tc32. No device accessed; not a flash image.')
if __name__=='__main__':
    try: main()
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors='replace') if isinstance(e.stderr,bytes) else e,file=sys.stderr)
        raise SystemExit(e.returncode)
