#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <string>
#include <map>

#include "Model.h"
#include "Shader.h"
#include "SnakeGame.h"

struct android_app;

struct TextTexture {
    GLuint id = 0;
    float width = 0;
    float height = 0;
};

class Renderer {
public:
    Renderer(android_app *pApp);
    virtual ~Renderer();

    void requestExitDialog() { pendingExitDialog_.store(true); }
    void handleInput();
    void render();
    void requestRestart() { pendingRestart_.store(true); }
    void requestGoToMainMenu() { pendingMainMenu_.store(true); }
    void restartGame();
    void goToMainMenu();
    GameState getGameState() { return game_.getState(); }

private:
    void initRenderer();
    void updateRenderArea();
    void createModels();
    void drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a = 1.0f, bool isCircle = false, float radius = 0.0f, GLuint textureId = 0);
    void drawButton(float x, float y, float w, float h, float r, float g, float b, bool active, const std::string& text = "");
    void triggerGameOver();

    void triggerExitDialog();
    void triggerReturnMenuDialog(); // 新增：调用 JNI 弹出返回主菜单对话框

    std::atomic<bool> pendingExitDialog_{false};
    std::atomic<bool> pendingReturnMenuDialog_{false}; // 新增：标志位

    void loadTextTextures();
    TextTexture createTextTexture(const std::string& text, int fontSize);
    GLuint loadBackgroundTexture(const std::string& fileName);

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
    std::atomic<bool> pendingMainMenu_;

    std::map<std::string, TextTexture> textTextures_;
    GLuint startBackgroundTextureId_;
    GLuint gameBackgroundTextureId_;
    GLuint playingBackgroundTextureId_; // 新增：游戏进行时的背景纹理 ID

    static constexpr float kProjectionHalfHeight = 22.f;
};

extern Renderer* gRenderer;

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H