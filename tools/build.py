"""Reproducible native checks; no device access and no firmware flashing."""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('CC'))
    args=parser.parse_args()
    os.chdir(ROOT)
    cc=args.cc or shutil.which('clang') or shutil.which('gcc')
    if not cc: raise SystemExit('Set CC to a native C99 compiler, or pass --cc')
    build=ROOT/'build'; build.mkdir(exist_ok=True)
    exe=build/('test_protocol.exe' if os.name=='nt' else 'test_protocol')
    flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-Wconversion',
           '-DCBC=0','-DCTR=0','-DECB=1','-Ifirmware/include','-Ifirmware/vendor/tiny-aes']
    # Preserve the vetted third-party AES source verbatim. Apply our extra
    # conversion warnings only to project code, not upstream integer idioms.
    aesobj=build/'aes.o'
    subprocess.run([cc,*[f for f in flags if f!='-Wconversion'],'-c',
                    'firmware/vendor/tiny-aes/aes.c','-o',str(aesobj)],check=True)
    cmd=[cc,*flags,'firmware/src/s2_protocol.c','firmware/src/s2_transport.c',
         'firmware/src/s2_rumble.c',str(aesobj),
         'tests/test_protocol.c','-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True)
    bondexe=build/('test_bond_store.exe' if os.name=='nt' else 'test_bond_store')
    subprocess.run([cc,*flags,'firmware/src/s2_bond_store.c','tests/test_bond_store.c','-o',str(bondexe)],check=True)
    subprocess.run([str(bondexe)],check=True)
    runtime=build/('test_runtime.exe' if os.name=='nt' else 'test_runtime')
    subprocess.run([cc,*flags,'firmware/src/s2_protocol.c','firmware/src/s2_transport.c',
                    'firmware/src/s2_input_301b.c','firmware/src/s2_rumble.c','firmware/src/s2_csa2.c',
                    str(aesobj),'tests/test_runtime.c','-o',str(runtime)],check=True)
    subprocess.run([str(runtime)],check=True)
    att=build/('test_att.exe' if os.name=='nt' else 'test_att')
    subprocess.run([cc,*flags,'firmware/src/s2_att.c','firmware/src/s2_l2cap.c',
                    'firmware/src/s2_virtual_flash.c','firmware/target/pro2_gatt.c',
                    'tests/test_att.c','-o',str(att)],check=True)
    subprocess.run([str(att)],check=True)
    target=build/('test_target.exe' if os.name=='nt' else 'test_target')
    target_flags=[f for f in flags if f!='-Wconversion']
    subprocess.run([cc,*target_flags,'-DS2_HOST_TARGET_TEST','-Ifirmware/target',
                    *map(str,sorted(Path('firmware/src').glob('*.c'))),
                    'firmware/target/runtime_301b.c','firmware/target/radio_301b.c',
                    'firmware/target/flash_301b.c',
                    'firmware/target/pro2_gatt.c',str(aesobj),
                    'tests/test_target.c','-o',str(target)],check=True)
    subprocess.run([str(target)],check=True)
    math=build/('test_libc.exe' if os.name=='nt' else 'test_libc')
    subprocess.run([cc,*flags,'-fno-builtin','firmware/target/libc_min.c',
                    'tests/test_libc.c','-o',str(math)],check=True)
    subprocess.run([str(math)],check=True)
    print('All offline checks passed. This command does not build or flash a firmware image.')
if __name__=='__main__': main()
