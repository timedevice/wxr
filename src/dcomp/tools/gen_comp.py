"""gen_comp.py - generate the Windows.UI.Composition ABI header + interface plumbing for dcomp.dll from .winmd metadata.
usage: WXR_WINMD_ROOT=<dir containing winmd_system/> python3 gen_comp.py <outdir>
emits   comp_abi.h  (vtables/IIDs/structs/enums)
        comp_gen.h  (prototype of every interface method as  CI_<Iface>_<Method>  + class table)
        comp_gen.c  (weak default stub for every method, one vtable per interface, class descriptors)
A hand-written file overrides a stub simply by defining a function with the same name (strong beats weak)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_abi as G
T = G.T

CLASSES = '''Compositor CompositionObject CompositionPropertySet Visual ContainerVisual SpriteVisual ShapeVisual LayerVisual VisualCollection
CompositionBrush CompositionColorBrush CompositionGradientBrush CompositionLinearGradientBrush CompositionRadialGradientBrush
CompositionColorGradientStop CompositionColorGradientStopCollection CompositionSurfaceBrush CompositionMaskBrush CompositionNineGridBrush
CompositionEffectBrush CompositionBackdropBrush CompositionEffectFactory
CompositionClip InsetClip CompositionGeometricClip
CompositionGeometry CompositionRectangleGeometry CompositionRoundedRectangleGeometry CompositionEllipseGeometry CompositionPathGeometry CompositionLineGeometry
CompositionPath CompositionShape CompositionSpriteShape CompositionContainerShape CompositionShapeCollection CompositionStrokeDashArray
CompositionAnimation CompositionAnimationGroup KeyFrameAnimation ScalarKeyFrameAnimation Vector2KeyFrameAnimation Vector3KeyFrameAnimation
Vector4KeyFrameAnimation ColorKeyFrameAnimation BooleanKeyFrameAnimation QuaternionKeyFrameAnimation ExpressionAnimation
CompositionEasingFunction LinearEasingFunction CubicBezierEasingFunction StepEasingFunction
ImplicitAnimationCollection CompositionScopedBatch CompositionCommitBatch CompositionBatchCompletedEventArgs
CompositionTarget Desktop.DesktopWindowTarget CompositionGraphicsDevice CompositionDrawingSurface CompositionVirtualDrawingSurface
CompositionShadow DropShadow CompositionLight AmbientLight DistantLight PointLight SpotLight CompositionCapabilities
CompositionProjectedShadow CompositionProjectedShadowCaster CompositionProjectedShadowCasterCollection CompositionProjectedShadowReceiver
CompositionProjectedShadowReceiverUnorderedCollection AnimationController
RenderingDeviceReplacedEventArgs CompositionViewBox CompositionVisualSurface
Interactions.VisualInteractionSource Interactions.InteractionTracker Interactions.CompositionInteractionSourceCollection
Interactions.InteractionTrackerInertiaModifier Interactions.InteractionTrackerInertiaRestingValue Interactions.InteractionTrackerInertiaMotion Interactions.InteractionTrackerInertiaNaturalMotion
Interactions.InteractionTrackerVector2InertiaModifier Interactions.InteractionTrackerVector2InertiaNaturalMotion Interactions.CompositionConditionalValue Interactions.InteractionSourceConfiguration
PathKeyFrameAnimation NaturalMotionAnimation ScalarNaturalMotionAnimation Vector2NaturalMotionAnimation Vector3NaturalMotionAnimation
SpringScalarNaturalMotionAnimation SpringVector2NaturalMotionAnimation SpringVector3NaturalMotionAnimation
BounceScalarNaturalMotionAnimation BounceVector2NaturalMotionAnimation BounceVector3NaturalMotionAnimation
BackEasingFunction BounceEasingFunction CircleEasingFunction ElasticEasingFunction ExponentialEasingFunction PowerEasingFunction SineEasingFunction
RedirectVisual CompositionMipmapSurface CompositionTransform
AnimationPropertyInfo Interactions.InteractionTrackerValuesChangedArgs Interactions.InteractionTrackerIdleStateEnteredArgs
Interactions.InteractionTrackerCustomAnimationStateEnteredArgs Interactions.InteractionTrackerInertiaStateEnteredArgs
Interactions.InteractionTrackerInteractingStateEnteredArgs Interactions.InteractionTrackerRequestIgnoredArgs'''.split()
NS = 'Windows.UI.Composition.'
DEFINED = set()
if len(sys.argv) > 2 and os.path.exists(sys.argv[2]): DEFINED = set(l.strip() for l in open(sys.argv[2]) if l.strip())

def short(full):  # Windows.UI.Composition.Desktop.DesktopWindowTarget -> DesktopWindowTarget
    return full.split('.')[-1]

def ifname(ref):
    """('type', name) or ('inst', base, args) -> (mangled c name, def/None, args, iid or None)"""
    if ref[0] == 'type':
        return G.mangle(ref[1]), T[ref[1]], None, None
    base, args = ref[1], ref[2]
    n = G.mangle(base) + '__' + '__'.join(G.inst_name(a) for a in args)
    return n, T[base], args, G.inst_iid(ref)

def iface_key(ref):
    return ref[1] if ref[0] == 'type' else G.mangle(ref[1]) + '__' + '__'.join(G.inst_name(a) for a in ref[2])

def cshort(n):
    return n[len('Windows_UI_Composition_'):] if n.startswith('Windows_UI_Composition_') else n

def guid_lit(g):
    p = g.split('-')
    return '{0x%s,0x%s,0x%s,{0x%s,0x%s,0x%s,0x%s,0x%s,0x%s,0x%s,0x%s}}' % (p[0], p[1], p[2], p[3][:2], p[3][2:], p[4][0:2], p[4][2:4], p[4][4:6], p[4][6:8], p[4][8:10], p[4][10:12])

OK_PREFIX = ('put_', 'Set', 'Insert', 'Add', 'Append', 'Remove', 'Clear', 'Start', 'Stop', 'Resume', 'Suspend', 'Trim', 'ReplaceAll', 'add_', 'remove_', 'get_', 'HasKey', 'Pause', 'Close', 'Populate', 'Lookup', 'Connect', 'Disconnect', 'Dispose')
CLASS_OF_IFACE = {}
def stub_kind(fn, retinfo):
    """what a method that nobody implemented does: ('create', CLASS, IFACE|None) -> returns an inert object; ('ok',) -> benign S_OK no-op / default value; None -> E_NOTIMPL"""
    r = retinfo[0]
    if fn.startswith('Create') and r[0] == 'type':
        d = T[r[1]]
        if d.kind == 'class' and d.full in ALLC: return ('create', short(d.full), None)
        if d.kind == 'interface' and d.full in CLASS_OF_IFACE: return ('create', CLASS_OF_IFACE[d.full], G.mangle(d.full))
        return None
    if fn.startswith(OK_PREFIX): return ('ok',)
    return None

def main():
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    g = G.Gen()
    full = []
    for c in CLASSES:
        f = NS + c if '.' not in c else NS + c
        full.append(f)
    # base closure
    allc = []
    def add(f):
        if f in allc or f not in T or not f.startswith('Windows.UI.Composition'): return
        t = T[f]
        if t.base and t.base.startswith('Windows.UI.Composition'): add(t.base)
        allc.append(f)
    for f in full: add(f)
    global ALLC; ALLC = set(allc)
    for f in allc:                                     # interface -> a class that has it as default interface (for Create* returning interfaces)
        t = T[f]
        if t.default_iface and t.default_iface[0] == 'type': CLASS_OF_IFACE.setdefault(t.default_iface[1], short(f))
    # emit interface vtables
    iface_order = []   # (cname, decl, args, iid, ref)
    seen = set()
    def emit(ref):
        k = iface_key(ref)
        if k in seen: return
        seen.add(k)
        n, d, args, iid = ifname(ref)
        g.emit_iface(d, name=n, args=args, iid=iid)
        iface_order.append((n, d, args, iid, ref))
    cls_ifaces = {}; cls_statics = {}
    for f in allc:
        t = T[f]
        own = list(t.interfaces)
        for r in own: emit(r)
        cls_ifaces[f] = own
        st = [('type', x) for x in t.statics] + [('type', x) for x in t.factories if x] + [('type', x) for x in t.composable if x] + [('type', x) for x in t.activatable if x]
        for r in st: emit(r)
        cls_statics[f] = st
    # force structs for common value types
    for s in ('Windows.Foundation.Numerics.Vector2', 'Windows.Foundation.Numerics.Vector3', 'Windows.Foundation.Numerics.Vector4',
              'Windows.Foundation.Numerics.Matrix3x2', 'Windows.Foundation.Numerics.Matrix4x4', 'Windows.Foundation.Numerics.Quaternion',
              'Windows.UI.Color', 'Windows.Foundation.TimeSpan', 'Windows.Foundation.Rect', 'Windows.Foundation.Size', 'Windows.Foundation.Point'):
        if s in T: g.add_root(s)
    hdr = g.render()
    open(os.path.join(out, 'comp_abi.h'), 'w').write(hdr)
    # ---------------- stubs
    H = ['/* generated by gen_comp.py - do not edit */', '#pragma once', '#include "comp_abi.h"', '#include "comp_base.h"', '']
    C = ['/* generated by gen_comp.py - do not edit.  Interface vtables and class descriptors.  (Default stubs are in comp_stubs.c.) */',
         '#include "comp_gen.h"', '']
    S = ['/* generated by gen_comp.py - do not edit.  Default stub for every interface method that no hand-written file defines (build.sh passes the list of defined CI_ symbols).',
         ' * Setters/starters/collection mutators are logged no-ops returning S_OK, getters return zeros with S_OK, Create* methods of known classes return an inert object,',
         ' * everything else logs UNIMPLEMENTED and returns E_NOTIMPL. */',
         '#include "comp_types.h"', '']
    done = set()
    for (n, d, args, iid, ref) in iface_order:
        sn = cshort(n)
        seen_fn = {}
        meths = []
        for m in d.methods:
            if m.name in ('.ctor', '.cctor'): continue
            if d.kind == 'delegate': continue
            fn = m.name; seen_fn[fn] = seen_fn.get(fn, 0) + 1
            if seen_fn[fn] > 1: fn = '%s_%d' % (fn, seen_fn[fn])
            ps = g.params(m, args)
            outs = []
            for p in m.params:
                ty = g.resolve(p.type, args); nm = p.name + ('_' if (p.name in G.CKW or p.name in ('result', 'resultSize')) else '')
                if ty[0] == 'byref':
                    if ty[1][0] == 'arr': outs += [nm + 'Size', nm]
                    else: outs.append(nm)
            r = g.resolve(m.ret, args)
            if r[0] == 'arr': outs += ['resultSize', 'result']
            elif r[0] != 'void': outs.append('result')
            meths.append((fn, ps, outs, (r, d.full)))
        for fn, ps, outs, retinfo in meths:
            name = 'CI_%s_%s' % (sn, fn)
            proto = 'HRESULT STDMETHODCALLTYPE %s(%s)' % (name, ', '.join(['%s* This' % n] + ps))
            H.append(proto + ';')
            body = ['    (void)This;']
            what = '%s.%s' % (sn, fn)
            for o in outs: body.append('    if (%s) memset(%s, 0, sizeof(*%s));' % (o, o, o))
            kind = stub_kind(fn, retinfo)
            if kind and kind[0] == 'create':
                cls, iface = kind[1], kind[2]
                body.append('    { CObj *c = OBJ_OF(This), *o; HRESULT hr; if (!result) return E_POINTER;')
                body.append('      o = comp_new(CLS_%s, c->cls == CLS_Compositor ? c : c->comp); if (!o) return E_OUTOFMEMORY;' % cls)
                body.append('      { static LONG once; if (!InterlockedExchange(&once, 1)) comp_log("UNIMPLEMENTED(inert object) %s -> %s\\n"); }' % (what, cls))
                body.append('      hr = %s; comp_release(o); return hr; }' % ('comp_qi(o, &IID_%s, (void **)result)' % iface if iface else 'comp_return_default(o, (void **)result)'))
            elif kind and kind[0] == 'ok':
                if fn.startswith('add_'): body.append('    if (result) result->Value = 1;')
                body.append('    { static LONG once; if (!InterlockedExchange(&once, 1)) comp_log("UNIMPLEMENTED(no-op, returns S_OK) %s\\n"); }' % what)
                body.append('    return S_OK;')
            else:
                body.append('    return comp_unimpl("%s");' % what)
            if not (name in DEFINED): S.append(proto + ' {\n' + '\n'.join(body) + '\n}')
        # vtable
        ent = ['(void*)cv_QI', '(void*)cv_AddRef', '(void*)cv_Release', '(void*)cv_GetIids', '(void*)cv_GetRuntimeClassName', '(void*)cv_GetTrustLevel']
        if d.kind == 'interface':
            ent += ['CI_%s_%s' % (sn, fn) for fn, ps, outs, _ri in meths]
        H.append('extern const %sVtbl VT_%s;' % (n, sn))
        C.append('const %sVtbl VT_%s = { %s };' % (n, sn, ', '.join(ent)))
        C.append('')
    # class table
    H.append('')
    H.append('enum {')
    for f in allc: H.append('    CLS_%s,' % short(f))
    H.append('    CLS__COUNT\n};')
    H.append('extern const ClassDesc g_cd[CLS__COUNT];')
    for (n, d, args, iid, ref) in iface_order:
        pass
    C.append('')
    flat = {}
    for f in allc:
        lst = []; t = T[f]
        # own interfaces first, then base chain
        cur = f
        while cur and cur in T and cur.startswith('Windows.UI.Composition'):
            for r in cls_ifaces[cur]:
                if iface_key(r) not in [iface_key(x) for x in lst]: lst.append(r)
            cur = T[cur].base
        flat[f] = lst
    for f in allc:
        s = short(f)
        C.append('static const IfaceEntry ifs_%s[] = {' % s)
        for r in flat[f]:
            n = ifname(r)[0]; C.append('    { &IID_%s, &VT_%s, "%s" },' % (n, cshort(n), cshort(n)))
        C.append('    { 0, 0, 0 } };')
        C.append('static const IfaceEntry st_%s[] = {' % s)
        for r in cls_statics[f]:
            n = ifname(r)[0]; C.append('    { &IID_%s, &VT_%s, "%s" },' % (n, cshort(n), cshort(n)))
        C.append('    { 0, 0, 0 } };')
    C.append('const ClassDesc g_cd[CLS__COUNT] = {')
    for f in allc:
        s = short(f); t = T[f]
        base = ('CLS_' + short(t.base)) if (t.base and t.base in allc) else '-1'
        di = ifname(t.default_iface)[0] if t.default_iface else ''
        act = 1 if (t.activatable and any(x is None for x in t.activatable)) else 0
        C.append('    { L"%s", "%s", %s, ifs_%s, st_%s, "%s", %d },' % (f, s, base, s, s, cshort(di), act))
    C.append('};')
    open(os.path.join(out, 'comp_gen.h'), 'w').write('\n'.join(H) + '\n')
    open(os.path.join(out, 'comp_gen.c'), 'w').write('\n'.join(C) + '\n')
    open(os.path.join(out, 'comp_stubs.c'), 'w').write('\n'.join(S) + '\n')
    print('classes', len(allc), 'interfaces', len(iface_order), 'stubs', sum(1 for l in H if l.startswith('HRESULT')))
main()
