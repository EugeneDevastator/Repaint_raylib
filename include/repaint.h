#ifndef REPAINT_H
#define REPAINT_H

#include "raylib.h"
#include "ui_style.h"
#include <cstdint>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SCREEN_WIDTH 1600
#define SCREEN_HEIGHT 900
#define RIGHT_PANEL_WIDTH 200
#define RIGHT_PANEL_X (SCREEN_WIDTH - RIGHT_PANEL_WIDTH)

extern int uiPanelWidth;
extern bool panelResizing;
extern bool g_panelsVisible;
#define LAYER_ENTRY_H 56
#define MAX_STROKE_PTS 65536
#define PEN_MODE_COUNT 13

typedef enum {
    csNone, csPressure, csVel, csDir, csRot, csTilt, csRelang,
    csHtilt, csVtilt, csLenpx, csAcc, csXtilt, csYtilt,
    csSTOP, csCrv, csIdir, csHVdir, csHVrot
} csParams;

typedef enum {
    eBrush, eSmudge, eDisp, eCont, eLine
} eTools;

typedef enum {
    eEraseNone, eEraseAlpha, eEraseColor
} eEraseMode;

typedef enum {
    bmGamma=0, bmLinear=1, bmEraseAlpha=2, bmEraseColor=3,
    bmScreen=4, bmDodge=5, bmLighten=6, bmDarken=7,
    bmBurn=8, bmMult=9, bmOvr=10, bmColor=11,
    bmSTOP=12
} bmBlends;

typedef enum {
    laAdd, laDel, laDup, laOp, laBm, laDrop, laMove
} eLActions;

typedef enum {
    plCFNSR
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
    float threshold;
    float feather;
    char layerName[256];
    float mat[6];        // 2×3 affine matrix (row-major: [a,b,tx, c,d,ty])
} sLayerProps;

#define MAX_BRUSH_TEX 32
#define BUILTIN_TEX_COUNT 4

typedef struct {
    int id;
    char name[64];
    int w, h;
    RenderTexture2D rt;
    Image cpuImage;
    bool dirty;
    bool builtIn;
} BrushTexture;

typedef struct {
    PackedFloat Prad_in, Prad_out;
    uint8_t crv;
    uint16_t resangle;
    uint8_t x2y, cop, pwr, sol, sol2op;
    uint16_t seed, noisex, noisey;
    uint8_t NoiseID, MaskID, pipeID, bmidx, noiseidx, preserveop;
    Color col;
} d_PackedBrush;

typedef struct {
    float radInRatio, rad_out, opacity;
    double resangle;
    float crv, x2y, cop, pwr, sol, sol2op;
    uint16_t seed, noisex, noisey;
    uint8_t NoiseID, MaskID, pipeID, bmidx, noiseidx, preserveop;
    int texId;          // -1 = no texture
    int texBlendMode;   // 0 = Mask, 1 = Thr, 2 = Mul
    float texBlendVal;  // threshold fraction or multiply strength
    int texNoisemode;   // 0 = stencil, 1 = random, 2 = constant (Qt Noisemode mapping)
    float texScale;     // texture UV multiplier
    float texFeather;
    float texThresh;
    bool useTexLumAsAlpha;
    bool texUseRGB;
    int texColorMode;   // 0 = use brush RGB, 1 = use texture RGB, 2 = multiply
    int eraseMode;      // 0 = none, 1 = alpha erase, 2 = color erase
    float perspective;  // perspective distortion (Y-axis rotation before in-plane)
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

// ── Network painter architecture ─────────────────────────────────────────
struct BrushDab  { float x, y; float srcX, srcY; d_RealBrush brush; };
struct DrawCommand { float x, y; uint32_t color; float radius; };

// ── Input pipeline architecture ─────────────────────────────────────────
struct StrokePoint {
    float x, y;
    float velocity;    // 0–1 normalized
    float pressure;    // 0–1
};

class InputFilter {
public:
    static const int RING_SIZE = 16;
    struct RawPt { float x, y; double t; };
    RawPt ring[RING_SIZE];
    int head, tail;
    float smoothedVel;

    InputFilter() : head(0), tail(0), smoothedVel(0.0f) { memset(ring, 0, sizeof(ring)); }
    void Reset() { head = tail = 0; smoothedVel = 0.0f; }
    StrokePoint Feed(float x, float y, double time);
};

struct StrokeEngine {
    // Segment chaining state
    d_Brush segBrushFrom;
    Vector2 lastDabPos;
    float lastDabRad;       // jittered radius of last placed dab
    Vector2 smudgeSrcPos;
    int dabIndex;
    bool inStroke;
    // Modulator tracking
    Vector2 prevSegPos;
    Vector2 prevSegDir;
    float prevSegLen;
    float prevVel;
    float initDir;
    bool initDirSet;
    // Spline buffer
    Vector2 splinePts[256];
    int splineCount;
    int processedCount;
    // Smooth mode buffer
    Vector2 smoothBuf[4];
    int smoothBufCount;
};

#define SMOOTH_MODE_LINEAR 0
#define SMOOTH_MODE_SMOOTH 1
#define SMOOTH_MODE_SPLINE 2

extern int g_strokeSmoothingMode;
extern float g_splineMinDist;
extern float g_splineAngleThreshold;

struct AppState;

struct ICommandBroker {
    virtual void on_input(const BrushDab& e) = 0;
    virtual void poll(AppState* state) = 0;
    virtual ~ICommandBroker() = default;
};

struct LocalBroker : ICommandBroker {
    static const int CMD_CAPACITY = 4096;

    struct QueuedDab {
        RenderTexture2D targetRT;
        float x, y;
        float srcX, srcY;
        float radInRatio, rad_out, opacity, crv, x2y, sol, sol2op, resangle;
        float cop;
        float texBlendVal;
        float texScale;
        float texFeather;
        float texThresh;
        bool useTexLumAsAlpha;
        bool texUseRGB;
        int texBlendMode, texNoisemode, texColorMode;
        Color color;
        int bmidx;
        uint16_t seed;
        int activeLayer;
        uint8_t preserveop;
        uint8_t eraseMode;
        float perspective;
    };

    QueuedDab queue[CMD_CAPACITY];
    volatile int head;
    volatile int tail;
    AppState* appState;

    LocalBroker();
    void on_input(const BrushDab& e) override;
    void poll(AppState* state) override;
};

typedef struct {
    float clipminF, clipmaxF, jitter;
} BPuserstate;

typedef struct {
    float clipminF, clipmaxF;
} BPrunstate;

typedef struct {
    Rectangle rect;
    Rectangle activeRect;
    float DsRange;
    int ActivePick;
    int orient;
    int sliderrad, Soff;
    int colorMode;  // -1 = gradStart→gradEnd gradient, 0 = hue, 1 = sat, 2 = lit
    Color gradStart, gradEnd;
    Color shade, hlite, midtone;
    bool showValue, noGradient;
    char label[48];
} DualSlider;

typedef struct {
    DualSlider slider;
    Texture2D iconTex;
    bool iconLoaded;
    int penMode;
    float outMin, outMax;
    float defClipmaxF;
    char name[48];
    char tooltip[128];
    int id;
    BPuserstate user;
    BPrunstate run;
} BParam;



typedef struct {
    Rectangle bounds;
    Vector2 strokePts[MAX_STROKE_PTS];
    Vector2 inputPts[MAX_STROKE_PTS];
    int strokeLen;
    int inputLen;
    bool wasMouseDown;
    bool debugShowStamps;
    bool rightMouseDown;
    Vector2 lastMousePos;
    bool inBounds;
    bool strokeEnded;
    int endLayer;
    ICommandBroker* broker;
    InputFilter inputFilter;
    StrokeEngine strokeEng;
    Vector2 lineLastDabPos;          // line tool dab chaining
} Viewport;

struct AppState {
    Canvas canvas;
    d_Brush currentBrush;
    int activeLayer;
    Camera2D camera;
    int mode;
    int eraseMode;
    Texture2D* layerTextures;
    RenderTexture2D* layerRTs;
    bool* texDirty;
    int texCount;
    BrushTexture brushTex[MAX_BRUSH_TEX];
    int brushTexCount;
    int activeBrushTex;  // -1 = none
    int editTexMode;     // 0 = canvas layers, 1 = edit texture
    float initialAngle;  // base angle set by gizmo arrow (degrees)
};

float PackedFloat_GetVal(PackedFloat* pf);
void PackedFloat_SetVal(PackedFloat* pf, double val);
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
void Canvas_SetLayerThreshold(Canvas* canvas, int layer, float threshold);
void Canvas_SetLayerFeather(Canvas* canvas, int layer, float feather);
void Canvas_MoveLayer(Canvas* canvas, int from, int to);
void Canvas_DuplicateLayer(Canvas* canvas, int idx);
void Layer_ApplyTransform(sLayerProps* lp, const float mat[6]);
void LayerStack_SetDirty(void);
void LayerStack_ReloadShader(void);
bool LayerStack_PresentInited(void);
Shader LayerStack_GetPresentShader(void);

void BrushBlend_Init(void);
void BrushBlend_Shutdown(void);
void BrushBlend_ApplyStamp(RenderTexture2D dstRT, d_Brush* brush,
    Texture2D brushTex, float stampX, float stampY, float srcX, float srcY);

void DualSlider_Init(DualSlider* slider);

void BParam_Init(BParam* bp, int id, const char* name, float outMin, float outMax, float outDef);
void BParam_SetIcon(BParam* bp, const char* filename);
void LoadPenIcons(void);
void UnloadPenIcons(void);
Texture2D GetPenModeIcon(int mode);
float BParam_GetValue(BParam* bp);
void BParam_SetValue(BParam* bp, float val);
void BParam_SnapRunState(BParam* bp);

Color HSLToRGB(float h, float s, float l);
void RGBToHSL(Color c, float& h, float& s, float& l);
float GetModVal(BParam* bp);
float GetModValFor(BParam* bp, float cpar);

extern float colorHue;
extern float colorSat;
extern float colorLit;

extern float g_velocity;

extern int quickPanelMouseMode;
extern bool g_colorPicking;
extern Color g_colorPickGrid[25];
extern float g_pivotCursorX, g_pivotCursorY;
extern bool g_seamlessPaint;
#define HUD_NONE 0
#define HUD_QUICK 1
#define HUD_LAYER_XFORM 2
extern int g_activeHud;

// ── QuickPanel layout constants ────────────────────────────────────────
#define QP_SLIDER_W 28
#define QP_SLIDER_H 256
#define QP_SLIDER_GAP 8
#define QP_CTRL_SZ 28
#define QP_SPACING 4



// Gizmo sub-component init/shutdown (no ImGui types)
void FilePanel_Init(void);
void FilePanel_Shutdown(void);
void FilePanel_Draw(AppState* state, Rectangle vp);
void ToolBox_Init(void);
void ToolBox_Shutdown(void);
void ToolBox_Draw(AppState* state, Rectangle vp);

extern Viewport viewport;
extern BParam bpOpacity;
extern BParam bpSize;
extern BParam bpHardness;
extern BParam bpSpacing;
extern BParam bpCurvature;
extern BParam bpScatter;
extern BParam bpCloneOpacity;
extern BParam bpQuickHue;
extern BParam bpQuickSat;
extern BParam bpQuickLit;
extern BParam bpTexScale;
extern BParam bpTexFeather;
extern BParam bpTexThresh;
extern BParam bpTexBlendVal;
extern BParam bpAngle;
extern BParam bpScaleRel;
extern BParam bpSizeMul;
extern BParam bpPower;
extern BParam bpPerspective;

extern d_StrokePars g_modPars;

void Modulators_Init(void);
void Modulators_Shutdown(void);
void Modulators_SnapRunState(void);

extern const char* PenModeNames[PEN_MODE_COUNT];
extern Texture2D g_blendModeIcon;
extern bool g_blendIconLoaded;

void LayerPanel_Draw(AppState* state);

void LeftPanel_Init(void);
void LeftPanel_Shutdown(void);
void LeftPanel_Draw(AppState* state);

void Changelog_Init(void);
void Changelog_Toggle(void);
void Changelog_Draw(void);

struct ImDrawList; // forward decl — repaint.h avoids pulling in imgui.h
struct ImVec2;
void DrawSlider(BParam* bp, int orient, float thick = 0, float len = 0);

// Draw a group of pressable radio buttons. One selected at a time.
// selected has light blue bg + dot, unselected has gray bg.
// items: null-terminated array of strings.
void DrawRadioGroup(const char* label, int* current, const char* items[], int itemCount);

void QuickPanel_DrawUI(AppState* state);
void QuickPanel_Init(void);
void QuickPanel_Shutdown(void);
void XORgizmo_DrawVisual(AppState* state);
void XORgizmo_HandleInput(AppState* state);
void Viewport_DrawDebugOverlays(Viewport* vp, AppState* state);

// Layer compositing — returns cached composited document texture
extern bool layersDirty;
RenderTexture2D* DocBlender_Composite(AppState* state);
bool GetPresentInited(void);
Shader GetPresentShader(void);

// Viewport HUD coordinator — doc + stamp + screen draw
void ViewportHUD_Draw(AppState* state);
void ViewportHUD_Shutdown(void);

Image CompositeLayersWithDither(AppState* state);
void MergeDownLayer(AppState* state, int idx);
void UnloadViewportRenderer(void);
void ReloadViewportShader(void);

void Viewport_Init(Viewport* vp, Rectangle bounds);
void Viewport_SetBounds(Viewport* vp, Rectangle bounds);
void Viewport_HandleInput(Viewport* vp, AppState* state);
void App_Init(AppState* state);
void App_Draw(AppState* state);
void App_Close(AppState* state);
bool App_IsDialogActive(void);
void UpdateUI(AppState* state);
void EnsureRTs(AppState* state);
void SyncRTFromImage(AppState* state, int layer);
void SyncImageFromRT(AppState* state, int layer);
void SyncAllImages(AppState* state);
void SyncAllRTs(AppState* state);
void SyncLayerTexture(AppState* state, int layer);

// ── .re.png file format ─────────────────────────────────────────────
bool SaveRePaint(const char* path, Canvas* canvas, AppState* state);
bool LoadRePaint(const char* path, Canvas* canvas, AppState* state);

void BrushTex_Init(AppState* state);
int  BrushTex_Add(AppState* state, const char* name, int w, int h);
void BrushTex_Delete(AppState* state, int idx);
void BrushTex_SetActive(AppState* state, int idx);
void BrushTex_SyncAll(AppState* state);
Texture2D BrushTex_GetThumb(AppState* state, int idx);

extern Texture2D g_activeBrushTex;
extern Texture2D g_defaultBrushTex;

void UserTexture_Init(void);
void UserTexture_Shutdown(void);
void UserTexture_Update(AppState* state);

// ── File operations (hooked from gizmo) ─────────────────────────────
void App_FileNew(void);
void App_FileOpen(void);
void App_FileSave(void);
void App_FileSaveAs(void);
void App_FileReload(void);
void App_FileSnap(void);

RenderTexture2D Load16BitRT(int width, int height);

#endif
