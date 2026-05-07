#ifndef REPAINT_H
#define REPAINT_H

// Use mock header for now - replace with actual raylib.h when available
#include "raylib_mock.h"
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

// Enums ported from Brushes.h
typedef enum {
    csNone,
    csPressure,
    csVel,
    csDir,
    csRot,
    csTilt,
    csRelang,
    csHtilt,
    csVtilt,
    csLenpx,
    csAcc,
    csXtilt,
    csYtilt,
    csSTOP,
    csERASER,
    csLen,
    csCrv,
    csIdir,
    csHVdir,
    csHVrot,
    csENDPOINT
} csParams;

typedef enum {
    eBrush, eSmudge, eDisp, eCont, eSTOP, eLine, eEOE
} eTools;

typedef enum {
    bmNormal,
    bmPlus,
    bmDodge,
    bmScreen,
    bmLighten,
    bmBurn,
    bmMult,
    bmDarken,
    bmOvr,
    bmHlight,
    bmSlight,
    bmXor,
    bmDiff,
    bmExc,
    bmSTOP
} bmBlends;

typedef enum {
    laAdd,
    laDel,
    laDup,
    laOp,
    laBm,
    laVis,
    laDrop,
    laDropall,
    laProps,
    laSel,
    laMove,
    laTransform,
    laCropLayer,
    laNewCanvas,
    laResizeCanvas,
    laCropCanvas,
    laSTOP
} eLActions;

typedef enum {
    caNew,
    caResize,
    caCrop,
    caSTOP
} eCanvasActions;

typedef enum {
    plCFNSR, plRS, plSTOP
} ePipelines;

// Packed float for serialization
typedef struct {
    int16_t IntVal;
    uint8_t FVal;
} PackedFloat;

// Point with packed floats
typedef struct {
    PackedFloat xpos;
    PackedFloat ypos;
} d_PointF;

// Stroke structure
typedef struct {
    d_PointF packpos1;
    d_PointF packpos2;
    Vector2 pos1;
    Vector2 pos2;
    Vector2 pos3; // for interpolation
    Vector2 pos4; // internal
} d_Stroke;

// Stroke parameters
typedef struct {
    float Pars[25];
} d_StrokePars;

// Brush parameters
typedef struct {
    float basemax;
    float basemin;
    float clipmax;
    float clipmin;
    float crv;
    float outmax;
    float outmin;
} cParam;

// Layer properties
typedef struct {
    float op;
    bool visible;
    int blendmode;
    uint8_t presop;
    bool droppedup;
    bool droppeddown;
    bool locked;
    uint8_t realidx;
    char layerName[256];
} sLayerProps;

// Packed brush
typedef struct {
    PackedFloat Prad_in;
    PackedFloat Prad_out;
    uint8_t crv;
    uint16_t resangle;
    uint8_t x2y;
    uint8_t scale;
    uint8_t cop;
    uint8_t pwr;
    uint8_t sol;
    uint8_t sol2op;
    uint16_t seed;
    uint16_t noisex;
    uint16_t noisey;
    uint8_t NoiseID;
    uint8_t MaskID;
    uint8_t pipeID;
    uint8_t bmidx;
    uint8_t noiseidx;
    uint8_t preserveop;
    Color col;
} d_PackedBrush;

// Real brush
typedef struct {
    float rad_in;
    float rad_out;
    float opacity;
    double resangle;
    float crv;
    float x2y;
    float scale;
    float cop;
    float pwr;
    float sol;
    float sol2op;
    uint16_t seed;
    uint16_t noisex;
    uint16_t noisey;
    uint8_t NoiseID;
    uint8_t MaskID;
    uint8_t pipeID;
    uint8_t bmidx;
    uint8_t noiseidx;
    uint8_t preserveop;
    Color col;
} d_RealBrush;

// Brush union
typedef struct {
    d_PackedBrush Pack;
    d_RealBrush Realb;
} d_Brush;

// Action
typedef struct {
    uint8_t ToolID;
    d_Brush Brush;
    uint8_t startseed;
    uint8_t Noisemode;
    d_Stroke Stroke;
    uint8_t layer;
} d_Action;

// Section
typedef struct {
    d_Stroke Stroke;
    d_Brush BrushFrom;
    d_Brush Brush;
    uint8_t BrushID;
    uint8_t NoiseID;
    uint8_t Noisemode;
    uint8_t ToolID;
    uint8_t startseed;
    uint8_t layer;
    float spacing;
    uint8_t scatter;
    uint8_t rRadout;
    uint8_t rRadrel;
    uint8_t rScale;
    uint8_t rScaleRel;
    uint8_t rAngle;
    uint8_t rSpacing;
    uint8_t rSpread;
    uint8_t rOp;
    uint8_t rSol;
    uint8_t rSol2;
    uint8_t rCrv;
    uint8_t rCop;
    uint8_t rPwr;
    uint8_t rHue;
    uint8_t rSat;
    uint8_t rLit;
} d_Section;

// Layer action
typedef struct {
    uint8_t ActID;
    int16_t layer;
    int16_t layerto;
    uint8_t bm;
    float op;
    bool vis;
    Rectangle rect;
} d_LAction;

// Canvas structure
typedef struct {
    Texture2D* layers;
    sLayerProps* layerProps;
    int layerCount;
    int width;
    int height;
    Color backgroundColor;
} Canvas;

// UI Rectangle element
typedef struct {
    Rectangle rect;
    Color color;
    Color hoverColor;
    bool hovered;
    bool clicked;
    char label[64];
    int id;
} UIButton;

// Slider element
typedef struct {
    Rectangle rect;
    float value;
    float minValue;
    float maxValue;
    Color color;
    Color sliderColor;
    bool dragging;
    char label[64];
    int id;
} UISlider;

// Global state
typedef struct {
    Canvas canvas;
    d_Brush currentBrush;
    int activeLayer;
    Vector2 scrollPos;
    float zoomK;
    int mode; // 0=none, 1=paint, 2=pan, 3=zoom, 4=pick
    bool leftMouseDown;
    bool rightMouseDown;
    Vector2 lastMousePos;
    Texture2D brushTexture; // current brush stamp texture
    Shader brushShader;
    RenderTexture2D canvasRenderTex;
} AppState;

// Function declarations
float PackedFloat_GetVal(PackedFloat* pf);
void PackedFloat_SetVal(PackedFloat* pf, double val);

void d_PointF_SetByVector2(d_PointF* p, Vector2 src);
Vector2 d_PointF_ToVector2(d_PointF* p);

void d_Brush_SelfPack(d_Brush* brush);
void d_Brush_SelfUnpack(d_Brush* brush);

float Dist2D(Vector2 pos1, Vector2 pos2);
float AtanXY(float x, float y);
float RngConv(float inval, float inmin, float inmax, float outmin, float outmax);

// Canvas functions
Canvas Canvas_Create(int width, int height, Color bgColor);
void Canvas_Destroy(Canvas* canvas);
void Canvas_AddLayer(Canvas* canvas);
void Canvas_DeleteLayer(Canvas* canvas, int index);
void Canvas_SetLayerOpacity(Canvas* canvas, int layer, float op);
void Canvas_SetLayerBlendMode(Canvas* canvas, int layer, int bm);
void Canvas_SetLayerVisible(Canvas* canvas, int layer, bool visible);
void Canvas_MergeAll(Canvas* canvas);
Texture2D Canvas_GetMergedTexture(Canvas* canvas);

// Brush rendering with shaders
void GenerateBrushTexture(d_Brush* brush, Texture2D* outTexture);
void DrawBrushStamp(Texture2D brushTex, Vector2 position, d_Brush* brush, Color color);
void RenderBrushStroke(Canvas* canvas, d_Stroke stroke, d_Brush brush, Shader brushShader);

// UI functions
void UIButton_Update(UIButton* btn, Vector2 mousePos, bool mousePressed);
void UIButton_Draw(UIButton* btn);
void UISlider_Update(UISlider* slider, Vector2 mousePos, bool mousePressed);
void UISlider_Draw(UISlider* slider);

// Main app functions
void App_Init(AppState* state);
void App_Update(AppState* state);
void App_Draw(AppState* state);
void App_Close(AppState* state);

#endif // REPAINT_H
