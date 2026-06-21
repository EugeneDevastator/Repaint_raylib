#include "serialize.h"
#include <stdlib.h>
#include <string.h>

size_t Stroke_Serialize(d_Stroke* st, uint8_t* buf, size_t cap) {
    size_t off = 0;
    if (off + sizeof(d_PointF)*2 + sizeof(Vector2)*4 > cap) return 0;
    memcpy(buf + off, &st->packpos1, sizeof(d_PointF)); off += sizeof(d_PointF);
    memcpy(buf + off, &st->packpos2, sizeof(d_PointF)); off += sizeof(d_PointF);
    memcpy(buf + off, &st->pos1, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(buf + off, &st->pos2, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(buf + off, &st->pos3, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(buf + off, &st->pos4, sizeof(Vector2)); off += sizeof(Vector2);
    return off;
}

bool Stroke_Deserialize(d_Stroke* st, uint8_t* buf, size_t len) {
    size_t off = 0;
    if (off + sizeof(d_PointF)*2 + sizeof(Vector2)*4 > len) return false;
    memcpy(&st->packpos1, buf + off, sizeof(d_PointF)); off += sizeof(d_PointF);
    memcpy(&st->packpos2, buf + off, sizeof(d_PointF)); off += sizeof(d_PointF);
    memcpy(&st->pos1, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(&st->pos2, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(&st->pos3, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(&st->pos4, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    return true;
}

size_t Brush_Serialize(d_Brush* br, uint8_t* buf, size_t cap) {
    size_t off = 0;
    if (off + sizeof(d_PackedBrush) + sizeof(d_RealBrush) > cap) return 0;
    memcpy(buf + off, &br->Pack, sizeof(d_PackedBrush)); off += sizeof(d_PackedBrush);
    memcpy(buf + off, &br->Realb, sizeof(d_RealBrush)); off += sizeof(d_RealBrush);
    return off;
}

bool Brush_Deserialize(d_Brush* br, uint8_t* buf, size_t len) {
    size_t off = 0;
    if (off + sizeof(d_PackedBrush) + sizeof(d_RealBrush) > len) return false;
    memcpy(&br->Pack, buf + off, sizeof(d_PackedBrush)); off += sizeof(d_PackedBrush);
    memcpy(&br->Realb, buf + off, sizeof(d_RealBrush)); off += sizeof(d_RealBrush);
    return true;
}

size_t Action_Serialize(d_Action* act, uint8_t* buf, size_t cap) {
    size_t off = 0;
    uint32_t strokeSz = (uint32_t)Stroke_Serialize(&act->Stroke, NULL, 0);
    if (off + sizeof(uint8_t)*3 + sizeof(d_Brush) + sizeof(uint8_t) + strokeSz > cap) return 0;

    buf[off++] = act->ToolID;
    uint32_t bsz = (uint32_t)Brush_Serialize(&act->Brush, buf + off, cap - off);
    off += bsz;
    buf[off++] = act->startseed;
    buf[off++] = act->Noisemode;

    uint32_t ssz = (uint32_t)Stroke_Serialize(&act->Stroke, buf + off, cap - off);
    off += ssz;
    buf[off++] = act->layer;
    return off;
}

bool Action_Deserialize(d_Action* act, uint8_t* buf, size_t len) {
    size_t off = 0;
    if (off + 1 > len) return false;
    act->ToolID = buf[off++];

    if (off + sizeof(d_PackedBrush) + sizeof(d_RealBrush) > len) return false;
    Brush_Deserialize(&act->Brush, buf + off, len - off);
    off += sizeof(d_PackedBrush) + sizeof(d_RealBrush);

    if (off + 2 > len) return false;
    act->startseed = buf[off++];
    act->Noisemode = buf[off++];

    if (off + sizeof(d_PointF)*2 + sizeof(Vector2)*4 + 1 > len) return false;
    Stroke_Deserialize(&act->Stroke, buf + off, len - off);
    off += sizeof(d_PointF)*2 + sizeof(Vector2)*4;

    if (off + 1 > len) return false;
    act->layer = buf[off++];
    return true;
}

size_t Section_Serialize(d_Section* sec, uint8_t* buf, size_t cap) {
    size_t off = 0;
    size_t need = sizeof(d_Stroke) + sizeof(d_Brush)*2 + sizeof(uint8_t)*7 +
                  sizeof(float) + sizeof(uint8_t)*16;
    if (off + need > cap) return 0;

    uint32_t ssz = (uint32_t)Stroke_Serialize(&sec->Stroke, buf + off, cap - off);
    off += ssz;

    uint32_t bsz = (uint32_t)Brush_Serialize(&sec->BrushFrom, buf + off, cap - off);
    off += bsz;
    bsz = (uint32_t)Brush_Serialize(&sec->Brush, buf + off, cap - off);
    off += bsz;

    buf[off++] = sec->BrushID;
    buf[off++] = sec->NoiseID;
    buf[off++] = sec->Noisemode;
    buf[off++] = sec->ToolID;
    buf[off++] = sec->startseed;
    buf[off++] = sec->layer;

    memcpy(buf + off, &sec->spacing, sizeof(float)); off += sizeof(float);

    buf[off++] = sec->scatter;
    buf[off++] = sec->rRadout;
    buf[off++] = sec->rRadrel;
    buf[off++] = sec->rScale;
    buf[off++] = sec->rScaleRel;
    buf[off++] = sec->rAngle;
    buf[off++] = sec->rSpacing;
    buf[off++] = sec->rSpread;
    buf[off++] = sec->rOp;
    buf[off++] = sec->rSol;
    buf[off++] = sec->rSol2;
    buf[off++] = sec->rCrv;
    buf[off++] = sec->rCop;
    buf[off++] = sec->rPwr;
    buf[off++] = sec->rHue;
    buf[off++] = sec->rSat;
    buf[off++] = sec->rLit;
    return off;
}

bool Section_Deserialize(d_Section* sec, uint8_t* buf, size_t len) {
    size_t off = 0;
    if (off + sizeof(d_PointF)*2 + sizeof(Vector2)*4 > len) return false;
    Stroke_Deserialize(&sec->Stroke, buf + off, len - off);
    off += sizeof(d_PointF)*2 + sizeof(Vector2)*4;

    if (off + sizeof(d_PackedBrush) + sizeof(d_RealBrush) > len) return false;
    Brush_Deserialize(&sec->BrushFrom, buf + off, len - off);
    off += sizeof(d_PackedBrush) + sizeof(d_RealBrush);

    if (off + sizeof(d_PackedBrush) + sizeof(d_RealBrush) > len) return false;
    Brush_Deserialize(&sec->Brush, buf + off, len - off);
    off += sizeof(d_PackedBrush) + sizeof(d_RealBrush);

    if (off + 6 > len) return false;
    sec->BrushID = buf[off++];
    sec->NoiseID = buf[off++];
    sec->Noisemode = buf[off++];
    sec->ToolID = buf[off++];
    sec->startseed = buf[off++];
    sec->layer = buf[off++];

    if (off + sizeof(float) > len) return false;
    memcpy(&sec->spacing, buf + off, sizeof(float)); off += sizeof(float);

    if (off + 17 > len) return false;
    sec->scatter   = buf[off++];
    sec->rRadout   = buf[off++];
    sec->rRadrel   = buf[off++];
    sec->rScale    = buf[off++];
    sec->rScaleRel = buf[off++];
    sec->rAngle    = buf[off++];
    sec->rSpacing  = buf[off++];
    sec->rSpread   = buf[off++];
    sec->rOp       = buf[off++];
    sec->rSol      = buf[off++];
    sec->rSol2     = buf[off++];
    sec->rCrv      = buf[off++];
    sec->rCop      = buf[off++];
    sec->rPwr      = buf[off++];
    sec->rHue      = buf[off++];
    sec->rSat      = buf[off++];
    sec->rLit      = buf[off++];
    return true;
}

size_t LAction_Serialize(d_LAction* la, uint8_t* buf, size_t cap) {
    size_t off = 0;
    if (off + sizeof(uint8_t)*2 + sizeof(int16_t)*2 + sizeof(float) + sizeof(uint8_t) + sizeof(Rectangle) > cap)
        return 0;
    buf[off++] = la->ActID;
    memcpy(buf + off, &la->layer, sizeof(int16_t)); off += sizeof(int16_t);
    memcpy(buf + off, &la->layerto, sizeof(int16_t)); off += sizeof(int16_t);
    buf[off++] = la->bm;
    memcpy(buf + off, &la->op, sizeof(float)); off += sizeof(float);
    buf[off++] = la->vis ? 1 : 0;
    memcpy(buf + off, &la->rect, sizeof(Rectangle)); off += sizeof(Rectangle);
    return off;
}

bool LAction_Deserialize(d_LAction* la, uint8_t* buf, size_t len) {
    size_t off = 0;
    if (off + 1 > len) return false;
    la->ActID = buf[off++];
    if (off + sizeof(int16_t)*2 > len) return false;
    memcpy(&la->layer, buf + off, sizeof(int16_t)); off += sizeof(int16_t);
    memcpy(&la->layerto, buf + off, sizeof(int16_t)); off += sizeof(int16_t);
    if (off + 1 > len) return false;
    la->bm = buf[off++];
    if (off + sizeof(float) > len) return false;
    memcpy(&la->op, buf + off, sizeof(float)); off += sizeof(float);
    if (off + 1 > len) return false;
    la->vis = buf[off++] != 0;
    if (off + sizeof(Rectangle) > len) return false;
    memcpy(&la->rect, buf + off, sizeof(Rectangle)); off += sizeof(Rectangle);
    return true;
}

size_t LayerProps_Serialize(sLayerProps* lp, uint8_t* buf, size_t cap) {
    size_t off = 0;
    uint32_t nameLen = (uint32_t)strnlen(lp->layerName, sizeof(lp->layerName));
    if (off + sizeof(float) + sizeof(uint8_t)*3 + sizeof(uint32_t) + nameLen + sizeof(uint8_t)*3 + 6*sizeof(float) > cap)
        return 0;

    memcpy(buf + off, &lp->op, sizeof(float)); off += sizeof(float);
    buf[off++] = lp->visible ? 1 : 0;
    memcpy(buf + off, &lp->blendmode, sizeof(int)); off += sizeof(int);
    off -= sizeof(int);
    int bm = lp->blendmode;
    buf[off++] = (uint8_t)bm;
    buf[off++] = lp->presop;
    buf[off++] = lp->droppedup ? 1 : 0;
    buf[off++] = lp->droppeddown ? 1 : 0;
    buf[off++] = lp->locked ? 1 : 0;
    buf[off++] = lp->realidx;

    memcpy(buf + off, &nameLen, sizeof(uint32_t)); off += sizeof(uint32_t);
    if (nameLen > 0) {
        memcpy(buf + off, lp->layerName, nameLen); off += nameLen;
    }
    // mat[6] — affine transform
    memcpy(buf + off, lp->xform.mat, 6 * sizeof(float)); off += 6 * sizeof(float);
    return off;
}

bool LayerProps_Deserialize(sLayerProps* lp, uint8_t* buf, size_t len) {
    size_t off = 0;
    if (off + sizeof(float) + 7 > len) return false;
    memcpy(&lp->op, buf + off, sizeof(float)); off += sizeof(float);
    lp->visible    = buf[off++] != 0;
    lp->blendmode  = buf[off++];
    lp->presop     = buf[off++];
    lp->droppedup  = buf[off++] != 0;
    lp->droppeddown = buf[off++] != 0;
    lp->locked     = buf[off++] != 0;
    lp->realidx    = buf[off++];

    if (off + sizeof(uint32_t) > len) return false;
    uint32_t nameLen = 0;
    memcpy(&nameLen, buf + off, sizeof(uint32_t)); off += sizeof(uint32_t);
    if (nameLen >= sizeof(lp->layerName)) nameLen = sizeof(lp->layerName) - 1;
    if (nameLen > 0) {
        if (off + nameLen > len) return false;
        memcpy(lp->layerName, buf + off, nameLen); off += nameLen;
    }
    lp->layerName[nameLen] = '\0';
    // mat[6] — affine transform (may be absent in old packets)
    if (off + 6*sizeof(float) <= len) {
        memcpy(lp->xform.mat, buf + off, 6 * sizeof(float)); off += 6 * sizeof(float);
    } else {
        lp->xform.mat[0] = 1; lp->xform.mat[1] = 0; lp->xform.mat[2] = 0;
        lp->xform.mat[3] = 0; lp->xform.mat[4] = 1; lp->xform.mat[5] = 0;
    }
    return true;
}

size_t Segment_Serialize(const SegmentData& seg, uint8_t* buf, size_t cap) {
    size_t need = sizeof(Vector2)*4 + sizeof(CollapsedBrush)*2 + sizeof(uint16_t)
                + sizeof(TexSlotID) + sizeof(uint8_t)*2 + sizeof(float)*2;
    if (cap < need) return 0;
    size_t off = 0;
    memcpy(buf + off, &seg.pos1, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(buf + off, &seg.pos2, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(buf + off, &seg.ctrl0, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(buf + off, &seg.ctrl3, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(buf + off, &seg.brushFrom, sizeof(CollapsedBrush)); off += sizeof(CollapsedBrush);
    memcpy(buf + off, &seg.brush, sizeof(CollapsedBrush)); off += sizeof(CollapsedBrush);
    memcpy(buf + off, &seg.seed, sizeof(uint16_t)); off += sizeof(uint16_t);
    memcpy(buf + off, &seg.targetSlot, sizeof(TexSlotID)); off += sizeof(TexSlotID);
    buf[off++] = seg.tool;
    buf[off++] = seg.seamless;
    memcpy(buf + off, &seg.smudgeSrcX, sizeof(float)); off += sizeof(float);
    memcpy(buf + off, &seg.smudgeSrcY, sizeof(float)); off += sizeof(float);
    return off;
}

bool Segment_Deserialize(SegmentData* seg, uint8_t* buf, size_t len) {
    size_t need = sizeof(Vector2)*4 + sizeof(CollapsedBrush)*2 + sizeof(uint16_t)
                + sizeof(TexSlotID) + sizeof(uint8_t)*2 + sizeof(float)*2;
    if (len < need) return false;
    size_t off = 0;
    memcpy(&seg->pos1, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(&seg->pos2, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(&seg->ctrl0, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(&seg->ctrl3, buf + off, sizeof(Vector2)); off += sizeof(Vector2);
    memcpy(&seg->brushFrom, buf + off, sizeof(CollapsedBrush)); off += sizeof(CollapsedBrush);
    memcpy(&seg->brush, buf + off, sizeof(CollapsedBrush)); off += sizeof(CollapsedBrush);
    memcpy(&seg->seed, buf + off, sizeof(uint16_t)); off += sizeof(uint16_t);
    memcpy(&seg->targetSlot, buf + off, sizeof(TexSlotID)); off += sizeof(TexSlotID);
    seg->tool = buf[off++];
    seg->seamless = buf[off++];
    seg->pixelPerfect = 0;          // old protocol didn't have this
    seg->dabOffset = 0;
    memcpy(&seg->smudgeSrcX, buf + off, sizeof(float)); off += sizeof(float);
    memcpy(&seg->smudgeSrcY, buf + off, sizeof(float)); off += sizeof(float);
    return true;
}
