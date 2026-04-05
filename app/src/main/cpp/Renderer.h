#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>
#include <vector>
#include <chrono>

#include "Model.h"
#include "Shader.h"
#include "SnakeGame.h"

struct android_app;

class Renderer {
public:
    inline Renderer(android_app *pApp) :
            app_(pApp),
            display_(EGL_NO_DISPLAY),
            surface_(EGL_NO_SURFACE),
            context_(EGL_NO_CONTEXT),
            width_(0),
            height_(0),
            shaderNeedsNewProjectionMatrix_(true),
            game_(120.0f, 80.0f),
            joystickTiltX_(0),
            joystickTiltY_(0),
            joystickPointerId_(-1),
            boostPointerId_(-1),
            joyPixelX_(0), joyPixelY_(0) {
        initRenderer();
        lastFrameTime_ = std::chrono::steady_clock::now();
    }

    virtual ~Renderer();
    void handleInput();
    void render();

private:
    void initRenderer();
    void updateRenderArea();
    void createModels();
    void drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a = 1.0f, bool isCircle = false);

    android_app *app_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    EGLint width_;
    EGLint height_;

    bool shaderNeedsNewProjectionMatrix_;
    std::unique_ptr<Shader> shader_;
    std::vector<Model> models_;

    SnakeGame game_;
    std::chrono::steady_clock::time_point lastFrameTime_;

    float joystickTiltX_, joystickTiltY_;
    int32_t joystickPointerId_, boostPointerId_;
    float joyPixelX_, joyPixelY_; 
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H
