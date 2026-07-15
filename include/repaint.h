#ifndef REPAINT_H
#define REPAINT_H

#include "raylib.h"
#include "xform.h"
#include <cstdint>

// ── Modulator slot enum (needed by brush_draw.h for SegmentData) ──────
typedef enum {
    csNone, csPressure, csVel, csDir, csRot, csTilt, csRelang,
    csHtilt, csVtilt, csLenpx, csAcc, csXtilt, csYtilt,
    csSTOP, csCrv, csIdir, csHVdir, csHVrot
} csParams;

#include "brush_draw.h"
#include "ui_style.h"
#include "ui_rect.h"

// ── Modulator module (replaces g_modPars) ────────────────────────────────
void Modulator_Init(void);
void Modulator_Set(int slot, float val);
float Modulator_Get(int slot);
void Modulator_GetTable(ModulatorTable* out);
RootModulators Modulator_SnapRoot(void);
void Modulator_ResetStroke(void);
void Modulator_Restore(const ModulatorTable* saved);
void FillSliderMods(const UserBrushConfig& cfg, uint8_t mods[20]);
#include <cstdint>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SCREEN_WIDTH 1600
#define SCREEN_HEIGHT 900
#define RIGHT_PANEL_WIDTH 200
#define RIGHT_PANEL_X (SCREEN_WIDTH - RIGHT_PANEL_WIDTH)

extern Font g_dialogFont;
extern int uiPanelWidth;
extern bool panelResizing;
extern bool g_panelsVisible;
#define LAYER_ENTRY_H 56
#define MAX_STROKE_PTS 65536
#define PEN_MODE_COUNT 13

typedef enum {
    eBrush, eSmudge, ePolyStripe, eDistort, eContrast, eSingleStamp
} eTools;

typedef enum {
    eEraseNone, eEraseAlpha, eEraseColor
} eEraseMode;

typedef enum {
    bmGamma=0, bmLinear=1, bmOKLab=2, bmEraseAlpha=3, bmEraseColor=4,
    bmScreen=5, bmDodge=6, bmLighten=7, bmDarken=8,
    bmBurn=9, bmMult=10, bmOvr=11, bmColor=12,
    bmLuminosity=13, bmSaturation=14,
    bmLinDodge=15, bmLinLight=16,
    bmSTOP=17
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
    RectXform xform;     // 2×3 affine matrix; ww,wh = extent in world units
    bool seamless;       // use seamless merge (3x3 tile wrap) on drop
    bool instanced;      // shares RT/texture with another layer
} sLayerProps;

#include "texture_manager.h"

// Pixel dimensions come from the render texture, never stored in xform.
extern RenderTexture2D LayerStack_GetRT(int idx);
inline int GetLayerWpx(int idx) { return LayerStack_GetRT(idx).texture.width; }
inline int GetLayerHpx(int idx) { return LayerStack_GetRT(idx).texture.height; }

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
    int texTiling;
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

// Document — canvas window framing (all in pixel units).
// Default: 1 world unit = 1 pixel. Texture size (LayerStack_RenderW/H)
// and world size (ww/wh) must not be affected by texture size.
// The texture is always fitted into the world-space region defined by ww/wh.
typedef struct {
    RectXform window; // framing rectangle in document space
} Document;

enum FramingMode { FRAME_DEFAULT, FRAME_CROP };

// Helpers — explicit rounding, not hidden in a macro
static inline int DocOutPxW(const Document* d) { return (int)(fabsf(d->window.ww) + 0.5f); }
static inline int DocOutPxH(const Document* d) { return (int)(fabsf(d->window.wh) + 0.5f); }

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

#define SMOOTH_MODE_LINEAR 0
#define SMOOTH_MODE_SMOOTH 1

extern int g_strokeSmoothingMode;
extern float g_strokeThrottle;


class UndoManager;
extern UndoManager* g_undoManager;
struct AppState;
struct ReplayRecorder;
extern ReplayRecorder* g_recorder;

struct ICommandBroker {
    virtual void on_segment(const SegmentData& seg) = 0;
    virtual void poll(AppState* state) = 0;
    virtual ~ICommandBroker() = default;
};

extern ICommandBroker* g_broker;
extern ReplayRecorder* g_recorder;

typedef struct { float clipminF,clipmaxF,jitter; } BPuserstate;
typedef struct { float clipminF,clipmaxF; } BPrunstate;
typedef struct { Rectangle rect,activeRect; float DsRange; int ActivePick,orient,sliderrad,Soff,colorMode; Color gradStart,gradEnd,shade,hlite,midtone; bool showValue,noGradient; char label[48]; } DualSlider;
typedef struct { DualSlider slider; Texture2D iconTex; bool iconLoaded; int penMode; float outMin,outMax,defClipmaxF,power; char name[48],tooltip[128]; int id; BPuserstate user; BPrunstate run; } BParam;

typedef struct {
    Rectangle bounds;
    Vector2 strokePts[MAX_STROKE_PTS], inputPts[MAX_STROKE_PTS];
    int strokeLen, inputLen;
    bool wasMouseDown, debugShowStamps, rightMouseDown;
    Vector2 lastMousePos; bool inBounds, strokeEnded, spaceHeldPrev; int endLayer;
    ICommandBroker* broker; InputFilter inputFilter;
    Vector2 lineStartPos, lineLastDabPos;
    Vector2 m_distortLastDabPos;
} Viewport;

struct AppState {
    Document doc;
    UndoManager* undo;
    d_Brush currentBrush;
    int activeLayer;
    Camera2D camera;
    int mode, eraseMode;
    int framingMode;          // FRAME_DEFAULT or FRAME_CROP
    RectXform cropEntryWindow; // saved on entering crop mode (for checker reference)
    TexSlotID brushTexSlot;   // texture used as brush stamp pattern — set by Quick HUD only
    TexSlotID editTexSlot;    // texture being edited (when editTexMode==1) — set by Layer Panel only
    bool brushTexActive;
    int editTexMode;
    float initialAngle;
};

float PackedFloat_GetVal(PackedFloat* pf);
void PackedFloat_SetVal(PackedFloat* pf, double val);
float Dist2D(Vector2 pos1, Vector2 pos2);
float DirAng(float x, float y);
float RngConv(float inval, float inmin, float inmax, float outmin, float outmax);

// Document operations
Document Doc_New(int w, int h);
void app_new_document(int w, int h, Color fill);

// Canvas window matrix — pure function, maps document coords → output pixel coords
// todo - this probably can be removed because layer compositor handles layer merges gracefully already
void ComputeCanvasMatrix(const RectXform* rx, int outW, int outH, float mat[6]);

// Commit the canvas window: bake the window transform into all layers,
// reset document to identity window at the new size.
void ApplyCanvasWindow(Document* doc);

// LayerStack — all layer management lives here
#include "layerstack.h"


void DualSlider_Init(DualSlider* slider);
void BParam_Init(BParam* bp, int id, const char* name, float outMin, float outMax, float outDef);
void BParam_SetIcon(BParam* bp, const char* filename);
void LoadPenIcons(void); void UnloadPenIcons(void);
Texture2D GetPenModeIcon(int mode);
extern Texture2D penModeTex[PEN_MODE_COUNT];
float BParam_GetValue(BParam* bp);
void BParam_SetValue(BParam* bp, float val);
void BParam_SnapRunState(BParam* bp);
Color HSLToRGB(float h, float s, float l);
void RGBToHSL(Color c, float& h, float& s, float& l);

extern float colorHue, colorSat, colorLit;
extern int quickPanelMouseMode;
extern bool g_colorPicking;
extern Color g_colorPickGrid[25];
extern int g_colorPickScreenX, g_colorPickScreenY;
extern Rectangle g_colorPickVpBounds;
extern float g_pivotCursorX, g_pivotCursorY;
extern bool g_lockAspect;
extern bool g_seamlessPaint;
extern bool g_seamlessPreview;
extern bool g_pixelPerfect;
extern int g_texScaleMode;  // 0 = brush scale, 1 = global scale
extern int g_texPanelAreaY; // y-coordinate for the texture panel in the Quick HUD
#define HUD_NONE 0
#define HUD_QUICK 1
#define HUD_LAYER_XFORM 2
#define HUD_CANVAS_XFORM 3
#define HUD_NN 4
#define HUD_SD 5
#define HUD_WARP 6
extern int g_activeHud;
#include "info_text.h"

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
extern BParam bpAngle, bpScaleRel, bpSizeMul, bpPower, bpPerspective, bpFocalOffset;

void Modulators_Init(void); void Modulators_Shutdown(void); void Modulators_SnapRunState(void);

extern const char* PenModeNames[PEN_MODE_COUNT];
extern Texture2D g_blendModeIcon;
extern bool g_blendIconLoaded;

void LayerPanel_Draw(AppState* state);
void LayerPanel_UpdatePreviews(AppState* state);
void LeftPanel_Init(void); void LeftPanel_Shutdown(void);
void LeftPanel_Draw(AppState* state);

void Changelog_Init(void); void Changelog_Toggle(void); void Changelog_Draw(void);

struct ImDrawList; struct ImVec2;
void DrawSlider(BParam* bp, int orient, float thick=0, float len=0);
void DrawRadioGroup(const char* label, int* current, const char* items[], int itemCount);
bool DrawSelector(const char* label, int* current, const char* names[], int count, int columns=1, float height=0, const Texture2D* icons=nullptr);
void DrawSingleSlider(const char* label, float* val, float vmin, float vmax, const char* display_fmt);
void DrawSingleSliderInt(const char* label, int* val, int vmin, int vmax, const char* display_fmt);
extern const char* g_blendModeNames[];
extern const int   g_blendModeCount;

void QuickPanel_DrawUI(AppState* state);
void QuickPanel_Init(void); void QuickPanel_Shutdown(void);
void XORgizmo_DrawVisual(AppState* state);
void XORgizmo_HandleInput(AppState* state);
void Viewport_DrawDebugOverlays(Viewport* vp, AppState* state);

extern bool layersDirty;

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



// File format
bool SaveRePaint(const char* path, Document* doc, AppState* state);
bool LoadRePaint(const char* path, Document* doc, AppState* state);

void        BrushTex_Init(AppState* state);
TexSlotID   BrushTex_Add(AppState* state, const char* name, int w, int h);
void        BrushTex_Delete(AppState* state, TexSlotID id);
void        BrushTex_SetActive(AppState* state, TexSlotID id);
Texture2D   BrushTex_GetThumb(AppState* state, TexSlotID id);
TexSlotID   BrushTex_GetSlot(AppState* state, int userTexSlot);

extern Texture2D g_activeBrushTex, g_defaultBrushTex;

void UserTexture_Init(void); void UserTexture_Shutdown(void); void UserTexture_Update(AppState* state);

void App_FileNew(void); void App_FileOpen(void); void App_FileSave(void);
void App_FileSaveAs(void); void App_FileReload(void); void App_FileSnap(void); void App_FileExportPNG(void);

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

struct CanvasXformModule : IModule {
    AppState* state;
    explicit CanvasXformModule(AppState* s) : state(s) {}
    const char* Name() const override { return "CanvasXform"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
    void OnExit() override;
};

struct NNHudModule : IModule {
    AppState* state;
    explicit NNHudModule(AppState* s) : state(s) {}
    const char* Name() const override { return "NNHud"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
    void OnExit() override;
};

struct SDHudModule : IModule {
    AppState* state;
    explicit SDHudModule(AppState* s) : state(s) {}
    const char* Name() const override { return "SDHud"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
};

struct WarpHudModule : IModule {
    AppState* state;
    explicit WarpHudModule(AppState* s) : state(s) {}
    const char* Name() const override { return "WarpHud"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
    void DrawGUI(const DrawRect& rect) override;
    void OnExit() override;
};

struct PaintHudModule : IModule {
    AppState* state;
    explicit PaintHudModule(AppState* s) : state(s) {}
    const char* Name() const override { return "PaintHud"; }
    bool HandleInput(InputState& input, const DrawRect& rect) override;
    void DrawGL(const DrawRect& rect) override;
};

void NNHud_Shutdown(void);

extern ModuleStack g_moduleStack;

void HudSetActive(AppState* state, int newHud);

#endif
