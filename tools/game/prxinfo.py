"""Lists the exports and imports (NIDs) of a decrypted PSP PRX: python -I tools/game/prxinfo.py <file.prx>"""
import struct,sys
def info(path):
    d=open(path,"rb").read()
    shoff=struct.unpack_from("<I",d,0x20)[0];shentsize,shnum,shstr=struct.unpack_from("<HHH",d,0x2e)
    ph=struct.unpack_from("<I",d,0x1c)[0];phn=struct.unpack_from("<H",d,0x2c)[0]
    # module info: p_paddr of first phdr (psp: offset of sceModuleInfo)
    p_type,p_off,p_va,p_pa,p_fs,p_ms,p_fl,p_al=struct.unpack_from("<IIIIIIII",d,ph)
    mi=p_pa&0x7fffffff
    attr,ver,name=struct.unpack_from("<HH28s",d,mi)
    gp,ent_top,ent_end,stub_top,stub_end=struct.unpack_from("<IIIII",d,mi+32)
    print(path,name.split(b"\0")[0],hex(ent_top),hex(ent_end),hex(stub_top),hex(stub_end),"phdr0",hex(p_va),hex(p_off))
    base=p_off-p_va
    exports={}
    o=ent_top
    while o<ent_end:
        namep,ver,attr,entlen,varc,funcc=struct.unpack_from("<IHHBBH",d,base+o)
        nids,ents=struct.unpack_from("<II",d,base+o+12)
        nm=d[base+namep:base+namep+40].split(b"\0")[0] if namep else b"(syscall/main)"
        fl=[(struct.unpack_from("<I",d,base+nids+4*i)[0],struct.unpack_from("<I",d,base+ents+4*i)[0]) for i in range(funcc)]
        exports[nm]=fl;o+=entlen*4
    imports={}
    o=stub_top
    while o<stub_end:
        namep,ver,attr,entlen,varc,funcc=struct.unpack_from("<IHHBBH",d,base+o)
        nids,stubs=struct.unpack_from("<II",d,base+o+12)
        nm=d[base+namep:base+namep+40].split(b"\0")[0]
        imports[nm]=[struct.unpack_from("<I",d,base+nids+4*i)[0] for i in range(funcc)];o+=entlen*4
    return exports,imports
if __name__=="__main__":
    for p in sys.argv[1:]:
        e,i=info(p)
        for k,v in e.items():print(" export",k,len(v),[hex(n) for n,_ in v][:6])
        for k,v in i.items():print(" import",k,len(v))
