"""winmd.py - minimal WinRT metadata (.winmd) reader built on dnfile.

Builds a model of every type in a set of .winmd files:
  Type.kind   : 'interface' | 'class' | 'struct' | 'enum' | 'delegate' | 'attribute' | 'apicontract'
  Type.iid    : GUID string (interfaces, delegates, generic definitions)
  Type.methods: [Method]  (vtable order)
  Type types  : tuples  ('prim','int32') ('str',) ('obj',) ('guid',) ('type', fullname) ('inst', fullname, [args]) ('var', n)
                ('arr', elem) ('byref', t)
"""
import dnfile, struct, glob, os, sys
from dnfile.mdtable import TypeDefRow

PRIM = {0x02:'bool',0x03:'char16',0x04:'int8',0x05:'uint8',0x06:'int16',0x07:'uint16',0x08:'int32',0x09:'uint32',
        0x0A:'int64',0x0B:'uint64',0x0C:'float',0x0D:'double',0x18:'intptr',0x19:'uintptr'}

class Method:
    __slots__=('name','ret','params','flags','special')
class Param:
    __slots__=('name','type','is_in','is_out')
class Type:
    def __init__(s):
        s.ns=s.name=''; s.kind=''; s.iid=None; s.methods=[]; s.fields=[]; s.base=None; s.interfaces=[]
        s.default_iface=None; s.attrs=[]; s.generic_n=0; s.enum_base='int32'; s.exclusive_to=None
        s.enum_values=[]; s.statics=[]; s.factories=[]; s.composable=[]; s.activatable=[]; s.contract=None; s.pe=None
    @property
    def full(s): return s.ns+'.'+s.name

def hv(x):
    """dnfile heap item -> python value"""
    if x is None: return None
    if hasattr(x,'value'): return x.value
    return x

class Reg:
    def __init__(s): s.types={}; s.pes=[]
    def load_dir(s,d):
        for f in sorted(glob.glob(os.path.join(d,'*.winmd'))): s.load(f)
    def load(s,path):
        pe=dnfile.dnPE(path); s.pes.append(pe)
        md=pe.net.mdtables
        tds=md.TypeDef.rows; mdr=md.MethodDef.rows if md.MethodDef else []; prm=md.Param.rows if md.Param else []; fld=md.Field.rows if md.Field else []
        n=len(tds)
        # attribute index by (table,row)
        attrs={}
        for c in md.CustomAttribute.rows:
            p=c.Parent
            if p is None or p.table is None: continue
            attrs.setdefault((p.table.name,p.row_index),[]).append(c)
        ifimpl={}
        for k,r in enumerate(md.InterfaceImpl.rows if md.InterfaceImpl else []):
            ifimpl.setdefault(r.Class.row_index,[]).append((k+1,r))
        consts={}
        for c in (md.Constant.rows if md.Constant else []):
            if c.Parent is not None and c.Parent.table is not None and c.Parent.table.name=='Field':
                b=bytes(hv(c.Value)); tp=int(c.Type)
                consts[c.Parent.row_index]=int.from_bytes(b,'little',signed=(tp in (0x04,0x06,0x08,0x0A)))
        gp={}
        for r in (md.GenericParam.rows if md.GenericParam else []):
            if r.Owner.table.name=='TypeDef': gp[r.Owner.row_index]=gp.get(r.Owner.row_index,0)+1
        for i,td in enumerate(tds):
            t=Type(); t.pe=pe
            t.ns=str(hv(td.TypeNamespace) or ''); t.name=str(hv(td.TypeName))
            if t.name=='<Module>': continue
            t.generic_n=gp.get(i+1,0)
            is_iface=bool(td.Flags.tdInterface)
            ext=None
            if td.Extends is not None and td.Extends.row_index:
                ext=s._typename(pe,td.Extends)
            t.base=ext
            if is_iface: t.kind='interface'
            elif ext=='System.Enum': t.kind='enum'
            elif ext=='System.ValueType': t.kind='struct'
            elif ext=='System.MulticastDelegate': t.kind='delegate'
            elif ext=='System.Attribute': t.kind='attribute'
            elif t.name=='ApiContract' or 'ApiContract' in t.name: t.kind='apicontract'
            else: t.kind='class'
            # members
            if td.MethodList:
                for mref in td.MethodList:
                    mi=mref.row_index
                    m=mdr[mi-1]; t.methods.append(s._method(pe,m,mi,prm,mdr))
            if td.FieldList:
                for fref in td.FieldList:
                    f=fld[fref.row_index-1]; nm=str(hv(f.Name))
                    sig=hv(f.Signature)
                    if f.Flags.fdStatic:
                        if t.kind=='enum' and fref.row_index in consts: t.enum_values.append((nm,consts[fref.row_index]))
                        continue
                    ty=s._parse_field(pe,bytes(sig))
                    t.fields.append((nm,ty))
                if t.kind=='enum':
                    for nm,ty in t.fields:
                        if nm=='value__': t.enum_base=ty[1] if ty[0]=='prim' else 'int32'
            # custom attrs
            for c in attrs.get(('TypeDef',i+1),[]):
                s._type_attr(t,pe,c)
            # interface impls
            for k,r in ifimpl.get(i+1,[]):
                ref=s._typeref(pe,r.Interface)
                d=False
                for c in attrs.get(('InterfaceImpl',k),[]):
                    an=s._attr_name(pe,c)
                    if an=='Windows.Foundation.Metadata.DefaultAttribute': d=True
                t.interfaces.append(ref)
                if d: t.default_iface=ref
            s.types[t.full if not t.generic_n else t.ns+'.'+t.name]=t
    # ---------- type name resolution
    def _typename(s,pe,idx):
        tb=idx.table.name if idx.table else None
        if tb=='TypeDef':
            r=pe.net.mdtables.TypeDef.rows[idx.row_index-1]
            return (str(hv(r.TypeNamespace))+'.' if hv(r.TypeNamespace) else '')+str(hv(r.TypeName))
        if tb=='TypeRef':
            r=pe.net.mdtables.TypeRef.rows[idx.row_index-1]
            return (str(hv(r.TypeNamespace))+'.' if hv(r.TypeNamespace) else '')+str(hv(r.TypeName))
        return '?'
    def _typeref(s,pe,idx):
        tb=idx.table.name if idx.table else None
        if tb in ('TypeDef','TypeRef'): return ('type',s._typename(pe,idx))
        if tb=='TypeSpec':
            blob=bytes(hv(pe.net.mdtables.TypeSpec.rows[idx.row_index-1].Signature))
            ty,_=s._sigtype(pe,blob,0); return ty
        return ('type','?')
    # ---------- blobs
    @staticmethod
    def _cu(b,p):
        x=b[p]
        if x&0x80==0: return x,p+1
        if x&0xC0==0x80: return ((x&0x3F)<<8)|b[p+1],p+2
        return ((x&0x1F)<<24)|(b[p+1]<<16)|(b[p+2]<<8)|b[p+3],p+4
    def _tdor(s,pe,tok):
        tag=tok&3; row=tok>>2
        name={0:'TypeDef',1:'TypeRef',2:'TypeSpec'}[tag]
        class I: pass
        i=I(); i.row_index=row
        class T: pass
        i.table=T(); i.table.name=name
        return i
    def _sigtype(s,pe,b,p):
        e=b[p]; p+=1
        if e in PRIM: return ('prim',PRIM[e]),p
        if e==0x01: return ('void',),p
        if e==0x0E: return ('str',),p
        if e==0x1C: return ('obj',),p
        if e in (0x11,0x12):
            tok,p=s._cu(b,p); ref=s._typeref(pe,s._tdor(pe,tok))
            if ref==('type','System.Guid'): return ('guid',),p
            return ref,p
        if e==0x15:
            p+=1; tok,p=s._cu(b,p); base=s._typeref(pe,s._tdor(pe,tok)); n,p=s._cu(b,p); args=[]
            for _ in range(n):
                a,p=s._sigtype(pe,b,p); args.append(a)
            return ('inst',base[1],args),p
        if e==0x1D:
            a,p=s._sigtype(pe,b,p); return ('arr',a),p
        if e==0x13:
            n,p=s._cu(b,p); return ('var',n),p
        if e==0x10:
            a,p=s._sigtype(pe,b,p); return ('byref',a),p
        if e in (0x1F,0x20):   # cmod
            _,p=s._cu(b,p); return s._sigtype(pe,b,p)
        raise ValueError('sig elem 0x%x'%e)
    def _parse_field(s,pe,b):
        assert b[0]==6
        ty,_=s._sigtype(pe,b,1); return ty
    def _method(s,pe,m,idx,prm,mdr):
        mm=Method(); mm.name=str(hv(m.Name)); b=bytes(hv(m.Signature)); p=0
        cc=b[p]; p+=1
        if cc&0x10: _,p=s._cu(b,p)
        n,p=s._cu(b,p)
        mm.ret,p=s._sigtype(pe,b,p); mm.params=[]
        # param names/flags
        pn={}
        plist=m.ParamList
        if plist:
            for pref in plist:
                r=prm[pref.row_index-1]; fl=(1 if r.Flags.pdIn else 0)|(2 if r.Flags.pdOut else 0)
                pn[int(r.Sequence)]=(str(hv(r.Name)),fl)
        for k in range(n):
            ty,p=s._sigtype(pe,b,p); pp=Param(); nm,fl=pn.get(k+1,('p%d'%k,1))
            pp.name=nm; pp.type=ty; pp.is_out=bool(fl&2); pp.is_in=bool(fl&1) or not pp.is_out
            mm.params.append(pp)
        mm.flags=0; mm.special=bool(m.Flags.mdSpecialName)
        return mm
    # ---------- attributes
    def _attr_name(s,pe,c):
        t=c.Type
        if t.table.name=='MemberRef':
            r=pe.net.mdtables.MemberRef.rows[t.row_index-1]
            return s._typename(pe,r.Class)
        if t.table.name=='MethodDef':
            return None
        return None
    def _attr_ctor_sig(s,pe,c):
        r=pe.net.mdtables.MemberRef.rows[c.Type.row_index-1]
        b=bytes(hv(r.Signature)); p=1; n,p=s._cu(b,p); _,p=s._sigtype(pe,b,p); ps=[]
        for _ in range(n):
            ty,p=s._sigtype(pe,b,p); ps.append(ty)
        return ps
    def _attr_args(s,pe,c):
        if c.Type.table.name!='MemberRef': return None
        ps=s._attr_ctor_sig(pe,c); b=bytes(hv(c.Value)); p=2; out=[]
        for ty in ps:
            if ty[0]=='prim':
                sz={'bool':1,'int8':1,'uint8':1,'int16':2,'uint16':2,'char16':2,'int32':4,'uint32':4,'int64':8,'uint64':8,'float':4,'double':8}[ty[1]]
                out.append(int.from_bytes(b[p:p+sz],'little')); p+=sz
            elif ty[0]=='str' or ty==('type','System.Type'):
                if b[p]==0xFF: out.append(None); p+=1
                else:
                    ln,p=s._cu(b,p); out.append(b[p:p+ln].decode('utf8')); p+=ln
            elif ty[0]=='type':      # enum (4 bytes) assumed
                out.append(int.from_bytes(b[p:p+4],'little')); p+=4
            else: out.append(None)
        return out
    def _type_attr(s,t,pe,c):
        an=s._attr_name(pe,c)
        if not an: return
        a=s._attr_args(pe,c)
        if an=='Windows.Foundation.Metadata.GuidAttribute' and a:
            d1,d2,d3=a[0],a[1],a[2]; d4=bytes(a[3:11])
            t.iid='%08X-%04X-%04X-%02X%02X-%s'%(d1,d2,d3,d4[0],d4[1],d4[2:].hex().upper())
        elif an=='Windows.Foundation.Metadata.StaticAttribute' and a: t.statics.append(a[0])
        elif an=='Windows.Foundation.Metadata.ActivatableAttribute' and a:
            t.activatable.append(a[0] if isinstance(a[0],str) else None)
        elif an=='Windows.Foundation.Metadata.ComposableAttribute' and a: t.composable.append(a[0])
        elif an=='Windows.Foundation.Metadata.ExclusiveToAttribute' and a: t.exclusive_to=a[0]
        elif an=='Windows.Foundation.Metadata.ContractVersionAttribute' and a: t.contract=a

def load_all(*dirs, cache=None):
    import pickle
    if cache and os.path.exists(cache):
        return pickle.load(open(cache,'rb'))
    r=Reg()
    for d in dirs: r.load_dir(d)
    for t in r.types.values(): t.pe=None
    r.pes=[]
    if cache: pickle.dump(r,open(cache,'wb'),protocol=4)
    return r

if __name__=='__main__':
    S=sys.argv[1]
    r=load_all(S)
    print(len(r.types),'types')
    for k in sys.argv[2:]:
        t=r.types[k]; print(t.kind,t.full,t.iid,'default',t.default_iface,'statics',t.statics,'comp',t.composable,'act',t.activatable)
        for m in t.methods: print('  ',m.name,m.ret,[(p.name,p.type) for p in m.params])
