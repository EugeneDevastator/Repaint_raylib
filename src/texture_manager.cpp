#include "texture_manager.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>

// Load16BitRT is defined in layerstack.cpp — forward-declared here.
RenderTexture2D Load16BitRT(int width, int height);

static struct {
    TexSlot slots[TM_BUCKETS][TM_SLOTS_PER_BUCKET];
    uint8_t nextFree[TM_BUCKETS];  // hint for O(1)-amortized alloc
} TM = {0};

void TM_Init(void) {
    memset(&TM, 0, sizeof(TM));
    // nextFree starts at 0 for each bucket
}

TexSlotID TM_Add(uint8_t bucket, int w, int h, const char* name, bool builtIn) {
    if (bucket >= TM_BUCKETS) return TM_INVALID_SLOT;

    // Scan from hint
    uint8_t* hint = &TM.nextFree[bucket];
    uint8_t slot = *hint;
    for (int i = 0; i < TM_SLOTS_PER_BUCKET; i++) {
        uint8_t s = (uint8_t)((slot + i) % TM_SLOTS_PER_BUCKET);
        if (!TM.slots[bucket][s].used) {
            slot = s;
            break;
        }
    }
    if (TM.slots[bucket][slot].used) return TM_INVALID_SLOT; // full

    TexSlot* ts = &TM.slots[bucket][slot];
    memset(ts, 0, sizeof(*ts));
    ts->used = true;
    ts->builtIn = builtIn;
    ts->w = w;
    ts->h = h;
    ts->refCount = 1;
    ts->ownsResources = true;
    if (name) snprintf(ts->name, sizeof(ts->name), "%s", name);

    // Create GPU render target and initialise to transparent
    ts->rt = Load16BitRT(w, h);
    if (ts->rt.id > 0) {
        BeginTextureMode(ts->rt);
        ClearBackground(BLANK);
        EndTextureMode();
    }

    // Advance hint past this slot
    *hint = (uint8_t)((slot + 1) % TM_SLOTS_PER_BUCKET);
    return TexSlotID{bucket, slot};
}

TexSlotID TM_Register(uint8_t bucket, RenderTexture2D rt,
                      const char* name, bool builtIn, int w, int h) {
    if (bucket >= TM_BUCKETS) return TM_INVALID_SLOT;

    uint8_t* hint = &TM.nextFree[bucket];
    uint8_t slot = *hint;
    for (int i = 0; i < TM_SLOTS_PER_BUCKET; i++) {
        uint8_t s = (uint8_t)((slot + i) % TM_SLOTS_PER_BUCKET);
        if (!TM.slots[bucket][s].used) {
            slot = s;
            break;
        }
    }
    if (TM.slots[bucket][slot].used) return TM_INVALID_SLOT;

    TexSlot* ts = &TM.slots[bucket][slot];
    memset(ts, 0, sizeof(*ts));
    ts->used = true;
    ts->builtIn = builtIn;
    ts->w = w;
    ts->h = h;
    ts->rt = rt;
    ts->refCount = 1;
    ts->ownsResources = false;  // caller owns lifecycle
    if (name) snprintf(ts->name, sizeof(ts->name), "%s", name);

    *hint = (uint8_t)((slot + 1) % TM_SLOTS_PER_BUCKET);
    return TexSlotID{bucket, slot};
}

void TM_AddRef(TexSlotID id) {
    if (!TM_IsValid(id)) return;
    TexSlot* ts = &TM.slots[id.bucket][id.slot];
    if (ts->used) ts->refCount++;
}

void TM_Remove(TexSlotID id) {
    if (!TM_IsValid(id)) return;
    TexSlot* ts = &TM.slots[id.bucket][id.slot];
    if (!ts->used) return;

    ts->refCount--;
    if (ts->refCount > 0) return;  // still referenced

    if (ts->ownsResources) {
        if (ts->rt.id > 0) UnloadRenderTexture(ts->rt);
    }
    memset(ts, 0, sizeof(*ts));
}

TexSlot* TM_Get(TexSlotID id) {
    if (id.bucket >= TM_BUCKETS || id.slot >= TM_SLOTS_PER_BUCKET) return NULL;
    TexSlot* ts = &TM.slots[id.bucket][id.slot];
    return ts->used ? ts : NULL;
}

int TM_Count(uint8_t bucket) {
    if (bucket >= TM_BUCKETS) return 0;
    int c = 0;
    for (int s = 0; s < TM_SLOTS_PER_BUCKET; s++)
        if (TM.slots[bucket][s].used) c++;
    return c;
}

bool TM_IsValid(TexSlotID id) {
    return id.bucket < TM_BUCKETS && id.slot < TM_SLOTS_PER_BUCKET &&
           TM.slots[id.bucket][id.slot].used;
}

void TM_Shutdown(void) {
    for (int b = 0; b < TM_BUCKETS; b++) {
        for (int s = 0; s < TM_SLOTS_PER_BUCKET; s++) {
            TexSlot* ts = &TM.slots[b][s];
            if (!ts->used) continue;
            if (ts->ownsResources) {
                if (ts->rt.id > 0) UnloadRenderTexture(ts->rt);
            }
            // registered (non-owned) slots: resources freed by caller
            memset(ts, 0, sizeof(*ts));
        }
    }
}
