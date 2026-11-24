"""Recheck exact-image evidence and reviewed stack chains. Offline only.

This is a static integration audit, not a proof of all proprietary control flow
or a substitute for measuring hardware high-water marks and console behavior.
"""
import json,re,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from image import backup,sha

ROOT=Path(__file__).resolve().parents[1]

def audit():
    original=(ROOT/'private/controller-original.bin').read_bytes();backup(original)
    elf=ROOT/'build/target/switch22c-integration.elf'
    frames={}
    with elf.open('rb') as f:
        e=ELFFile(f)
        symbols={s['st_value']&~1:s.name for s in e.get_section_by_name('.symtab').iter_symbols()
                 if s['st_info']['type']=='STT_FUNC' and s['st_value']}
        for entry in e.get_dwarf_info().CFI_entries():
            if 'initial_location' in entry.header and entry['initial_location'] in symbols:
                frames[symbols[entry['initial_location']]]=max(x['cfa'].offset for x in entry.get_decoded().table)
    # Stock frames include saves and locals, from exact disassembly, not SDK estimates.
    stock={'main_a374':16,'stack_11220':20,'rx_125c0':56,'l2cap_1484c':340,
           'll_122fc':24,'encrypt_16ccc':32,'ccm_16894':108,'flash_fe5c':24,
           'flash_10f5c':52,'status_mask_102f8':12,'status_read_102dc':8,
           'status_read_feb0':20,'status_write_fed8':12,
           'flash_fe40':12,'flash_f64':28,'flash_eb4':28,'flash_wait_e70':16,
           'instant_121c8':20,'decrypt_16d78':40,
           'flash_command_e44':12,'irq_entry':60,'irq_wrapper_220':4,
           'radio_stock_allowance':128}
    frames.update(stock)
    rx=['main_a374','s2_301b_loop','stack_11220','rx_125c0','receive','s2_att_receive',
        'att_write','s2_transport_command','s2_command']
    chains={
      'pairing_AES':rx+['s2_confirm','AES_ECB_encrypt','AddRoundKey'],
      'bond_program':rx+['commit_bond','s2_bond_save','flash_program','flash_10f5c',
                        'flash_f64','flash_wait_e70','flash_command_e44'],
      'bond_verify':rx+['commit_bond','s2_bond_save','matches','flash_read',
                       'flash_fe40','flash_eb4','flash_command_e44'],
      'bond_protection_restore':rx+['commit_bond','s2_bond_save','flash_program',
                       's2_301b_flash_open','s2_301b_flash_close','flash_protection',
                       'status_mask_102f8','status_write_fed8','flash_f64','flash_wait_e70','flash_command_e44'],
      'bond_protection_read':rx+['commit_bond','s2_bond_save','flash_program',
                       's2_301b_flash_open','s2_301b_flash_close','flash_protection',
                       'status_mask_102f8','status_read_102dc','status_read_feb0','flash_eb4','flash_command_e44'],
      # Both startup and deferred radio restarts flush only in the idle context.
      'diagnostic_program':['main_a374','s2_301b_loop','advertise','trace_flush','s2_bond_trace_append',
                        'flash_program','flash_10f5c','flash_f64','flash_wait_e70','flash_command_e44'],
      'diagnostic_verify':['main_a374','s2_301b_loop','advertise','trace_flush','s2_bond_trace_append',
                        'matches','flash_read','flash_fe40','flash_eb4','flash_command_e44'],
      'encrypted_notify':['main_a374','s2_301b_loop','s2_transport_poll','notify','enqueue',
                          'l2cap_1484c','ll_122fc','encrypt_16ccc','ccm_16894'],
      'encrypted_ATT_reply':['main_a374','s2_301b_loop','s2_att_poll','att_send','enqueue',
                            'l2cap_1484c','ll_122fc','encrypt_16ccc','ccm_16894'],
      'TX_pending_encrypted_control':['main_a374','s2_301b_loop','s2_transport_poll','notify','enqueue',
                          'l2cap_1484c','ll_122fc','instant_121c8','decrypt_16d78','ccm_16894'],
      'IRQ_CSA2':['irq_entry','irq_wrapper_220','radio_stock_allowance','s2_301b_channel',
                  's2_csa2_channel','__umodsi3','divide']}
    totals={}
    for name,chain in chains.items():
        amount=sum(frames[f] for f in chain)
        limit=384 if name.startswith('IRQ') else 784
        if amount>limit-16:raise ValueError(f'{name}: insufficient reviewed stack margin {amount}/{limit}')
        totals[name]={'bytes':amount,'available':limit,'margin':limit-amount,'chain':chain}
    # Look for every direct literal reference into the four replacement regions.
    # Remaining reference-bearing stock functions are bypassed ATT/SMP routines.
    regions=[(0x84313c,0x843178),(0x8436c0,0x8436e0),(0x843bd4,0x843bf4),(0x843c04,0x843cd8)]
    references=[]
    for line in (ROOT/'build/tc32/stock-app.disasm.txt').read_text().splitlines():
        m=re.match(r'\s*([0-9a-f]+):.*tloadr.*; \(0x([0-9a-f]+)\)',line)
        if not m:continue
        pc,literal=map(lambda s:int(s,16),m.groups());value=struct.unpack_from('<I',original,literal)[0]
        if any(a<=value<b for a,b in regions):
            if not (pc==0xa178 or 0x138c0<=pc<0x14524 or 0x146ac<=pc<0x14780 or 0x14c00<=pc<0x16370):
                raise ValueError(f'Unexpected overlay reference {pc:x} -> {value:x}')
            references.append({'pc':hex(pc),'literal':hex(value)})
    assert original[0x19000:0x3f000]==b'\xff'*(0x3f000-0x19000)
    assert struct.unpack_from('<H',original,0x600c)[0]==0x250
    assert struct.unpack_from('<I',original,0x6018)[0]==0x12274
    assert original[0x3f018:0x3f01c]==bytes(4) and original[0x3f020:0x3f024]==bytes(4)
    result={'elf_sha256':sha(elf.read_bytes()),'frames':frames,'reviewed_chains':totals,
      'overlay_literal_references':references,
      'boot_evidence':{'normal_entry':'3304 -> 35f0 (update record), 3308 -> 3674 (pending copy), 330c -> 302c',
        'pending_copy_gate':'3674 requires 3f018 == aeb14d6c and valid nonzero 3f020 length; both are zero',
        'application_gate':'302c checks 6002, KNLT at 6008 and RAM length at 600c; 3000 copies unchanged 2500 bytes',
        'extension':'Normal boot does not checksum the application or restrict cached code to old bin_size',
        'protected':'Bootloader, bank B, update metadata, factory, calibration and MAC are unchanged'},
      'limitations':['Static reviewed call chains; hardware stack high-water marks unmeasured',
                    'Literal-reference audit is not a formal proof of all indirect stock memory accesses',
                    'Radio timing, physical inputs, console acceptance and restore on this unit remain unverified']}
    (ROOT/'build/target/integration-audit.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Reviewed stack chains:',{k:v['bytes'] for k,v in totals.items()})
    return result

if __name__=='__main__':audit()
