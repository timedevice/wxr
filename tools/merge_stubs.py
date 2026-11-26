"""merge_stubs.py <shipped comp_stubs.c> <regenerated comp_stubs.c> <out>: keep the shipped (hand-edited) file; take from the regenerated one
only (a) functions it has that the shipped one lacks and (b) the functions named on the command line after --replace."""
import re, sys

def funcs(src):
    parts = re.split(r'(?m)^(?=HRESULT STDMETHODCALLTYPE CI_)', src)
    head, out = parts[0], {}
    order = []
    for p in parts[1:]:
        m = re.match(r'HRESULT STDMETHODCALLTYPE (CI_\w+)\(', p)
        out[m.group(1)] = p
        order.append(m.group(1))
    return head, out, order

shipped, regen, dst = sys.argv[1], sys.argv[2], sys.argv[3]
replace = set(sys.argv[sys.argv.index('--replace') + 1:]) if '--replace' in sys.argv else set()
s_src = open(shipped).read()
_, rf, rorder = funcs(open(regen).read())
for name in replace:
    m = re.search(r'(?ms)^HRESULT STDMETHODCALLTYPE %s\(.*?^}\n' % re.escape(name), s_src)
    if not m:
        sys.exit('not in shipped: ' + name)
    body = rf[name]
    body = body[:body.index('\n}\n') + 3] if '\n}\n' in body else body
    s_src = s_src[:m.start()] + body + s_src[m.end():]
    print('replaced', name)
new = [n for n in rorder if ('HRESULT STDMETHODCALLTYPE %s(' % n) not in s_src]
add = []
for n in new:
    b = rf[n]
    add.append(b[:b.index('\n}\n') + 3] if '\n}\n' in b else b)
if add:
    s_src = s_src.rstrip('\n') + '\n\n/* ---- CompositionVisualSurface (added by regenerating with gen_comp.py; merged by hand) ---- */\n' + ''.join(add)
open(dst, 'w').write(s_src)
print('added', len(add), ':', ' '.join(new))
