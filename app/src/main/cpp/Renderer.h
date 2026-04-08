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

    // 增加 uIsGear 开关支持着色器渲染齿轮
    void drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a = 1.0f, bool isCircle = false, float radius = 0.0f, GLuint textureId = 0, bool isGear = false);
    void drawButton(float x, float y, float w, float h, float r, float g, float b, bool active, const std::string& text = "");
    void triggerGameOver();

    void triggerExitDialog();
    void triggerReturnMenuDialog();

    // 音频控制函数
    void playSfx(int soundType);
    void playBgm(int musicMode);
    void setAudioSetting(int type, bool enabled); // 同步设置给Java层

    std::atomic<bool> pendingExitDialog_{false};
    std::atomic<bool> pendingReturnMenuDialog_{false};

    GameState lastBgmState_ = (GameState)-1;
    GameState previousState_ = GameState::MODE_SELECTION; // 记录打开设置前的状态
    int lastScore_ = 0;

    // --- 保存音乐和音效状态 ---
    bool isMusicOn_ = true;
    bool isSfxOn_ = true;

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
    GLuint playingBackgroundTextureId_;

    static constexpr float kProjectionHalfHeight = 22.f;
};

extern Renderer* gRenderer;

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H