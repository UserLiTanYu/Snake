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

    void drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a = 1.0f, bool isCircle = false, float radius = 0.0f, GLuint textureId = 0, bool isGear = false, bool isLightning = false, float rotation = 0.0f);
    void drawButton(float x, float y, float w, float h, float r, float g, float b, bool active, const std::string& text = "");
    void triggerGameOver();

    void triggerExitDialog();
    void triggerReturnMenuDialog();

    void playSfx(int soundType);
    void playBgm(int musicMode);
    void setAudioSetting(int type, bool enabled);

    int getCoins();
    void addCoins(int amount);
    bool isSkinOwned(int skinId);
    bool buySkin(int skinId, int price);
    int getEquippedSkin();
    void equipSkin(int skinId);

    std::atomic<bool> pendingExitDialog_{false};
    std::atomic<bool> pendingReturnMenuDialog_{false};

    GameState lastBgmState_ = (GameState)-1;
    GameState previousState_ = GameState::MODE_SELECTION;
    int lastScore_ = 0;
    int lastCoins_ = -1;

    bool isMusicOn_ = true;
    bool isSfxOn_ = true;

    // --- 新增：滑动视图控制参数 ---
    float storeScrollY_ = 0.0f;
    float inventoryScrollY_ = 0.0f;
    float initialTouchX_ = 0.0f;
    float initialTouchY_ = 0.0f;
    float lastTouchY_ = 0.0f;
    bool isDraggingUI_ = false;

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
    TextTexture dynamicCoinText_;

    GLuint startBackgroundTextureId_;
    GLuint gameBackgroundTextureId_;
    GLuint playingBackgroundTextureId_;

    GLuint speedTextureId_;
    GLuint shieldTextureId_;
    GLuint magnetTextureId_;

    // --- 核心修改：支持到最高 20 款皮肤 ---
    GLuint skinTex_[20];

    static constexpr float kProjectionHalfHeight = 22.f;
};

extern Renderer* gRenderer;

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H