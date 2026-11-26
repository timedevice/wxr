# strip stubs from gen/comp_stubs.c for CI_ methods listed in a defined.txt (offline substitute for gen_comp.py)
import re,sys
defd=set(open(sys.argv[2]).read().split())
src=open(sys.argv[1]).read()
parts=re.split(r'(?m)^(?=HRESULT STDMETHODCALLTYPE CI_)',src)
out=[parts[0]]
for p in parts[1:]:
    m=re.match(r'HRESULT STDMETHODCALLTYPE (CI_\w+)\(',p)
    if m and m.group(1) in defd: continue
    out.append(p)
open(sys.argv[1],'w').write(''.join(out))
