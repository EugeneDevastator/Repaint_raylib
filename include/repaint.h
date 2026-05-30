#ifndef REPAINT_H
#define REPAINT_H

#include "raylib.h"
#include "ui_style.h"
#include "ui_rect.h"
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

typedef struct { int16_t IntVal; uint8_t FVal; } PackedFloat;
typedef struct { PackedFloat xpos, ypos; } d_PointF;
typedef struct { d_PointF packpos1, packpos2; Vector2 pos1,pos2,pos3,pos4; } d_Stroke;
typedef struct { float Pars[25]; } d_StrokePars;
typedef struct { float basemax,basemin,clipmax,clipmin,crv,outmax,outmin; } cParam;

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
    int layerW, layerH;  // native resolution of this layer
} sLayerProps;

#define MAX_BRUSH_TEX 32
#define BUILTIN_TEX_COUNT 4

typedef struct {
    int id; char name[64]; int w, h;
    RenderTexture2D rt; Image cpuImage;
    bool dirty; bool builtIn;
} BrushTexture;

typedef struct {
    PackedFloat Prad_in, Prad_out;
    uint8_t crv; uint16_t resangle;
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
    int texId;
    int texBlendMode; float texBlendVal;
    int texNoisemode; float texScale, texFeather, texThresh;
    bool useTexLumAsAlpha, texUseRGB;
    int texColorMode;
    int eraseMode;
    float perspective;
    Color col;
    float userTexOriginX, userTexOriginY; // sampling center 0..1 in texture UV
    float userTexDirection;              // rotation angle for the texture handle
} d_RealBrush;

typedef struct { d_PackedBrush Pack; d_RealBrush Realb; } d_Brush;
typedef struct { uint8_t ToolID; d_Brush Brush; uint8_t startseed, Noisemode; d_Stroke Stroke; uint8_t layer; } d_Action;
typedef struct { d_Stroke Stroke; d_Brush BrushFrom, Brush; uint8_t BrushID,NoiseID,Noisemode,ToolID,startseed,layer; float spacing; uint8_t scatter,rRadout,rRadrel,rScale,rScaleRel,rAngle,rSpacing,rSpread,rOp,rSol,rSol2,rCrv,rCop,rPwr,rHue,rSat,rLit; } d_Section;
typedef struct { uint8_t ActID; int16_t layer, layerto; uint8_t bm; float op; bool vis; Rectangle rect; } d_LAction;

// Document — base resolution for viewport / export
typedef struct { int width, height; } Document;

struct BrushDab  { float x, y; float srcX, srcY; d_RealBrush brush; };
struct DrawCommand { float x, y; uint32_t color; float radius; };

struct StrokePoint { float x, y; float velocity; float pressure; };

class InputFilter {
public:
    static const int RING_SIZE = 16;
    struct RawPt { float x,y; double t; };
    RawPt ring[RING_SIZE]; int head, tail; float smoothedVel;
    InputFilter() : head(0),tail(0),smoothedVel(0) { memset(ring,0,sizeof(ring)); }
    void Reset() { head=tail=0; smoothedVel=0; }
    StrokePoint Feed(float x, float y, double time);
};

struct StrokeEngine {
    d_Brush segBrushFrom; Vector2 lastDabPos; float lastDabRad;
    Vector2 smudgeSrcPos; int dabIndex; bool inStroke;
    Vector2 prevSegPos, prevSegDir; float prevSegLen, prevVel;
    float initDir; bool initDirSet;

    // Spline buffer for Smooth mode: throttled input points used as Catmull-Rom control points
    Vector2 splinePts[256];
    int splineCount, processedCount;
    float accumDist;        // path length accumulated since last control point
    Vector2 lastInputPos;   // previous raw input position (for incremental distance)
};

#define SMOOTH_MODE_LINEAR 0
#define SMOOTH_MODE_SMOOTH 1

extern int g_strokeSmoothingMode;
extern float g_strokeThrottle;

struct AppState;

struct ICommandBroker {
    virtual void on_input(const BrushDab& e) = 0;
    virtual void poll(AppState* state) = 0;
    virtual ~ICommandBroker() = default;
};

struct LocalBroker : ICommandBroker {
    static const int CMD_CAPACITY = 4096;
    struct QueuedDab {
        RenderTexture2D targetRT; float x,y,srcX,srcY;
        float radInRatio,rad_out,opacity,crv,x2y,sol,sol2op,resangle,cop;
        float texBlendVal,texScale,texFeather,texThresh;
        bool useTexLumAsAlpha,texUseRGB;
        int texBlendMode,texNoisemode,texColorMode;
        Color color; int bmidx; uint16_t seed;
        int activeLayer; uint8_t preserveop,eraseMode; float perspective;
        float userTexOriginX, userTexOriginY, userTexDirection;
    };
    QueuedDab queue[CMD_CAPACITY]; volatile int head,tail; AppState* appState;
    LocalBroker();
    void on_input(const BrushDab& e) override;
    void poll(AppState* state) override;
};

typedef struct { float clipminF,clipmaxF,jitter; } BPuserstate;
typedef struct { float clipminF,clipmaxF; } BPrunstate;
typedef struct { Rectangle rect,activeRect; float DsRange; int ActivePick,orient,sliderrad,Soff,colorMode; Color gradStart,gradEnd,shade,hlite,midtone; bool showValue,noGradient; char label[48]; } DualSlider;
typedef struct { DualSlider slider; Texture2D iconTex; bool iconLoaded; int penMode; float outMin,outMax,defClipmaxF; char name[48],tooltip[128]; int id; BPuserstate user; BPrunstate run; } BParam;

typedef struct {
    Rectangle bounds;
    Vector2 strokePts[MAX_STROKE_PTS], inputPts[MAX_STROKE_PTS];
    int strokeLen, inputLen;
    bool wasMouseDown, debugShowStamps, rightMouseDown;
    Vector2 lastMousePos; bool inBounds, strokeEnded; int endLayer;
    ICommandBroker* broker; InputFilter inputFilter; StrokeEngine strokeEng;
    Vector2 lineLastDabPos;
} Viewport;

struct AppState {
    Document doc;
    d_Brush currentBrush;
    int activeLayer;
    Camera2D camera;
    int mode, eraseMode;
    BrushTexture brushTex[MAX_BRUSH_TEX];
    int brushTexCount;
    int activeBrushTex;
    int editTexMode;
    float initialAngle;
};

float PackedFloat_GetVal(PackedFloat* pf);
void PackedFloat_SetVal(PackedFloat* pf, double val);
float Dist2D(Vector2 pos1, Vector2 pos2);
float AtanXY(float x, float y);
float RngConv(float inval, float inmin, float inmax, float outmin, float outmax);

// Document operations
Document Doc_New(int w, int h);

// LayerStack — all layer management lives here
#include "layerstack.h"

void BrushBlend_Init(void);
void BrushBlend_Shutdown(void);
void BrushBlend_ApplyStamp(RenderTexture2D dstRT, d_Brush* brush,
    Texture2D brushTex, float stampX, float stampY, float srcX, float srcY);

void DualSlider_Init(DualSlider* slider);
void BParam_Init(BParam* bp, int id, const char* name, float outMin, float outMax, float outDef);
void BParam_SetIcon(BParam* bp, const char* filename);
void LoadPenIcons(void); void UnloadPenIcons(void);
Texture2D GetPenModeIcon(int mode);
float BParam_GetValue(BParam* bp);
void BParam_SetValue(BParam* bp, float val);
void BParam_SnapRunState(BParam* bp);
Color HSLToRGB(float h, float s, float l);
void RGBToHSL(Color c, float& h, float& s, float& l);
float GetModVal(BParam* bp);
float GetModValFor(BParam* bp, float cpar);

extern float colorHue, colorSat, colorLit;
extern float g_velocity;
extern int quickPanelMouseMode;
extern bool g_colorPicking;
extern Color g_colorPickGrid[25];
extern float g_pivotCursorX, g_pivotCursorY;
extern bool g_seamlessPaint;
extern bool g_seamlessPreview;
extern int g_texScaleMode;  // 0 = brush scale, 1 = global scale
extern int g_texPanelAreaY; // y-coordinate for the texture panel in the Quick HUD
#define HUD_NONE 0
#define HUD_QUICK 1
#define HUD_LAYER_XFORM 2
extern int g_activeHud;

#define QP_SLIDER_W 28
#define QP_SLIDER_H 256
#define QP_SLIDER_GAP 8
#define QP_CTRL_SZ 28
#define QP_SPACING 4

void FilePanel_Init(void); void FilePanel_Shutdown(void);
void FilePanel_Draw(AppState* state, Rectangle vp);
void ToolBox_Init(void); void ToolBox_Shutdown(void);
void ToolBox_Draw(AppState* state, Rectangle vp);

extern Viewport viewport;
extern BParam bpOpacity, bpSize, bpHardness, bpSpacing, bpCurvature, bpScatter;
extern BParam bpCloneOpacity, bpQuickHue, bpQuickSat, bpQuickLit;
extern BParam bpTexScale, bpTexFeather, bpTexThresh, bpTexBlendVal;
extern BParam bpAngle, bpScaleRel, bpSizeMul, bpPower, bpPerspective;

extern d_StrokePars g_modPars;

void Modulators_Init(void); void Modulators_Shutdown(void); void Modulators_SnapRunState(void);

extern const char* PenModeNames[PEN_MODE_COUNT];
extern Texture2D g_blendModeIcon;
extern bool g_blendIconLoaded;

void LayerPanel_Draw(AppState* state);
void LeftPanel_Init(void); void LeftPanel_Shutdown(void);
void LeftPanel_Draw(AppState* state);

void Changelog_Init(void); void Changelog_Toggle(void); void Changelog_Draw(void);

struct ImDrawList; struct ImVec2;
void DrawSlider(BParam* bp, int orient, float thick=0, float len=0);
void DrawRadioGroup(const char* label, int* current, const char* items[], int itemCount);

void QuickPanel_DrawUI(AppState* state);
void QuickPanel_Init(void); void QuickPanel_Shutdown(void);
void XORgizmo_DrawVisual(AppState* state);
void XORgizmo_HandleInput(AppState* state);
void Viewport_DrawDebugOverlays(Viewport* vp, AppState* state);

extern bool layersDirty;
void LayerStack_SetDirty(void);
bool LayerStack_PresentInited(void);
Shader LayerStack_GetPresentShader(void);

void ViewportHUD_Draw(AppState* state);
void ViewportHUD_Shutdown(void);

void Viewport_Init(Viewport* vp, Rectangle bounds);
void Viewport_SetBounds(Viewport* vp, Rectangle bounds);
void Viewport_HandleInput(Viewport* vp, AppState* state);
void App_Init(AppState* state);
void App_Draw(AppState* state);
void App_Close(AppState* state);
bool App_IsDialogActive(void);
void UpdateUI(AppState* state);

// Layer compositing — delegates to LayerStack
RenderTexture2D* DocBlender_Composite(AppState* state);
bool GetPresentInited(void);
Shader GetPresentShader(void);
Image CompositeLayersWithDither(AppState* state);
void MergeDownLayer(AppState* state, int idx);
void UnloadViewportRenderer(void);
void ReloadViewportShader(void);

// Legacy sync (called by viewport after strokes, delegates to LayerStack)
void SyncLayerTexture(AppState* state, int layer);

// File format
bool SaveRePaint(const char* path, Document* doc, AppState* state);
bool LoadRePaint(const char* path, Document* doc, AppState* state);

void BrushTex_Init(AppState* state);
int  BrushTex_Add(AppState* state, const char* name, int w, int h);
void BrushTex_Delete(AppState* state, int idx);
void BrushTex_SetActive(AppState* state, int idx);
void BrushTex_SyncAll(AppState* state);
Texture2D BrushTex_GetThumb(AppState* state, int idx);

extern Texture2D g_activeBrushTex, g_defaultBrushTex;

void UserTexture_Init(void); void UserTexture_Shutdown(void); void UserTexture_Update(AppState* state);

void App_FileNew(void); void App_FileOpen(void); void App_FileSave(void);
void App_FileSaveAs(void); void App_FileReload(void); void App_FileSnap(void);

RenderTexture2D Load16BitRT(int width, int height);

// ── Notifications ──────────────────────────────────────────────────────────
void DisplayInfoText(const char* text);
void ShowNotification(const char* text, float duration);

// ── Input ─────────────────────────────────────────────────────────────────
// Forwards raylib mouse state to ImGui (captures tablet/touch that raylib
// sees but GLFW callbacks might miss).
void SyncImGuiInput(void);

// ── Module-based top-level components ──────────────────────────────────────

struct LeftPanelModule : IModule {
    AppState* state;
    explicit LeftPanelModule(AppState* s) : state(s) {}
    const char* Name() const override { return "LeftPanel"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
};

struct ViewportModule : IModule {
    AppState* state;
    explicit ViewportModule(AppState* s) : state(s) {}
    const char* Name() const override { return "Viewport"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
};

struct RightPanelModule : IModule {
    AppState* state;
    explicit RightPanelModule(AppState* s) : state(s) {}
    const char* Name() const override { return "RightPanel"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
};

struct QuickHudModule : IModule {
    AppState* state;
    std::unique_ptr<IModule> gizmoChild;   // reserved for future gizmo extraction
    std::unique_ptr<IModule> texPanelChild;
    explicit QuickHudModule(AppState* s);
    const char* Name() const override { return "QuickHud"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
};

struct LayerXformModule : IModule {
    AppState* state;
    explicit LayerXformModule(AppState* s) : state(s) {}
    const char* Name() const override { return "LayerXform"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
};

extern ModuleStack g_moduleStack;

#endif
