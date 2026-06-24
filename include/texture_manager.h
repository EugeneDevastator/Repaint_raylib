#ifndef TEXTURE_MANAGER_H
#define TEXTURE_MANAGER_H

#include "raylib.h"
#include <cstdint>

#define TM_BUCKETS           2
#define TM_SLOTS_PER_BUCKET  256

enum {
    TM_BUCKET_USER = 0,   // brush textures (user + built-in)
    TM_BUCKET_LAYER = 1,  // layer render textures
};

struct TexSlotID {
    uint8_t bucket;
    uint8_t slot;
    bool operator==(const TexSlotID& o) const { return bucket == o.bucket && slot == o.slot; }
    bool operator!=(const TexSlotID& o) const { return !(*this == o); }
};

static const TexSlotID TM_INVALID_SLOT = {0xFF, 0xFF};

struct TexSlot {
    RenderTexture2D rt;
    bool used;
    bool builtIn;
    char name[64];
    int w, h;
    int refCount;
    bool ownsResources;
};

// ── Core API ──
void      TM_Init(void);

// Add a new texture: creates RT + CPU image (full ownership)
TexSlotID TM_Add(uint8_t bucket, int w, int h, const char* name, bool builtIn);

// Register existing resources (layers that manage their own lifecycle)
TexSlotID TM_Register(uint8_t bucket, RenderTexture2D rt,
                      const char* name, bool builtIn, int w, int h);
void      TM_AddRef(TexSlotID id);
void      TM_Remove(TexSlotID id);
TexSlot*  TM_Get(TexSlotID id);

int       TM_Count(uint8_t bucket);
bool      TM_IsValid(TexSlotID id);
void      TM_Shutdown(void);

#endif
