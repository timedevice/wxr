/* comp_batch.c - CompositionScopedBatch / CompositionCommitBatch and Completed events
 * A batch completes once it has ended and every animation that started while it was open has finished or been stopped.  Events are delivered on the
 * compositor's thread at the end of a frame tick (so a window must be attached for them to fire). */
#include "comp_types.h"
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

typedef struct Batch { CObj o; int kind; int types; BYTE ended, suspended, queued; int pending; struct { void *h; INT64 tok; } hd[8]; int nh; INT64 next; } Batch;
typedef struct HandlerVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *, void *sender, void *args); } HandlerVtbl;
typedef struct Handler { const HandlerVtbl *lpVtbl; } Handler;

static void batch_init(CObj *o) { ((Batch *)o)->next = 1; }
static void batch_destroy(CObj *o) { Batch *b = (Batch *)o; int i; for (i = 0; i < b->nh; i++) ((Handler *)b->hd[i].h)->lpVtbl->Release(b->hd[i].h); }
static void stack_remove(Compositor *c, CObj *b) { int i; for (i = 0; i < c->nscoped; i++) if (c->scoped[i] == b) { memmove(&c->scoped[i], &c->scoped[i + 1], (size_t)(c->nscoped - i - 1) * sizeof(CObj *)); c->nscoped--; return; } }
static void queue(Compositor *c, Batch *b)
{
    if (b->queued) return; { const int cap = 1024; if (c->nfire >= cap) { static LONG d; if (InterlockedIncrement(&d) <= 20) comp_log("batch %p: completion dropped (cap %d)\n", (void *)b, cap); return; } }
    if (c->nfire >= 1024) { static LONG n; if (InterlockedIncrement(&n) <= 5) comp_log("batch: completion queue full, Completed event dropped\n"); return; } b->queued = 1; comp_addref(&b->o); c->fireq[c->nfire++] = &b->o; comp_invalidate(&c->o);
}

void comp_batch_attach(CObj *co, ActiveAnim *a)
{
    Compositor *c = (Compositor *)co; int i; a->nb = 0;
    for (i = 0; i < c->nscoped && a->nb < 4; i++) { Batch *b = (Batch *)c->scoped[i]; if (!b->suspended && !b->ended && (b->types & 1)) { a->batches[a->nb++] = &b->o; comp_addref(&b->o); b->pending++; } }
    if (c->commit && a->nb < 4) { Batch *b = (Batch *)c->commit; if (!b->ended) { a->batches[a->nb++] = &b->o; comp_addref(&b->o); b->pending++; } }
}
void comp_batch_detach(CObj *co, ActiveAnim *a)
{
    Compositor *c = (Compositor *)co; int i;
    for (i = 0; i < a->nb; i++) { Batch *b = (Batch *)a->batches[i]; if (--b->pending <= 0 && b->ended) queue(c, b); comp_release(&b->o); }
    a->nb = 0;
}
/* the same bookkeeping for things that are not ActiveAnims (expression bindings: typemask 4 = InfiniteAnimation batches only) */
void comp_batch_attach_list(CObj *co, CObj **batches, int *nb, int typemask)
{
    Compositor *c = (Compositor *)co; int i; *nb = 0;
    for (i = 0; i < c->nscoped && *nb < 4; i++) { Batch *b = (Batch *)c->scoped[i]; if (!b->suspended && !b->ended && (b->types & typemask)) { batches[(*nb)++] = &b->o; comp_addref(&b->o); b->pending++; } }
}
void comp_batch_detach_list(CObj *co, CObj **batches, int *nb)
{
    Compositor *c = (Compositor *)co; int i;
    for (i = 0; i < *nb; i++) { Batch *b = (Batch *)batches[i]; if (--b->pending <= 0 && b->ended) queue(c, b); comp_release(&b->o); }
    *nb = 0;
}
void comp_batch_frame(CObj *co)
{
    Compositor *c = (Compositor *)co; Batch *b = (Batch *)c->commit;
    if (!b) return;
    b->ended = 1; if (b->pending <= 0) queue(c, b); comp_release(&b->o); c->commit = NULL;
}
void comp_batch_fire(CObj *co)
{
    Compositor *c = (Compositor *)co; CObj **q; int n, i, k; extern void comp_interact_fire(CObj *);
    comp_interact_fire(co);            /* InteractionTracker owner callbacks (they may start animations and end batches) */
    n = c->nfire; if (!n) return; q =(CObj **)HeapAlloc(GetProcessHeap(), 0, (size_t)n * sizeof(CObj *)); if (!q) return; c->nfire = 0; memcpy(q, c->fireq, (size_t)n * sizeof(CObj *));   /* handlers may queue more */
    for (i = 0; i < n; i++) {
        Batch *b = (Batch *)q[i]; CObj *args; void *hs[8]; int nh = b->nh; b->queued = 0;
        for (k = 0; k < nh; k++) { hs[k] = b->hd[k].h; ((Handler *)hs[k])->lpVtbl->AddRef(hs[k]); }
        args = comp_new(CLS_CompositionBatchCompletedEventArgs, co);
        for (k = 0; k < nh; k++) {
            void *sender = &b->o.views[0], *a = args ? (void *)&args->views[0] : NULL;
            HRESULT hr = ((Handler *)hs[k])->lpVtbl->Invoke(hs[k], sender, a); if (FAILED(hr)) comp_log("batch Completed handler returned 0x%08lX\n", (unsigned long)hr);
            ((Handler *)hs[k])->lpVtbl->Release(hs[k]);
        }
        if (args) comp_release(args); comp_release(&b->o);
    }
    HeapFree(GetProcessHeap(), 0, q);
}

HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateScopedBatch)(W(ICompositor) *This, Windows_UI_Composition_CompositionBatchTypes types, W(ICompositionScopedBatch) **r)
{
    Compositor *c = (Compositor *)OBJ_OF(This); CObj *o; HRESULT hr; Batch *b; if (!r) return E_POINTER; *r = NULL;
    { const int lim = 64; if (c->nscoped >= lim) { static LONG n; if (InterlockedIncrement(&n) <= 10) comp_log("CreateScopedBatch: more than %d open batches -> E_FAIL\n", lim); return E_FAIL; } }
    o = comp_new(CLS_CompositionScopedBatch, &c->o); if (!o) return E_OUTOFMEMORY; b = (Batch *)o; b->types = (int)types;
    c->scoped[c->nscoped++] = o; comp_addref(o);                                      /* the stack holds a reference until End() */
    hr = comp_qi(o, &IID_(ICompositionScopedBatch), (void **)r); comp_release(o); return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor, GetCommitBatch)(W(ICompositor) *This, Windows_UI_Composition_CompositionBatchTypes types, W(ICompositionCommitBatch) **r)
{
    Compositor *c = (Compositor *)OBJ_OF(This); Batch *b; if (!r) return E_POINTER; *r = NULL;
    if (!c->commit) { c->commit = comp_new(CLS_CompositionCommitBatch, &c->o); if (!c->commit) return E_OUTOFMEMORY; b = (Batch *)c->commit; b->kind = 1; b->types = (int)types ? (int)types : 1; comp_invalidate(&c->o); }
    return comp_qi(c->commit, &IID_(ICompositionCommitBatch), (void **)r);
}
#define BT(T) ((Batch *)OBJ_OF(T))
#define ACTIVE(b) (!(b)->ended && !(b)->suspended)
HRESULT STDMETHODCALLTYPE CI_(ICompositionScopedBatch, get_IsActive)(W(ICompositionScopedBatch) *This, boolean *r) { *r = (boolean)ACTIVE(BT(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionScopedBatch, get_IsEnded)(W(ICompositionScopedBatch) *This, boolean *r) { *r = BT(This)->ended; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionScopedBatch, End)(W(ICompositionScopedBatch) *This)
{
    Batch *b = BT(This); Compositor *c = (Compositor *)b->o.comp; if (b->ended) return S_OK;
    b->ended = 1; if (!b->suspended) { int had = 0, i; for (i = 0; i < c->nscoped; i++) if (c->scoped[i] == &b->o) had = 1; if (had) { stack_remove(c, &b->o); comp_release(&b->o); } }
    if (b->pending <= 0) queue(c, b); return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionScopedBatch, Suspend)(W(ICompositionScopedBatch) *This)
{
    Batch *b = BT(This); Compositor *c = (Compositor *)b->o.comp; if (b->ended || b->suspended) return S_OK;
    b->suspended = 1; stack_remove(c, &b->o); comp_release(&b->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionScopedBatch, Resume)(W(ICompositionScopedBatch) *This)
{
    Batch *b = BT(This); Compositor *c = (Compositor *)b->o.comp; if (b->ended || !b->suspended) return S_OK;
    { const int lim = 64; if (c->nscoped >= lim) { static LONG n; if (InterlockedIncrement(&n) <= 10) comp_log("CreateScopedBatch: more than %d open batches -> E_FAIL\n", lim); return E_FAIL; } } b->suspended = 0; c->scoped[c->nscoped++] = &b->o; comp_addref(&b->o); return S_OK;
}
static HRESULT add_handler(Batch *b, void *h, Windows_Foundation_EventRegistrationToken *tok)
{
    if (!h || !tok) return E_POINTER; if (b->nh >= 8) return E_FAIL;
    ((Handler *)h)->lpVtbl->AddRef(h); b->hd[b->nh].h = h; b->hd[b->nh].tok = b->next; tok->Value = b->next++; b->nh++; return S_OK;
}
static HRESULT remove_handler(Batch *b, Windows_Foundation_EventRegistrationToken tok)
{
    int i; for (i = 0; i < b->nh; i++) if (b->hd[i].tok == tok.Value) { ((Handler *)b->hd[i].h)->lpVtbl->Release(b->hd[i].h); memmove(&b->hd[i], &b->hd[i + 1], (size_t)(b->nh - i - 1) * sizeof b->hd[0]); b->nh--; break; }
    return S_OK;
}
#define EVT_H Windows_Foundation_TypedEventHandler_2__Object__Windows_UI_Composition_CompositionBatchCompletedEventArgs
HRESULT STDMETHODCALLTYPE CI_(ICompositionScopedBatch, add_Completed)(W(ICompositionScopedBatch) *This, EVT_H *h, Windows_Foundation_EventRegistrationToken *r) { return add_handler(BT(This), h, r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionScopedBatch, remove_Completed)(W(ICompositionScopedBatch) *This, Windows_Foundation_EventRegistrationToken t) { return remove_handler(BT(This), t); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionCommitBatch, get_IsActive)(W(ICompositionCommitBatch) *This, boolean *r) { *r = (boolean)ACTIVE(BT(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionCommitBatch, get_IsEnded)(W(ICompositionCommitBatch) *This, boolean *r) { *r = BT(This)->ended; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionCommitBatch, add_Completed)(W(ICompositionCommitBatch) *This, EVT_H *h, Windows_Foundation_EventRegistrationToken *r) { return add_handler(BT(This), h, r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionCommitBatch, remove_Completed)(W(ICompositionCommitBatch) *This, Windows_Foundation_EventRegistrationToken t) { return remove_handler(BT(This), t); }

void comp_register_impls_batch(void)
{
    g_impl[CLS_CompositionScopedBatch].size = sizeof(Batch); g_impl[CLS_CompositionScopedBatch].init = batch_init; g_impl[CLS_CompositionScopedBatch].destroy = batch_destroy;
    g_impl[CLS_CompositionCommitBatch].size = sizeof(Batch); g_impl[CLS_CompositionCommitBatch].init = batch_init; g_impl[CLS_CompositionCommitBatch].destroy = batch_destroy;
}
