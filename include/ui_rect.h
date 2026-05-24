#pragma once
#include "raylib.h"
#include <vector>
#include <memory>
#include <cstring>

struct InputState {
    bool mouseCaptured    = false;
    bool keyboardCaptured = false;

    Vector2 MousePos()          const { return GetMousePosition(); }
    bool    MousePressed(int b) const { return IsMouseButtonPressed(b); }
    bool    MouseDown(int b)    const { return IsMouseButtonDown(b); }
    bool    MouseReleased(int b)const { return IsMouseButtonReleased(b); }
    float   ScrollDelta()       const { return GetMouseWheelMove(); }
    bool    KeyPressed(int k)   const { return IsKeyPressed(k); }
};

struct DrawRect {
    float x, y, w, h;

    bool Contains(Vector2 p) const {
        return p.x >= x && p.x <= x + w
            && p.y >= y && p.y <= y + h;
    }

    Rectangle ToRaylib() const { return { x, y, w, h }; }
};

struct IModule {
    virtual ~IModule() = default;
    virtual const char* Name() const = 0;
    virtual bool HandleInput(InputState& input, const DrawRect& rect) { return false; }
    virtual void DrawGL     (const DrawRect& rect)                    {}
    virtual void DrawGUI    (const DrawRect& rect)                    {}
    virtual void OnResize   (const DrawRect& rect)                    {}
};

struct ModuleStack {
    struct Slot {
        std::unique_ptr<IModule> module;
        DrawRect                 rect;
    };

    std::vector<Slot> slots;

    void Add(std::unique_ptr<IModule> m, DrawRect rect) {
        m->OnResize(rect);
        slots.push_back({ std::move(m), rect });
    }

    void SetRect(const char* name, DrawRect rect) {
        for (auto& s : slots) {
            if (strcmp(s.module->Name(), name) == 0) {
                s.rect = rect;
                s.module->OnResize(rect);
                return;
            }
        }
    }

    IModule* Find(const char* name) {
        for (auto& s : slots)
            if (strcmp(s.module->Name(), name) == 0)
                return s.module.get();
        return nullptr;
    }

    void HandleInput(InputState& input) {
        for (auto it = slots.rbegin(); it != slots.rend(); ++it)
            if (it->module->HandleInput(input, it->rect)) break;
    }

    void DrawGL() {
        for (auto& s : slots)
            s.module->DrawGL(s.rect);
    }

    void DrawGUI() {
        for (auto& s : slots)
            s.module->DrawGUI(s.rect);
    }
};
