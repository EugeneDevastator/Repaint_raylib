#ifndef SERIALIZE_H
#define SERIALIZE_H

#include "repaint.h"

size_t Stroke_Serialize(d_Stroke* st, uint8_t* buf, size_t cap);
bool Stroke_Deserialize(d_Stroke* st, uint8_t* buf, size_t len);
size_t Brush_Serialize(d_Brush* br, uint8_t* buf, size_t cap);
bool Brush_Deserialize(d_Brush* br, uint8_t* buf, size_t len);
size_t Action_Serialize(d_Action* act, uint8_t* buf, size_t cap);
bool Action_Deserialize(d_Action* act, uint8_t* buf, size_t len);
size_t Section_Serialize(d_Section* sec, uint8_t* buf, size_t cap);
bool Section_Deserialize(d_Section* sec, uint8_t* buf, size_t len);
size_t LAction_Serialize(d_LAction* la, uint8_t* buf, size_t cap);
bool LAction_Deserialize(d_LAction* la, uint8_t* buf, size_t len);
size_t LayerProps_Serialize(sLayerProps* lp, uint8_t* buf, size_t cap);
bool LayerProps_Deserialize(sLayerProps* lp, uint8_t* buf, size_t len);

#endif
