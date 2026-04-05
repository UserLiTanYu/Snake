#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>
#include <vector>
#include <chrono>
#include <cstdint>
#include <atomic>

#include "Model.h"
#include "Shader.h"
#include "SnakeGame.h"

struct android_app;

class Renderer {
public:
    Renderer(android_app *pApp);
    virtual ~Renderer();

    void handleInput();
    void render();
    void requestRestart() { pendingRestart_.store(true); }
    void restartGame();

private:
    void initRenderer();
    void updateRenderArea();
    void createModels();
    void drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a = 1.0f, bool isCircle = false);
    void triggerGameOver();

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
    bool wasGameOver_;
    std::atomic<bool> pendingRestart_; 
};

extern Renderer* gRenderer;

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H
