/* comp_interact.c - Windows.UI.Composition.Interactions.InteractionTracker
 *
 * What is real: the tracker's values (Position, Scale, their limits, NaturalRestingPosition) - readable by expressions
 * ("-_interactionTracker.Position.X + ..."), animatable ("MaxPosition.X"), and changed by TryUpdatePosition / TryUpdatePositionBy /
 * TryUpdatePositionWithAnimation / TryUpdatePositionWithAdditionalVelocity and the scale counterparts; the state machine
 * Idle <-> CustomAnimation / Inertia with the IInteractionTrackerOwner callbacks (state entered, ValuesChanged).
 * What is not: input. A VisualInteractionSource is an inert object, so touch / wheel never reach a tracker (no Interacting state),
 * and inertia modifiers (snap points) are accepted and ignored: inertia from an additional velocity eases to the natural resting
 * position. The Store's carousel pages with TryUpdatePositionWithAnimation and reads Position in expressions.
 *
 * Callbacks are delivered at the end of a frame, outside the composition lock, with the batch Completed events (comp_batch_fire). */
#include "comp_types.h"
#define CI_(i, m) CI_Interactions_##i##_##m
#define WI(n) Windows_UI_Composition_Interactions_##n
#define IID_I(n) IID_Windows_UI_Composition_Interactions_##n

enum { ST_IDLE, ST_CUSTOM, ST_INERTIA };
enum { F_CUSTOM = 1, F_IDLE = 2, F_INERTIA = 4, F_VALUES = 8 };
typedef struct Tracker {
    CObj o; struct Tracker *next; IUnknown *owner; CObj *sources;
    float pos[3], minp[3], maxp[3], rest[3], vel[3]; float scale, mins, maxs, rscale;
    int state, flags, dirty, reqid, linked;
    float last[4]; int have_last;
    float ifrom[3], ito[3]; LONGLONG it0;          /* inertia: ease from ifrom to ito, started at it0 */
} Tracker;
typedef struct TArgs { CObj o; float pos[3], scale, rest[3], rscale, vel[3]; int reqid; } TArgs;
#define TR(T) ((Tracker *)OBJ_OF(T))
#define INERTIA_TICKS 4000000LL                    /* 0.4 s */

static void tr_init(CObj *o)
{
    Tracker *t = (Tracker *)o; Compositor *c = (Compositor *)o->comp;
    t->scale = t->mins = t->maxs = t->rscale = 1.0f;
    if (c) { t->next = c->trackers; c->trackers = t; t->linked = 1; }
}
static void tr_destroy(CObj *o)
{
    Tracker *t = (Tracker *)o, **pp; Compositor *c = (Compositor *)o->comp;
    if (t->linked && c) for (pp = &c->trackers; *pp; pp = &(*pp)->next) if (*pp == t) { *pp = t->next; break; }
    if (t->owner) t->owner->lpVtbl->Release(t->owner);
    if (t->sources) comp_release(t->sources);
}
static void tr_clamp(Tracker *t)
{
    int i;
    for (i = 0; i < 3; i++) { float lo = t->minp[i], hi = t->maxp[i]; if (hi < lo) hi = lo; if (t->pos[i] < lo) t->pos[i] = lo; if (t->pos[i] > hi) t->pos[i] = hi; }
    if (t->maxs >= t->mins) { if (t->scale < t->mins) t->scale = t->mins; if (t->scale > t->maxs) t->scale = t->maxs; }
}
static void tr_touch(Tracker *t) { t->dirty = 1; INVALIDATE(&t->o); }
/* properties by name (expressions, animations). set: writes `v` (already merged with the current value by the caller) */
int comp_tracker_prop(CObj *o, const WCHAR *name, int set, int *dim, float *v)
{
    Tracker *t = (Tracker *)o; float *p3 = NULL, *p1 = NULL; float old[4];
    if (!wcscmp(name, L"Position")) p3 = t->pos; else if (!wcscmp(name, L"MinPosition")) p3 = t->minp; else if (!wcscmp(name, L"MaxPosition")) p3 = t->maxp;
    else if (!wcscmp(name, L"NaturalRestingPosition")) p3 = t->rest; else if (!wcscmp(name, L"PositionVelocityInPixelsPerSecond")) p3 = t->vel;
    else if (!wcscmp(name, L"Scale")) p1 = &t->scale; else if (!wcscmp(name, L"MinScale")) p1 = &t->mins; else if (!wcscmp(name, L"MaxScale")) p1 = &t->maxs;
    else if (!wcscmp(name, L"NaturalRestingScale")) p1 = &t->rscale;
    else if (!wcscmp(name, L"ScaleVelocityInPercentPerSecond") || !wcscmp(name, L"IsPositionRoundingSuggested")) { if (set) return 1; v[0] = 0; *dim = 1; return 1; }
    else return 0;
    if (!set) { if (p3) { memcpy(v, p3, 3 * sizeof(float)); *dim = 3; } else { v[0] = *p1; *dim = 1; } return 1; }
    memcpy(old, t->pos, sizeof t->pos); old[3] = t->scale;
    if (p3) memcpy(p3, v, 3 * sizeof(float)); else *p1 = v[0];
    tr_clamp(t);                                                    /* new limits move the position, a new position obeys the limits */
    if (memcmp(old, t->pos, sizeof t->pos) || old[3] != t->scale) tr_touch(t);
    return 1;
}
static int tr_animating(Tracker *t)
{
    Compositor *c = (Compositor *)t->o.comp; ActiveAnim *a; ExprBind *b;
    if (!c) return 0;
    for (a = c->active; a; a = a->next) if (a->target == &t->o && (!wcsncmp(a->prop, L"Position", 8) || !wcsncmp(a->prop, L"Scale", 5))) return 1;
    for (b = c->exprs; b; b = b->next) if (b->target == &t->o && (!wcsncmp(b->prop, L"Position", 8) || !wcsncmp(b->prop, L"Scale", 5))) return 1;
    return 0;
}
static void tr_stop_motion(Tracker *t) { comp_stop_animation(&t->o, L"Position"); comp_stop_animation(&t->o, L"Scale"); }
static void tr_enter(Tracker *t, int state)
{
    if (t->state == state && state == ST_IDLE) return;
    t->state = state; t->flags |= state == ST_IDLE ? F_IDLE : state == ST_CUSTOM ? F_CUSTOM : F_INERTIA; t->flags &= ~(state == ST_IDLE ? (F_CUSTOM | F_INERTIA) : F_IDLE);
    if (state == ST_IDLE) { memcpy(t->rest, t->pos, sizeof t->rest); t->rscale = t->scale; memset(t->vel, 0, sizeof t->vel); }
    tr_touch(t);
}
/* frames keep coming while a tracker moves by itself or has callbacks to deliver */
int comp_interact_busy(CObj *comp)
{
    Compositor *c = (Compositor *)comp; Tracker *t;
    for (t = c ? c->trackers : NULL; t; t = t->next) if (t->state == ST_INERTIA || t->dirty || t->flags) return 1;
    return 0;
}
/* comp_tick, after animations and expressions wrote the trackers' values */
void comp_interact_tick(CObj *comp)
{
    Compositor *c = (Compositor *)comp; Tracker *t; LONGLONG now = comp_clock();
    for (t = c ? c->trackers : NULL; t; t = t->next) {
        float cur[4];
        if (t->state == ST_INERTIA) {
            double p = (double)(now - t->it0) / (double)INERTIA_TICKS; int i;
            if (p >= 1.0) { memcpy(t->pos, t->ito, sizeof t->pos); tr_clamp(t); tr_enter(t, ST_IDLE); }
            else { double q = 1.0 - p, k = 1.0 - q * q * q; for (i = 0; i < 3; i++) t->pos[i] = (float)(t->ifrom[i] + (t->ito[i] - t->ifrom[i]) * k); tr_clamp(t); INVALIDATE(&t->o); }
        } else if (t->state == ST_CUSTOM && !tr_animating(t)) tr_enter(t, ST_IDLE);
        memcpy(cur, t->pos, sizeof t->pos); cur[3] = t->scale;
        if (!t->have_last || memcmp(cur, t->last, sizeof cur)) { if (t->have_last) t->flags |= F_VALUES; memcpy(t->last, cur, sizeof cur); t->have_last = 1; }
        t->dirty = 0;
    }
}
static CObj *tr_args(Tracker *t, int cls)
{
    TArgs *a = (TArgs *)comp_new(cls, t->o.comp); if (!a) return NULL;
    memcpy(a->pos, t->pos, sizeof a->pos); a->scale = t->scale; memcpy(a->rest, t->rest, sizeof a->rest); a->rscale = t->rscale; memcpy(a->vel, t->vel, sizeof a->vel); a->reqid = t->reqid;
    return &a->o;
}
/* IInteractionTrackerOwner: IInspectable, then CustomAnimationStateEntered, IdleStateEntered, InertiaStateEntered,
   InteractingStateEntered, RequestIgnored, ValuesChanged - each (sender, args) */
static void tr_call(Tracker *t, int slot, int argcls)
{
    CObj *args = tr_args(t, argcls); HRESULT hr; void *sender = comp_view(&t->o, &IID_I(IInteractionTracker)); static LONG nlog;
    if (!args) return;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))(*(void ***)t->owner)[slot])(t->owner, sender, &args->views[0]);
    if (InterlockedIncrement(&nlog) <= 60 || FAILED(hr)) comp_log("tracker %p: owner callback %s -> 0x%08lX (position %.1f %.1f, scale %.2f)\n", (void *)t,
        slot == 6 ? "CustomAnimationStateEntered" : slot == 7 ? "IdleStateEntered" : slot == 8 ? "InertiaStateEntered" : "ValuesChanged", (unsigned long)hr, t->pos[0], t->pos[1], t->scale);
    comp_release(args);
}
void comp_interact_fire(CObj *comp)
{
    Compositor *c = (Compositor *)comp; Tracker *t, *q[32]; int fl[32], n = 0, i;
    for (t = c ? c->trackers : NULL; t && n < 32; t = t->next) if (t->flags) { fl[n] = t->flags; t->flags = 0; comp_addref(&t->o); q[n++] = t; }
    for (i = 0; i < n; i++) {
        t = q[i];
        if (t->owner) { IUnknown *own = t->owner; own->lpVtbl->AddRef(own);
            if (fl[i] & F_CUSTOM) tr_call(t, 6, CLS_InteractionTrackerCustomAnimationStateEnteredArgs);
            if (fl[i] & F_INERTIA) tr_call(t, 8, CLS_InteractionTrackerInertiaStateEnteredArgs);
            if (fl[i] & F_VALUES) tr_call(t, 11, CLS_InteractionTrackerValuesChangedArgs);
            if (fl[i] & F_IDLE) tr_call(t, 7, CLS_InteractionTrackerIdleStateEnteredArgs);
            own->lpVtbl->Release(own); }
        comp_release(&t->o);
    }
}

/* ---- creation ---- */
static HRESULT tr_create(void *compositor, void *owner, WI(IInteractionTracker) **r)
{
    CObj *comp = compositor ? comp_obj_of_any(compositor, "InteractionTracker.Create") : NULL, *o; HRESULT hr;
    if (!r) return E_POINTER; *r = NULL;
    o = comp_new(CLS_InteractionTracker, comp); if (!o) return E_OUTOFMEMORY;
    if (owner) { ((Tracker *)o)->owner = (IUnknown *)owner; ((IUnknown *)owner)->lpVtbl->AddRef((IUnknown *)owner); }
    comp_log("InteractionTracker %p created (owner %p)\n", (void *)o, owner);
    hr = comp_qi(o, &IID_I(IInteractionTracker), (void **)r); comp_release(o); return hr;
}
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerStatics, Create)(WI(IInteractionTrackerStatics) *This, Windows_UI_Composition_ICompositor *c, WI(IInteractionTracker) **r) { (void)This; return tr_create(c, NULL, r); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerStatics, CreateWithOwner)(WI(IInteractionTrackerStatics) *This, Windows_UI_Composition_ICompositor *c, WI(IInteractionTrackerOwner) *owner, WI(IInteractionTracker) **r) { (void)This; return tr_create(c, owner, r); }

/* ---- values ---- */
#define V3OF(p) (*(V3 *)(p))
static void tr_set3(Tracker *t, float *dst, V3 v) { float old[4]; memcpy(old, t->pos, sizeof t->pos); old[3] = t->scale; dst[0] = v.X; dst[1] = v.Y; dst[2] = v.Z; tr_clamp(t); if (memcmp(old, t->pos, sizeof t->pos)) tr_touch(t); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_Position)(WI(IInteractionTracker) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TR(This)->pos); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_MinPosition)(WI(IInteractionTracker) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TR(This)->minp); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, put_MinPosition)(WI(IInteractionTracker) *This, V3 v) { tr_set3(TR(This), TR(This)->minp, v); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_MaxPosition)(WI(IInteractionTracker) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TR(This)->maxp); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, put_MaxPosition)(WI(IInteractionTracker) *This, V3 v) { tr_set3(TR(This), TR(This)->maxp, v); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_NaturalRestingPosition)(WI(IInteractionTracker) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TR(This)->rest); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_PositionVelocityInPixelsPerSecond)(WI(IInteractionTracker) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TR(This)->vel); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_Scale)(WI(IInteractionTracker) *This, float *r) { if (!r) return E_POINTER; *r = TR(This)->scale; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_MinScale)(WI(IInteractionTracker) *This, float *r) { if (!r) return E_POINTER; *r = TR(This)->mins; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, put_MinScale)(WI(IInteractionTracker) *This, float v) { Tracker *t = TR(This); float s = t->scale; t->mins = v; tr_clamp(t); if (s != t->scale) tr_touch(t); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_MaxScale)(WI(IInteractionTracker) *This, float *r) { if (!r) return E_POINTER; *r = TR(This)->maxs; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, put_MaxScale)(WI(IInteractionTracker) *This, float v) { Tracker *t = TR(This); float s = t->scale; t->maxs = v; tr_clamp(t); if (s != t->scale) tr_touch(t); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_NaturalRestingScale)(WI(IInteractionTracker) *This, float *r) { if (!r) return E_POINTER; *r = TR(This)->rscale; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_Owner)(WI(IInteractionTracker) *This, WI(IInteractionTrackerOwner) **r)
{ Tracker *t = TR(This); if (!r) return E_POINTER; if (t->owner) t->owner->lpVtbl->AddRef(t->owner); *r = (WI(IInteractionTrackerOwner) *)t->owner; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, get_InteractionSources)(WI(IInteractionTracker) *This, WI(ICompositionInteractionSourceCollection) **r)
{
    /* callers Add() a VisualInteractionSource right away - one inert collection per tracker, never NULL (the Store's ResponsiveItemsRepeaterList) */
    Tracker *t = TR(This); if (!r) return E_POINTER; *r = NULL;
    if (!t->sources) { t->sources = comp_new(CLS_CompositionInteractionSourceCollection, t->o.comp); if (!t->sources) return E_OUTOFMEMORY; }
    return comp_return_default(t->sources, (void **)r);
}

/* ---- requests ---- */
static HRESULT tr_move(Tracker *t, V3 v, int relative, INT32 *req)
{
    tr_stop_motion(t);
    if (relative) { t->pos[0] += v.X; t->pos[1] += v.Y; t->pos[2] += v.Z; } else { t->pos[0] = v.X; t->pos[1] = v.Y; t->pos[2] = v.Z; }
    tr_clamp(t); t->reqid++; if (req) *req = t->reqid;
    if (t->state != ST_IDLE) tr_enter(t, ST_IDLE); else { memcpy(t->rest, t->pos, sizeof t->rest); tr_touch(t); }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, TryUpdatePosition)(WI(IInteractionTracker) *This, V3 v, INT32 *r) { return tr_move(TR(This), v, 0, r); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, TryUpdatePositionBy)(WI(IInteractionTracker) *This, V3 v, INT32 *r) { return tr_move(TR(This), v, 1, r); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker4, TryUpdatePosition)(WI(IInteractionTracker4) *This, V3 v, WI(InteractionTrackerClampingOption) opt, INT32 *r) { (void)opt; return tr_move(TR(This), v, 0, r); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker4, TryUpdatePositionBy)(WI(IInteractionTracker4) *This, V3 v, WI(InteractionTrackerClampingOption) opt, INT32 *r) { (void)opt; return tr_move(TR(This), v, 1, r); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker5, TryUpdatePosition)(WI(IInteractionTracker5) *This, V3 v, WI(InteractionTrackerClampingOption) opt, WI(InteractionTrackerPositionUpdateOption) po, INT32 *r) { (void)opt; (void)po; return tr_move(TR(This), v, 0, r); }
static HRESULT tr_animate(Tracker *t, const WCHAR *prop, void *anim, INT32 *req)
{
    CObj *a = anim ? comp_obj_of_any(anim, "InteractionTracker animation") : NULL; HRESULT hr;
    if (req) *req = 0; if (!a) return E_INVALIDARG;
    tr_stop_motion(t);
    hr = comp_start_animation(&t->o, prop, a); if (FAILED(hr)) return hr;
    t->reqid++; if (req) *req = t->reqid;
    tr_enter(t, ST_CUSTOM);                     /* comp_interact_tick returns to Idle when the animation has ended */
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, TryUpdatePositionWithAnimation)(WI(IInteractionTracker) *This, Windows_UI_Composition_ICompositionAnimation *a, INT32 *r) { return tr_animate(TR(This), L"Position", a, r); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, TryUpdateScaleWithAnimation)(WI(IInteractionTracker) *This, Windows_UI_Composition_ICompositionAnimation *a, V3 center, INT32 *r) { (void)center; return tr_animate(TR(This), L"Scale", a, r); }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, TryUpdatePositionWithAdditionalVelocity)(WI(IInteractionTracker) *This, V3 v, INT32 *r)
{
    /* no physics: the content coasts about a quarter second's worth of the velocity and eases out there (the natural resting position) */
    Tracker *t = TR(This); int i; float old[3];
    tr_stop_motion(t);
    t->vel[0] = v.X; t->vel[1] = v.Y; t->vel[2] = v.Z; memcpy(t->ifrom, t->pos, sizeof t->ifrom); memcpy(old, t->pos, sizeof old);
    for (i = 0; i < 3; i++) t->pos[i] += t->vel[i] * 0.25f;
    tr_clamp(t); memcpy(t->ito, t->pos, sizeof t->ito); memcpy(t->rest, t->ito, sizeof t->rest); memcpy(t->pos, old, sizeof old);
    t->it0 = comp_clock(); t->reqid++; if (r) *r = t->reqid;
    tr_enter(t, ST_INERTIA); return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(IInteractionTracker, TryUpdateScale)(WI(IInteractionTracker) *This, float v, V3 center, INT32 *r)
{
    Tracker *t = TR(This); (void)center;
    comp_stop_animation(&t->o, L"Scale"); t->scale = v; tr_clamp(t); t->rscale = t->scale; t->reqid++; if (r) *r = t->reqid; tr_touch(t); return S_OK;
}

/* ---- callback arguments ---- */
#define TA(T) ((TArgs *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerValuesChangedArgs, get_Position)(WI(IInteractionTrackerValuesChangedArgs) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TA(This)->pos); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerValuesChangedArgs, get_RequestId)(WI(IInteractionTrackerValuesChangedArgs) *This, INT32 *r) { if (!r) return E_POINTER; *r = TA(This)->reqid; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerValuesChangedArgs, get_Scale)(WI(IInteractionTrackerValuesChangedArgs) *This, float *r) { if (!r) return E_POINTER; *r = TA(This)->scale; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerIdleStateEnteredArgs, get_RequestId)(WI(IInteractionTrackerIdleStateEnteredArgs) *This, INT32 *r) { if (!r) return E_POINTER; *r = TA(This)->reqid; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerCustomAnimationStateEnteredArgs, get_RequestId)(WI(IInteractionTrackerCustomAnimationStateEnteredArgs) *This, INT32 *r) { if (!r) return E_POINTER; *r = TA(This)->reqid; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerInteractingStateEnteredArgs, get_RequestId)(WI(IInteractionTrackerInteractingStateEnteredArgs) *This, INT32 *r) { if (!r) return E_POINTER; *r = TA(This)->reqid; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerRequestIgnoredArgs, get_RequestId)(WI(IInteractionTrackerRequestIgnoredArgs) *This, INT32 *r) { if (!r) return E_POINTER; *r = TA(This)->reqid; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerInertiaStateEnteredArgs, get_RequestId)(WI(IInteractionTrackerInertiaStateEnteredArgs) *This, INT32 *r) { if (!r) return E_POINTER; *r = TA(This)->reqid; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerInertiaStateEnteredArgs, get_NaturalRestingPosition)(WI(IInteractionTrackerInertiaStateEnteredArgs) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TA(This)->rest); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerInertiaStateEnteredArgs, get_NaturalRestingScale)(WI(IInteractionTrackerInertiaStateEnteredArgs) *This, float *r) { if (!r) return E_POINTER; *r = TA(This)->rscale; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IInteractionTrackerInertiaStateEnteredArgs, get_PositionVelocityInPixelsPerSecond)(WI(IInteractionTrackerInertiaStateEnteredArgs) *This, V3 *r) { if (!r) return E_POINTER; *r = V3OF(TA(This)->vel); return S_OK; }

void comp_register_impls_interact(void)
{
    static const int argcls[] = { CLS_InteractionTrackerValuesChangedArgs, CLS_InteractionTrackerIdleStateEnteredArgs, CLS_InteractionTrackerCustomAnimationStateEnteredArgs,
        CLS_InteractionTrackerInertiaStateEnteredArgs, CLS_InteractionTrackerInteractingStateEnteredArgs, CLS_InteractionTrackerRequestIgnoredArgs }; int i;
    g_impl[CLS_InteractionTracker].size = sizeof(Tracker); g_impl[CLS_InteractionTracker].init = tr_init; g_impl[CLS_InteractionTracker].destroy = tr_destroy;
    for (i = 0; i < 6; i++) g_impl[argcls[i]].size = sizeof(TArgs);
}
