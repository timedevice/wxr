# wilsum.py <log> [module filter]: counts the failure reports Windows DLLs write to the debugger (WIL ReturnHr / LogHr / ...)
# in a wxrrun log (logs\wxr_run.log), by module, source line and HRESULT, with the first address seen.
import sys, re, collections
flt = sys.argv[2].lower() if len(sys.argv) > 2 else ''
c = collections.Counter(); first = {}
rx = re.compile(r'\\([^\\]+\.cpp\(\d+\))\\([^!]+)!([0-9A-Fa-f]+): \(caller: ([0-9A-Fa-f]+)\) (\w+)\(\d+\) tid\(\w+\) (\w+) ?(.*)')
for l in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = rx.search(l)
    if not m or (flt and flt not in m.group(2).lower()): continue
    k = (m.group(2), m.group(1), m.group(6), m.group(7).strip()[:60]); c[k] += 1
    first.setdefault(k, (m.group(3), m.group(4)))
for k, v in c.most_common(40): print(v, *k, 'at', *first[k])
