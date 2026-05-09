#ifndef REPAINT_H
#define REPAINT_H

#include "raylib.h"
#include "raygui.h"
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define RIGHT_PANEL_WIDTH 200
#define RIGHT_PANEL_X (SCREEN_WIDTH - RIGHT_PANEL_WIDTH)

extern int uiPanelWidth;
extern bool panelResizing;
#define LAYER_ENTRY_H 56
#define MAX_STROKE_PTS 65536
#define PEN_MODE_COUNT 13

typedef enum {
    csNone, csPressure, csVel, csDir, csRot, csTilt, csRelang,
    csHtilt, csVtilt, csLenpx, csAcc, csXtilt, csYtilt,
    csSTOP, csERASER, csLen, csCrv, csIdir, csHVdir, csHVrot, csENDPOINT
} csParams;

typedef enum {
    eBrush, eSmudge, eDisp, eCont, eSTOP, eLine, eEOE
} eTools;

typedef enum {
    bmNormal, bmPlus, bmDodge, bmScreen, bmLighten, bmBurn,
    bmMult, bmDarken, bmOvr, bmHlight, bmSlight, bmXor, bmDiff, bmExc, bmSTOP
} bmBlends;

typedef enum {
    laAdd, laDel, laDup, laOp, laBm, laVis, laDrop, laDropall,
    laProps, laSel, laMove, laTransform, laCropLayer,
    laNewCanvas, laResizeCanvas, laCropCanvas, laSTOP
} eLActions;

typedef enum {
    caNew, caResize, caCrop, caSTOP
} eCanvasActions;

typedef enum {
    plCFNSR, plRS, plSTOP
} ePipelines;

typedef struct {
    int16_t IntVal;
    uint8_t FVal;
} PackedFloat;

typedef struct {
    PackedFloat xpos;
    PackedFloat ypos;
} d_PointF;

typedef struct {
    d_PointF packpos1;
    d_PointF packpos2;
    Vector2 pos1;
    Vector2 pos2;
    Vector2 pos3;
    Vector2 pos4;
} d_Stroke;

typedef struct {
    float Pars[25];
} d_StrokePars;

typedef struct {
    float basemax, basemin, clipmax, clipmin, crv, outmax, outmin;
} cParam;

typedef struct {
    float op;
    bool visible;
    int blendmode;
    uint8_t presop;
    bool droppedup, droppeddown, locked;
    uint8_t realidx;
    char layerName[256];
} sLayerProps;

typedef struct {
    PackedFloat Prad_in, Prad_out;
    uint8_t crv;
    uint16_t resangle;
    uint8_t x2y, scale, cop, pwr, sol, sol2op;
    uint16_t seed, noisex, noisey;
    uint8_t NoiseID, MaskID, pipeID, bmidx, noiseidx, preserveop;
    Color col;
} d_PackedBrush;

typedef struct {
    float rad_in, rad_out, opacity;
    double resangle;
    float crv, x2y, scale, cop, pwr, sol, sol2op;
    uint16_t seed, noisex, noisey;
    uint8_t NoiseID, MaskID, pipeID, bmidx, noiseidx, preserveop;
    Color col;
} d_RealBrush;

typedef struct {
    d_PackedBrush Pack;
    d_RealBrush Realb;
} d_Brush;

typedef struct {
    uint8_t ToolID;
    d_Brush Brush;
    uint8_t startseed, Noisemode;
    d_Stroke Stroke;
    uint8_t layer;
} d_Action;

typedef struct {
    d_Stroke Stroke;
    d_Brush BrushFrom, Brush;
    uint8_t BrushID, NoiseID, Noisemode, ToolID, startseed, layer;
    float spacing;
    uint8_t scatter, rRadout, rRadrel, rScale, rScaleRel, rAngle;
    uint8_t rSpacing, rSpread, rOp, rSol, rSol2, rCrv, rCop, rPwr, rHue, rSat, rLit;
} d_Section;

typedef struct {
    uint8_t ActID;
    int16_t layer, layerto;
    uint8_t bm;
    float op;
    bool vis;
    Rectangle rect;
} d_LAction;

typedef struct {
    Image* layerImages;
    sLayerProps* layerProps;
    int layerCount;
    int width;
    int height;
    Color backgroundColor;
} Canvas;

typedef struct {
    Rectangle rect;
    Rectangle activeRect;
    float clipminF, clipmaxF, jitter;
    float DsRange;
    int ActivePick;
    int orient;
    int sliderrad, Soff;
    Color gradStart, gradEnd;
    Color shade, hlite, midtone;
    bool showValue, noGradient;
    char label[48];
    bool prevDown[3];
} DualSlider;

typedef struct {
    DualSlider slider;
    Texture2D iconTex;
    bool iconLoaded;
    int penMode;
    bool penEdit;
    bool penPending;
    float outMin, outMax;
    char name[48];
    int id;
} BParam;

#define DAB_QUEUE_CAPACITY 16384

typedef struct {
    float x, y;
    RenderTexture2D targetRT;
    int activeLayer;
} Dab;

typedef struct {
    Rectangle bounds;
    Vector2 strokePts[MAX_STROKE_PTS];
    int strokeLen;
    bool wasMouseDown;
    Vector2 lastDabPos;
    float strokeDabAccum;
    bool debugShowStamps;
    bool rightMouseDown;
    Vector2 lastMousePos;
    bool inBounds;
    Dab dabQueue[DAB_QUEUE_CAPACITY];
    volatile int dabHead;
    volatile int dabTail;
    bool strokeEnded;
    int endLayer;
} Viewport;

typedef struct {
    Canvas canvas;
    d_Brush currentBrush;
    int activeLayer;
    Camera2D camera;
    int mode;
    Texture2D* layerTextures;
    RenderTexture2D* layerRTs;
    bool* texDirty;
    int texCount;
} AppState;

float PackedFloat_GetVal(PackedFloat* pf);
void PackedFloat_SetVal(PackedFloat* pf, double val);
void d_PointF_SetByVector2(d_PointF* p, Vector2 src);
Vector2 d_PointF_ToVector2(d_PointF* p);
void d_Brush_SelfPack(d_Brush* brush);
void d_Brush_SelfUnpack(d_Brush* brush);
float Dist2D(Vector2 pos1, Vector2 pos2);
float AtanXY(float x, float y);
float RngConv(float inval, float inmin, float inmax, float outmin, float outmax);

Canvas Canvas_Create(int width, int height, Color bgColor);
void Canvas_Destroy(Canvas* canvas);
void Canvas_AddLayer(Canvas* canvas);
void Canvas_InsertLayer(Canvas* canvas, int idx);
void Canvas_DeleteLayer(Canvas* canvas, int index);
void Canvas_SetLayerOpacity(Canvas* canvas, int layer, float op);
void Canvas_SetLayerBlendMode(Canvas* canvas, int layer, int bm);
void Canvas_SetLayerVisible(Canvas* canvas, int layer, bool visible);
void Canvas_MoveLayer(Canvas* canvas, int from, int to);

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

void Painter_Init(void);
void Painter_Shutdown(void);

void BrushBlend_Init(void);
void BrushBlend_Shutdown(void);
void BrushBlend_ApplyStamp(RenderTexture2D dstRT, d_Brush* brush,
    float stampX, float stampY);

void DualSlider_Init(DualSlider* slider);
void DualSlider_Draw(DualSlider* slider);
void DualSlider_HandleInput(DualSlider* slider, Vector2 mousePos);

void BParam_Init(BParam* bp, int id, const char* name, float outMin, float outMax, float outDef);
void BParam_SetIcon(BParam* bp, const char* filename);
void BParam_Draw(BParam* bp);
void BParam_DrawPen(BParam* bp);
void LoadPenIcons(void);
void UnloadPenIcons(void);
float BParam_GetValue(BParam* bp);
void BParam_SetValue(BParam* bp, float val);

Color HSLToRGB(float h, float s, float l);

extern float colorHue;
extern float colorSat;
extern float colorLit;

extern bool gizmoShow;
extern int gizmoMouseMode;

extern int dragFromIdx;
extern bool dragActive;
extern Vector2 dragMouseDownPos;
extern int dragDropTarget;

extern Viewport viewport;
extern BParam bpOpacity;
extern BParam bpSize;
extern BParam bpHardness;
extern BParam bpSpacing;
extern BParam bpCurvature;

void LayerPanel_HandleInput(AppState* state, Vector2 mousePos);
void LayerPanel_Draw(AppState* state);

void Gizmo_Draw(AppState* state);
void Gizmo_HandleInput(AppState* state, Vector2 mousePos);

extern bool layersDirty;

void DrawViewport(AppState* state, Rectangle screenRect, Camera2D camera);
void UnloadViewportRenderer(void);

void Viewport_Init(Viewport* vp, Rectangle bounds);
void Viewport_SetBounds(Viewport* vp, Rectangle bounds);
void Viewport_HandleInput(Viewport* vp, AppState* state);
void Viewport_FlushDabs(Viewport* vp, AppState* state);
void Viewport_Draw(Viewport* vp, AppState* state);

void App_Init(AppState* state);
void App_Draw(AppState* state);
void App_Close(AppState* state);
void UpdateUI(AppState* state);
void EnsureRTs(AppState* state);
void SyncRTFromImage(AppState* state, int layer);
void SyncImageFromRT(AppState* state, int layer);
void SyncAllImages(AppState* state);
void SyncAllRTs(AppState* state);

#endif
