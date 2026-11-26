"""gen_dcomp_tables.py - hand-written COM vtables of dcomp.dll (no metadata exists for them).  Every slot either points at a real function
dcD_*/dcV_*/dcS_*/... (listed in IMPL) or at a numbered logging stub.  Layouts: public dcomp.h (devices, visuals) + wine-uwp's reading of dcomp.pdb
(interop device 76 slots, interop visual 52 slots).  usage: python3 gen_dcomp_tables.py <outdir>"""
import sys, os
out = sys.argv[1] if len(sys.argv) > 1 else '.'
DEV2 = ['Commit','WaitForCommitCompletion','GetFrameStatistics','CreateVisual','CreateSurfaceFactory','CreateSurface','CreateVirtualSurface',
        'CreateTranslateTransform','CreateScaleTransform','CreateRotateTransform','CreateSkewTransform','CreateMatrixTransform','CreateTransformGroup',
        'CreateTranslateTransform3D','CreateScaleTransform3D','CreateRotateTransform3D','CreateMatrixTransform3D','CreateTransform3DGroup',
        'CreateEffectGroup','CreateRectangleClip','CreateAnimation']                                          # slots 3..23
DESKTOP = DEV2 + ['CreateTargetForHwnd','CreateSurfaceFromHandle','CreateSurfaceFromHwnd']                     # 3..26
DEV3 = DEV2 + ['CreateGaussianBlurEffect','CreateBrightnessEffect','CreateColorMatrixEffect','CreateShadowEffect','CreateHueRotationEffect',
               'CreateSaturationEffect','CreateTurbulenceEffect','CreateLinearTransferEffect','CreateTableTransferEffect','CreateCompositeEffect',
               'CreateBlendEffect','CreateArithmeticCompositeEffect','CreateAffineTransform2DEffect']
DEV1 = ['Commit','WaitForCommitCompletion','GetFrameStatistics','CreateTargetForHwnd','CreateVisual','CreateSurface','CreateVirtualSurface','CreateSurfaceFromHandle',
        'CreateSurfaceFromHwnd','CreateTranslateTransform','CreateScaleTransform','CreateRotateTransform','CreateSkewTransform','CreateMatrixTransform','CreateTransformGroup',
        'CreateTranslateTransform3D','CreateScaleTransform3D','CreateRotateTransform3D','CreateMatrixTransform3D','CreateTransform3DGroup','CreateEffectGroup',
        'CreateRectangleClip','CreateAnimation','CheckDeviceState']
INTEROP = DESKTOP + ['CreateSharedResource','OpenSharedResourceHandle','OpenSharedResource','CreateAnimationTrigger','CreateGradientSurface',   # 27..31
        'EnableWhitePixelOptimization','OfferSurfaceResources','ReclaimSurfaceResources','DisableD2DStatePreservation','Flush',                        # 32..36
        'CreateSurfaceFromVisualSnapshot','CreateInteraction','CreateTouchInteractionConfiguration','CreateTouchpadInteractionConfiguration',            # 37..40
        'CreatePenInteractionConfiguration','CreateMouseInteractionConfiguration','OpenSharedResource2','CreateDesktopTree','GetCurrentBatchID',        # 41..45
        'GetLastConfirmedBatchId','CreateGaussianBlurEffect','CreateManipulationTransform','CreateBrightnessEffect','CreateColorMatrixEffect',          # 46..50
        'CreateShadowEffect','CreateHueRotationEffect','CreateSaturationEffect','CreateTurbulenceEffect','CreateLinearTransferEffect',                 # 51..55
        'CreateTableTransferEffect','RegisterCallbackThread','CreateCompositeEffect','SnapAnimationReadBackTime'] + ['Slot%d' % i for i in range(60, 76)]
INTEROP[67 - 3] = 'CommitMainDevice'   # 19041: XAML's DCompTreeHost::CommitMainDevice calls slot 67 (0x218) every frame; wine-uwp's 26100 reading had 69
assert len(INTEROP) == 73, len(INTEROP)
VIS1 = ['SetOffsetXAnim','SetOffsetX','SetOffsetYAnim','SetOffsetY','SetTransformObject','SetTransformMatrix','SetTransformParent','SetEffect',
        'SetBitmapInterpolationMode','SetBorderMode','SetClipObject','SetClipRect','SetContent','AddVisual','RemoveVisual','RemoveAllVisuals','SetCompositeMode']   # 3..19
INTEROP_VIS = VIS1 + ['SetOpacityMode','SetBackFaceVisibility','Slot22','Slot23','Slot24','Slot25','SetDepthMode','SetOffsetZAnim','SetOffsetZ','SetOpacity','SetOpacityAnim',
        'Slot31','Slot32','SetVisible','SetTransformMode','SetInput','SetWidth','SetHeight','Slot38','Slot39','Slot40','Slot41','Slot42','Slot43',
        'GetVisualCollection','RealClose','Slot46','Slot47','Slot48','SetTransformParent2','Slot50','Slot51']                                          # 3..51
# 19041 layout: XAML (HWCompTreeNodeWinRT::GetVisualCollectionFromWUCSpineVisual) calls GetVisualCollection at 44, one before wine-uwp's 26100
# reading (45); SetInput at 35 still matches, so 26100's extra slot lies in 38..43 and everything from GetVisualCollection on moves down one.
assert len(INTEROP_VIS) == 49, len(INTEROP_VIS)
# IDCompositionVisual3 derives from IDCompositionVisualDebug (slots 22..25: heat map / redraw regions), which the old list skipped: everything
# from SetDepthMode on was 4 slots early and the table ended at 29. WinUI 3's dwmcorei (CLiftedOverlayPlane::SetTreeTransform) calls slot 31 and
# jumped to 0. Now the same prefix as the interop visual (through SetVisible at 33).
VIS3 = INTEROP_VIS[:31]
assert VIS3[-1] == 'SetVisible', VIS3[-1]
SURF = ['BeginDraw','EndDraw','SuspendDraw','ResumeDraw','Scroll']
VSURF = SURF + ['Resize','Trim']
# IDCompositionSurfaceInternal {5680c979}, 19041 layout: XAML's DCompSurface::IsDiscarded calls HasValidPixels at 9 (wine-uwp's 26100 reading has
# BeginDrawWithClear at 9 and the rest one later); BeginDrawWithClear kept at 12 for any newer client
SURFINT = SURF + ['BeginDrawWithGutters','HasValidPixels','Resize','CopySurface','BeginDrawWithClear']
FACT = ['CreateSurface','CreateVirtualSurface']
TARGET = ['SetRoot']
PARTNER = ['MarkDirty','ClearCallback','CreateManipulationTransform','RealClose']
# Windows 10 19041 layout, from its callers: DirectManipulation's compositor calls slot 8 with the event it just created
# (SetCommitCompletionEvent), XAML's EnsureAnimationTrackingAppId calls slot 9 (id, name). wine-uwp's newer dcomp.pdb order differs;
# slots 4-7 are unidentified (logging stubs), the batch-id pair after 9 is a guess.
INTERNAL = ['HintSize','Slot4','Slot5','Slot6','Slot7','SetCommitCompletionEvent','TelemetrySetApplicationId','GetCurrentBatchId','GetFrameIdFromBatchId'] + ['Slot%d' % i for i in range(12, 24)]
DEBUG = ['EnableDebugCounters','DisableDebugCounters']
PRIV5 = ['Slot3','Slot4','Slot5','CreateCompositionSurfaceForDCompositionSurface'] + ['Slot%d' % i for i in range(7, 24)]
PRIV = ['Slot%d' % i for i in range(3, 24)]
TABLES = [  # (C name, method names, prefix of implementations)
    ('Dev1', DEV1, 'dcD'), ('Dev2', DEV2, 'dcD'), ('Desktop', DESKTOP, 'dcD'), ('Dev3', DEV3, 'dcD'), ('Interop', INTEROP, 'dcD'),
    ('Vis', INTEROP_VIS, 'dcV'), ('Vis3', VIS3, 'dcV'), ('Surf', SURF, 'dcS'), ('VSurf', VSURF, 'dcS'), ('SurfInt', SURFINT, 'dcS'), ('Fact', FACT, 'dcF'), ('Target', TARGET, 'dcT'),
    ('Partner', PARTNER, 'dcP'), ('Internal', INTERNAL, 'dcI'), ('Debug', DEBUG, 'dcG'), ('Priv5', PRIV5, 'dcX'), ('Priv', PRIV, 'dcX')]
# Win10 19045 Compositor interfaces (dcomp.pdb Compositor::s_InterfaceParts) that nothing else answers yet: one named probe table each,
# so the log says which interface and slot XAML 19041 uses (dcomp_visual.c, DC_PROBE_IIDS)
NPROBE = 20
TABLES += [('Q%d' % k, ['Slot%d' % i for i in range(3, 40)], 'dcX') for k in range(NPROBE)]
# implemented (strong) functions, by prefix: everything else becomes a logging stub
IMPL = {
 'dcD': 'Commit WaitForCommitCompletion GetFrameStatistics CreateVisual CreateSurfaceFactory CreateSurface CreateVirtualSurface CreateTranslateTransform CreateScaleTransform CreateRotateTransform CreateSkewTransform CreateMatrixTransform CreateTransformGroup CreateRectangleClip CreateAnimation CreateTargetForHwnd CheckDeviceState EnableWhitePixelOptimization OfferSurfaceResources ReclaimSurfaceResources DisableD2DStatePreservation Flush CommitMainDevice CreateSharedResource OpenSharedResourceHandle OpenSharedResource CreateInteraction CreateTouchInteractionConfiguration CreateTouchpadInteractionConfiguration CreatePenInteractionConfiguration OpenSharedResource2 CreateEffectGroup GetCurrentBatchID GetLastConfirmedBatchId'.split(),
 'dcV': 'SetOffsetXAnim SetOffsetYAnim SetOpacityAnim SetOffsetX SetOffsetY SetTransformObject SetTransformMatrix SetTransformParent SetBorderMode SetClipObject SetClipRect SetContent AddVisual RemoveVisual RemoveAllVisuals SetOpacity SetVisible SetWidth SetHeight GetVisualCollection RealClose SetTransformParent2'.split(),
 'dcS': 'BeginDraw EndDraw SuspendDraw ResumeDraw Scroll BeginDrawWithGutters BeginDrawWithClear HasValidPixels Resize CopySurface Trim'.split(),
 'dcF': 'CreateSurface CreateVirtualSurface'.split(), 'dcT': ['SetRoot'], 'dcP': 'MarkDirty ClearCallback RealClose CreateManipulationTransform'.split(),
 'dcI': 'HintSize SetCommitCompletionEvent TelemetrySetApplicationId GetCurrentBatchId GetFrameIdFromBatchId'.split(), 'dcG': 'EnableDebugCounters DisableDebugCounters'.split(), 'dcX': ['CreateCompositionSurfaceForDCompositionSurface'],
}
C = ['/* generated by gen_dcomp_tables.py - do not edit */', '#include "dcomp_priv.h"', '']
decl = set(); body = []
for tname, names, pre in TABLES:
    ent = ['(void *)hv_QI', '(void *)hv_AddRef', '(void *)hv_Release']
    for i, n in enumerate(names):
        slot = i + 3
        if n in IMPL.get(pre, []):
            fn = '%s_%s' % (pre, n); decl.add(fn); ent.append('(void *)%s' % fn)
        else:
            sn = 'dcstub_%s_%d' % (tname, slot); ret = 'E_NOTIMPL' if (n.startswith('Create') or n.startswith('Open')) else 'S_OK'
            body.append('static HRESULT STDMETHODCALLTYPE %s(void *This) { (void)This; return dc_stub("%s", %d, "%s", %s); }' % (sn, tname, slot, n, ret)); ent.append('(void *)' + sn)
    body.append('const void *const DCVT_%s[] = { %s };' % (tname, ', '.join(ent)))
for fn in sorted(decl): C.append('extern HRESULT STDMETHODCALLTYPE %s();' % fn)
C += [''] + body
open(os.path.join(out, 'dcomp_tables.c'), 'w').write('\n'.join(C) + '\n')
C.append('const void *const *const DCVT_Q[] = { %s };' % ', '.join('DCVT_Q%d' % k for k in range(NPROBE)))
open(os.path.join(out, 'dcomp_tables.c'), 'w').write('\n'.join(C) + '\n')
H = ['/* generated */', '#pragma once'] + ['extern const void *const DCVT_%s[];' % t[0] for t in TABLES] + ['extern const void *const *const DCVT_Q[];', '#define DC_NPROBE %d' % NPROBE]
open(os.path.join(out, 'dcomp_tables.h'), 'w').write('\n'.join(H) + '\n')
print('tables ok')
