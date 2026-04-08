#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include <jni.h>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Renderer* gRenderer = nullptr;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_example_snake_MainActivity_nativeIsAtMainMenu(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (gRenderer) {
        GameState state = gRenderer->getGameState();
        return (state == GameState::START_SCREEN || state == GameState::MODE_SELECTION);
    }
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_example_snake_MainActivity_nativeRestartGame(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (gRenderer) gRenderer->requestRestart();
}

JNIEXPORT void JNICALL
Java_com_example_snake_MainActivity_nativeGoToMainMenu(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (gRenderer) gRenderer->requestGoToMainMenu();
}
}

static const char *vertex = R"vertex(#version 300 es
in vec3 inPosition;
in vec2 inUV;
out vec2 fragUV;
uniform mat4 uProjection;
uniform vec3 uOffset;
uniform vec2 uScale;
void main() {
    fragUV = inUV;
    vec3 pos = (inPosition * vec3(uScale, 1.0)) + uOffset;
    gl_Position = uProjection * vec4(pos, 1.0);
}
)vertex";

static const char *fragment = R"fragment(#version 300 es
precision mediump float;
in vec2 fragUV;
uniform vec4 uColor;
uniform bool uIsCircle;
uniform bool uIsGear;
uniform bool uIsLightning;
uniform float uRadius;
uniform bool uUseTexture;
uniform sampler2D uTexture;
out vec4 outColor;

void main() {
    if (uUseTexture) {
        vec2 correctedUV = vec2(fragUV.x, 1.0 - fragUV.y);
        vec4 texColor = texture(uTexture, correctedUV);
        if (uColor.r > 0.99 && uColor.g > 0.99 && uColor.b > 0.99 && uColor.a > 0.99) {
            outColor = texColor;
        } else {
            outColor = vec4(uColor.rgb, uColor.a * texColor.a);
        }
        return;
    }

    vec2 p = fragUV - vec2(0.5);
    float alpha = 1.0;

    if (uIsGear) {
        float r = length(p);
        float a = atan(p.y, p.x);
        float gearRadius = 0.38 + 0.07 * smoothstep(-0.2, 0.2, cos(a * 8.0));
        float hole = smoothstep(0.12, 0.15, r);
        alpha = smoothstep(gearRadius + 0.01, gearRadius - 0.01, r) * hole;
    } else if (uIsLightning) {
        vec2 q = p * 2.0;
        float y = q.y;
        float x = -q.x; // 反轉閃電符號
        float top = max(abs(x - 0.15 + y*0.3) - 0.2, abs(y - 0.3) - 0.4);
        float bot = max(abs(x + 0.15 + y*0.3) - 0.2, abs(y + 0.3) - 0.4);
        alpha = smoothstep(0.0, -0.03, min(top, bot));
    } else if (uIsCircle) {
        alpha = smoothstep(0.5, 0.47, length(p));
    } else {
        vec2 q = abs(p) - vec2(0.5 - uRadius);
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uRadius;
        alpha = smoothstep(0.0, -0.02, dist);
    }
    float glow = 1.0 - length(p) * 1.2;
    outColor = vec4(uColor.rgb, uColor.a * alpha * (0.7 + 0.3 * glow));
}
)fragment";

Renderer::Renderer(android_app *pApp) :
        app_(pApp), display_(EGL_NO_DISPLAY), surface_(EGL_NO_SURFACE), context_(EGL_NO_CONTEXT),
        width_(0), height_(0), shaderNeedsNewProjectionMatrix_(true), game_(120.0f, 80.0f),
        joystickTiltX_(0), joystickTiltY_(0), joystickPointerId_(-1), boostPointerId_(-1),
        joyPixelX_(0), joyPixelY_(0), wasGameOver_(false), pendingRestart_(false), pendingMainMenu_(false),
        startBackgroundTextureId_(0), gameBackgroundTextureId_(0), playingBackgroundTextureId_(0) {
    gRenderer = this;
    initRenderer();
    loadTextTextures();
    startBackgroundTextureId_ = loadBackgroundTexture("images/background.png");
    gameBackgroundTextureId_ = loadBackgroundTexture("images/main.png");
    playingBackgroundTextureId_ = loadBackgroundTexture("images/background_game.png");
    lastFrameTime_ = std::chrono::steady_clock::now();
}

Renderer::~Renderer() {
    if (gRenderer == this) gRenderer = nullptr;
    if (startBackgroundTextureId_) glDeleteTextures(1, &startBackgroundTextureId_);
    if (gameBackgroundTextureId_) glDeleteTextures(1, &gameBackgroundTextureId_);
    if (playingBackgroundTextureId_) glDeleteTextures(1, &playingBackgroundTextureId_);
    for (auto& pair : textTextures_) glDeleteTextures(1, &pair.second.id);
}

GLuint Renderer::loadBackgroundTexture(const std::string& fileName) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }

    GLuint textureId = 0;
    jstring jstr = env->NewStringUTF(fileName.c_str());
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "getAssetPixels", "(Ljava/lang/String;)[I");

    jintArray pixelArray = (jintArray)env->CallObjectMethod(activityObj, method, jstr);
    if (pixelArray) {
        jint* pixels = env->GetIntArrayElements(pixelArray, nullptr);
        int texW = pixels[0];
        int texH = pixels[1];

        if (texW > 0 && texH > 0) {
            glGenTextures(1, &textureId);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[2]);
        }
        env->ReleaseIntArrayElements(pixelArray, pixels, JNI_ABORT);
    }
    env->DeleteLocalRef(jstr);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return textureId;
}

TextTexture Renderer::createTextTexture(const std::string& text, int fontSize) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }

    jstring jstr = env->NewStringUTF(text.c_str());
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "getTextPixels", "(Ljava/lang/String;I)[I");

    jintArray pixelArray = (jintArray)env->CallObjectMethod(activityObj, method, jstr, (jint)fontSize);
    jint* pixels = env->GetIntArrayElements(pixelArray, nullptr);
    int texW = pixels[0];
    int texH = pixels[1];

    TextTexture tex;
    tex.width = (float)texW / 10.0f;
    tex.height = (float)texH / 10.0f;

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[2]);

    env->ReleaseIntArrayElements(pixelArray, pixels, JNI_ABORT);
    env->DeleteLocalRef(jstr);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return tex;
}

void Renderer::loadTextTextures() {
    textTextures_["start"] = createTextTexture("开始游戏", 50);
    textTextures_["endless"] = createTextTexture("无尽模式", 40);
    textTextures_["challenge"] = createTextTexture("挑战模式", 40);
    textTextures_["more"] = createTextTexture("更多玩法", 40);

    textTextures_["music_on"] = createTextTexture("音乐: 开", 40);
    textTextures_["music_off"] = createTextTexture("音乐: 关", 40);
    textTextures_["sfx_on"] = createTextTexture("音效: 开", 40);
    textTextures_["sfx_off"] = createTextTexture("音效: 关", 40);
    textTextures_["resume"] = createTextTexture("继续游戏", 40);
    textTextures_["main_menu"] = createTextTexture("返回主界面", 40);
    textTextures_["quit"] = createTextTexture("退出游戏", 40);
    textTextures_["settings"] = createTextTexture("游戏设置", 50);
}

void Renderer::drawButton(float x, float y, float w, float h, float r, float g, float b, bool active, const std::string& label) {
    float alpha = active ? 0.8f : 0.2f;

    // --- 核心修改：减小圆角半径，变成标准的带圆角长方形 ---
    float cornerRadius = 0.15f;

    drawShape(x, y, w + 0.6f, h + 0.6f, r, g, b, 0.1f, false, cornerRadius + 0.05f);
    drawShape(x, y, w, h, r, g, b, alpha, false, cornerRadius);
    drawShape(x, y, w, h, 1.0f, 1.0f, 1.0f, alpha * 0.3f, false, cornerRadius);

    if (textTextures_.count(label)) {
        auto& tex = textTextures_[label];
        float tw = tex.width;
        float th = tex.height;
        float maxW = w * 0.75f;
        float maxH = h * 0.6f;
        if (tw > maxW || th > maxH) {
            float scale = std::min(maxW / tw, maxH / th);
            tw *= scale; th *= scale;
        }
        drawShape(x, y, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex.id);
    }
}

void Renderer::playSfx(int soundType) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "playSoundEffect", "(I)V");
    if (method) env->CallVoidMethod(activityObj, method, (jint)soundType);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

void Renderer::playBgm(int musicMode) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "playBackgroundMusic", "(I)V");
    if (method) env->CallVoidMethod(activityObj, method, (jint)musicMode);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

void Renderer::setAudioSetting(int type, bool enabled) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "setAudioSetting", "(IZ)V");
    if (method) env->CallVoidMethod(activityObj, method, (jint)type, (jboolean)enabled);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

void Renderer::triggerExitDialog() {
    JNIEnv *env;
    app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    app_->activity->vm->AttachCurrentThread(&env, nullptr);

    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "showExitDialog", "()V");

    env->CallVoidMethod(activityObj, method);

    app_->activity->vm->DetachCurrentThread();
}

void Renderer::triggerReturnMenuDialog() {
    JNIEnv *env;
    app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    app_->activity->vm->AttachCurrentThread(&env, nullptr);

    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "showReturnToMenuDialog", "()V");

    env->CallVoidMethod(activityObj, method);

    app_->activity->vm->DetachCurrentThread();
}

void Renderer::render() {
    if (pendingRestart_.load()) restartGame();
    if (pendingMainMenu_.load()) goToMainMenu();

    if (pendingExitDialog_.load()) {
        pendingExitDialog_.store(false);
        triggerExitDialog();
    }

    if (pendingReturnMenuDialog_.load()) {
        pendingReturnMenuDialog_.store(false);
        triggerReturnMenuDialog();
    }

    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastFrameTime_).count();
    lastFrameTime_ = now;
    if (deltaTime > 0.033f) deltaTime = 0.033f;

    game_.update(deltaTime);

    GameState currentState = game_.getState();
    GameState renderState = currentState;

    if (currentState == GameState::PAUSED) {
        renderState = previousState_;
    }

    if (renderState != lastBgmState_) {
        if (renderState == GameState::START_SCREEN || renderState == GameState::MODE_SELECTION) {
            playBgm(1);
        } else if (renderState == GameState::PLAYING) {
            playBgm(2);
        } else if (renderState == GameState::GAME_OVER) {
            playBgm(0);
        }
        lastBgmState_ = renderState;
    }

    if (currentState == GameState::START_SCREEN || currentState == GameState::MODE_SELECTION) {
        lastScore_ = 0;
    } else if (currentState == GameState::PLAYING) {
        int currentScore = game_.getScore();
        if (currentScore > lastScore_) {
            playSfx(1);
            lastScore_ = currentScore;
        }
    }

    if (currentState == GameState::GAME_OVER && !wasGameOver_) {
        wasGameOver_ = true;
        playSfx(3);
        triggerGameOver();
    }

    updateRenderArea();
    if (shaderNeedsNewProjectionMatrix_) {
        float projectionMatrix[16] = {0};
        Utility::buildOrthographicMatrix(projectionMatrix, kProjectionHalfHeight, (float)width_/height_, -1.f, 1.f);
        shader_->activate();
        shader_->setProjectionMatrix(projectionMatrix);
        shaderNeedsNewProjectionMatrix_ = false;
    }

    glClearColor(0.01f, 0.01f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    shader_->activate();

    float aspect = (float)width_ / height_;
    float worldHalfWidth = kProjectionHalfHeight * aspect;

    if (renderState == GameState::START_SCREEN) {
        if (startBackgroundTextureId_) {
            drawShape(0, 0, worldHalfWidth * 2.0f, kProjectionHalfHeight * 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, startBackgroundTextureId_);
        }
        drawButton(0, -10.0f, 20.0f, 8.0f, 0.0f, 1.0f, 0.7f, true, "start");
    }
    else if (renderState == GameState::MODE_SELECTION) {
        if (gameBackgroundTextureId_) {
            drawShape(0, 0, worldHalfWidth * 2.0f, kProjectionHalfHeight * 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, gameBackgroundTextureId_);
        }
        drawButton(-32.0f, -8.0f, 18.0f, 18.0f, 0.1f, 0.6f, 1.0f, true, "endless");
        drawButton(0, -8.0f, 18.0f, 18.0f, 0.7f, 0.2f, 1.0f, false, "challenge");
        drawButton(32.0f, -8.0f, 18.0f, 18.0f, 0.2f, 0.9f, 0.4f, false, "more");
    }
    else if (renderState == GameState::PLAYING) {
        const auto& snake = game_.getSnake();
        static Vector2f lastCamPos = {60.0f, 40.0f};
        if (!snake.empty()) lastCamPos = snake[0];
        float camX = lastCamPos.x, camY = lastCamPos.y;

        float w = game_.getWorldWidth(), h = game_.getWorldHeight();
        if (playingBackgroundTextureId_) {
            drawShape(w/2.0f - camX, h/2.0f - camY, w + 0.8f, h + 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, playingBackgroundTextureId_);
        }

        drawShape(w/2.0f - camX, -0.2f - camY, w + 0.8f, 0.4f, 1.0f, 0.1f, 0.1f, 1.0f);
        drawShape(w/2.0f - camX, h + 0.2f - camY, w + 0.8f, 0.4f, 1.0f, 0.1f, 0.1f, 1.0f);
        drawShape(-0.2f - camX, h/2.0f - camY, 0.4f, h + 0.8f, 1.0f, 0.1f, 0.1f, 1.0f);
        drawShape(w + 0.2f - camX, h/2.0f - camY, 0.4f, h + 0.8f, 1.0f, 0.1f, 0.1f, 1.0f);

        // --- 核心修改：渲染不同颜色的食物 ---
        for (const auto& food : game_.getFoods()) {
            if (food.isDropped) {
                drawShape(food.pos.x - camX, food.pos.y - camY, 0.8f, 0.8f, 0.0f, 1.0f, 1.0f, 1.0f, true);
            } else {
                float fr = 1.0f, fg = 1.0f, fb = 1.0f;
                switch(food.colorType) {
                    case 0: fr = 0.2f; fg = 0.5f; fb = 1.0f; break; // 蓝色
                    case 1: fr = 1.0f; fg = 0.4f; fb = 0.8f; break; // 粉色
                    case 2: fr = 1.0f; fg = 0.2f; fb = 0.2f; break; // 红色
                    case 3: fr = 0.2f; fg = 1.0f; fb = 0.2f; break; // 绿色
                    case 4: fr = 0.7f; fg = 0.2f; fb = 1.0f; break; // 紫色
                    case 5: fr = 1.0f; fg = 0.9f; fb = 0.2f; break; // 黄色
                }
                drawShape(food.pos.x - camX, food.pos.y - camY, 0.8f, 0.8f, fr, fg, fb, 1.0f, true);
            }
        }

        float scaleFactor = 1.0f + std::min(game_.getScore() * 0.02f, 2.0f);

        for (size_t i = 0; i < snake.size(); ++i) {
            float intensity = 1.0f - (static_cast<float>(i) / snake.size() * 0.5f);
            float curSize = (i==0?1.5f:1.1f) * scaleFactor;
            drawShape(snake[i].x - camX, snake[i].y - camY, curSize, curSize, 0.0f, 1.0f*intensity, 1.0f*intensity, 1.0f, true);
        }

        if (currentState == GameState::PLAYING) {
            float joyX = -worldHalfWidth + 18.0f, joyY = -kProjectionHalfHeight + 14.0f;
            joyPixelX_ = (joyX / worldHalfWidth + 1.0f) * 0.5f * (float)width_;
            joyPixelY_ = (1.0f - (joyY / kProjectionHalfHeight + 1.0f) * 0.5f) * (float)height_;
            drawShape(joyX, joyY, 13.5f, 13.5f, 1.0f, 1.0f, 1.0f, 0.15f, true);
            drawShape(joyX + joystickTiltX_*5.0f, joyY + joystickTiltY_*5.0f, 6.0f, 6.0f, 1.0f, 1.0f, 1.0f, 0.5f, true);

            float bX = worldHalfWidth - 16.0f, bY = -kProjectionHalfHeight + 14.0f;
            float boostAlpha = (boostPointerId_ != -1) ? 0.7f : 0.3f;

            drawShape(bX, bY, 11.0f, 11.0f, 1.0f, 1.0f, 1.0f, boostAlpha, true);
            drawShape(bX, bY, 6.0f, 6.0f, 1.0f, 0.8f, 0.0f, 1.0f, false, 0.0f, 0, false, true);
        }
    }

    if (currentState != GameState::PAUSED && currentState != GameState::GAME_OVER) {
        float gearX = worldHalfWidth - 4.0f;
        float gearY = kProjectionHalfHeight - 4.0f;
        drawShape(gearX, gearY, 3.5f, 3.5f, 0.8f, 0.8f, 0.8f, 0.8f, false, 0.0f, 0, true);
    }

    if (currentState == GameState::PAUSED) {
        drawShape(0, 0, worldHalfWidth * 2.0f, kProjectionHalfHeight * 2.0f, 0.0f, 0.0f, 0.0f, 0.6f);

        float boxW = 20.0f;
        float boxH = 32.0f;
        drawShape(0, -0.5f, boxW + 0.6f, boxH + 0.6f, 0.3f, 0.4f, 0.6f, 0.8f, false, 0.1f);
        drawShape(0, -0.5f, boxW, boxH, 0.1f, 0.12f, 0.18f, 0.95f, false, 0.1f);

        auto& titleTex = textTextures_["settings"];
        float titleW = 11.0f;
        float titleH = titleW * (titleTex.height / titleTex.width);
        drawShape(0, 11.0f, titleW, titleH, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, titleTex.id);

        std::string musicLabel = isMusicOn_ ? "music_on" : "music_off";
        std::string sfxLabel = isSfxOn_ ? "sfx_on" : "sfx_off";

        float btnW = 16.0f;
        float btnH = 3.6f;

        if (previousState_ == GameState::PLAYING) {
            drawButton(0, 5.5f, btnW, btnH, 0.2f, 0.6f, 1.0f, true, musicLabel);
            drawButton(0, 1.0f, btnW, btnH, 0.2f, 0.6f, 1.0f, true, sfxLabel);
            drawButton(0, -3.5f, btnW, btnH, 0.1f, 0.8f, 0.3f, true, "resume");
            drawButton(0, -8.0f, btnW, btnH, 0.8f, 0.4f, 0.1f, true, "main_menu");
            drawButton(0, -12.5f, btnW, btnH, 0.9f, 0.2f, 0.2f, true, "quit");
        } else {
            drawButton(0, 4.0f, btnW, btnH, 0.2f, 0.6f, 1.0f, true, musicLabel);
            drawButton(0, -0.5f, btnW, btnH, 0.2f, 0.6f, 1.0f, true, sfxLabel);
            drawButton(0, -5.0f, btnW, btnH, 0.1f, 0.8f, 0.3f, true, "main_menu");
            drawButton(0, -9.5f, btnW, btnH, 0.9f, 0.2f, 0.2f, true, "quit");
        }
    }

    eglSwapBuffers(display_, surface_);
}

void Renderer::drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a, bool isCircle, float radius, GLuint textureId, bool isGear, bool isLightning) {
    if (!shader_ || models_.empty()) return;
    glUniform3f(glGetUniformLocation(shader_->getProgram(), "uOffset"), x, y, 0.0f);
    glUniform2f(glGetUniformLocation(shader_->getProgram(), "uScale"), sx, sy);
    glUniform4f(glGetUniformLocation(shader_->getProgram(), "uColor"), r, g, b, a);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsCircle"), isCircle ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsGear"), isGear ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsLightning"), isLightning ? 1 : 0);
    glUniform1f(glGetUniformLocation(shader_->getProgram(), "uRadius"), radius);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uUseTexture"), textureId > 0 ? 1 : 0);

    if (textureId > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glUniform1i(glGetUniformLocation(shader_->getProgram(), "uTexture"), 0);
    }

    GLint posAttr = shader_->getPosition();
    const auto& model = models_[0];
    glVertexAttribPointer(posAttr, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), model.getVertexData());
    glEnableVertexAttribArray(posAttr);
    GLint uvAttr = shader_->getUV();
    if (uvAttr != -1) {
        glVertexAttribPointer(uvAttr, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (uint8_t*)model.getVertexData() + sizeof(Vector3));
        glEnableVertexAttribArray(uvAttr);
    }
    glDrawElements(GL_TRIANGLES, model.getIndexCount(), GL_UNSIGNED_SHORT, model.getIndexData());
}

void Renderer::handleInput() {
    gRenderer = this;
    if (width_ <= 0 || height_ <= 0) {
        if (app_->window) { width_ = ANativeWindow_getWidth(app_->window); height_ = ANativeWindow_getHeight(app_->window); }
        if (width_ <= 0) return;
    }
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) return;

    for (auto i = 0; i < inputBuffer->keyEventsCount; i++) {
        auto &keyEvent = inputBuffer->keyEvents[i];
        if (keyEvent.keyCode == AKEYCODE_BACK && keyEvent.action == AKEY_EVENT_ACTION_DOWN) {
            GameState state = game_.getState();
            if (state == GameState::START_SCREEN || state == GameState::MODE_SELECTION) {
                pendingExitDialog_.store(true);
            } else if (state == GameState::PLAYING) {
                previousState_ = state;
                game_.setState(GameState::PAUSED);
                playSfx(2);
            } else if (state == GameState::PAUSED) {
                game_.setState(previousState_);
                playSfx(2);
            }
            android_app_clear_key_events(inputBuffer);
            return;
        }
    }

    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];

        int actionMasked = motionEvent.action & AMOTION_EVENT_ACTION_MASK;
        int pointerIndex = (motionEvent.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        if (actionMasked == AMOTION_EVENT_ACTION_DOWN || actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            auto &pointer = motionEvent.pointers[pointerIndex];
            float px = GameActivityPointerAxes_getX(&pointer);
            float py = GameActivityPointerAxes_getY(&pointer);
            float aspect = (float)width_ / height_;
            float nx = (px / (float)width_ * 2.0f - 1.0f) * aspect * kProjectionHalfHeight;
            float ny = (1.0f - py / (float)height_ * 2.0f) * kProjectionHalfHeight;

            if (game_.getState() != GameState::PAUSED && game_.getState() != GameState::GAME_OVER) {
                float gearX = (kProjectionHalfHeight * aspect) - 4.0f;
                float gearY = kProjectionHalfHeight - 4.0f;
                if (abs(nx - gearX) < 3.0f && abs(ny - gearY) < 3.0f) {
                    previousState_ = game_.getState();
                    game_.setState(GameState::PAUSED);
                    playSfx(2);
                    continue;
                }
            }

            if (game_.getState() == GameState::START_SCREEN) {
                if (abs(nx) < 10.0f && abs(ny + 10.0f) < 4.0f) {
                    playSfx(2);
                    game_.setState(GameState::MODE_SELECTION);
                }
            } else if (game_.getState() == GameState::MODE_SELECTION) {
                if (abs(nx + 32.0f) < 9.0f && abs(ny + 8.0f) < 9.0f) { playSfx(2); game_.startGame(); }
                else if (abs(nx) < 9.0f && abs(ny + 8.0f) < 9.0f) { playSfx(2); game_.startGame(); }
                else if (abs(nx - 32.0f) < 9.0f && abs(ny + 8.0f) < 9.0f) { playSfx(2); game_.startGame(); }
            } else if (game_.getState() == GameState::PLAYING) {
                if (px < (float)width_ * 0.5f && joystickPointerId_ == -1) joystickPointerId_ = pointer.id;
                else if (px >= (float)width_ * 0.5f && boostPointerId_ == -1) boostPointerId_ = pointer.id;
            } else if (game_.getState() == GameState::PAUSED) {
                if (abs(nx) < 8.0f) {
                    if (previousState_ == GameState::PLAYING) {
                        if (abs(ny - 5.5f) < 1.8f) { isMusicOn_ = !isMusicOn_; setAudioSetting(1, isMusicOn_); playSfx(2); }
                        else if (abs(ny - 1.0f) < 1.8f) { isSfxOn_ = !isSfxOn_; setAudioSetting(2, isSfxOn_); playSfx(2); }
                        else if (abs(ny - (-3.5f)) < 1.8f) { game_.setState(previousState_); playSfx(2); }
                        else if (abs(ny - (-8.0f)) < 1.8f) { game_.setState(GameState::MODE_SELECTION); playSfx(2); }
                        else if (abs(ny - (-12.5f)) < 1.8f) { playSfx(2); pendingExitDialog_.store(true); }
                    } else {
                        if (abs(ny - 4.0f) < 1.8f) { isMusicOn_ = !isMusicOn_; setAudioSetting(1, isMusicOn_); playSfx(2); }
                        else if (abs(ny - (-0.5f)) < 1.8f) { isSfxOn_ = !isSfxOn_; setAudioSetting(2, isSfxOn_); playSfx(2); }
                        else if (abs(ny - (-5.0f)) < 1.8f) { game_.setState(previousState_); playSfx(2); }
                        else if (abs(ny - (-9.5f)) < 1.8f) { playSfx(2); pendingExitDialog_.store(true); }
                    }
                }
            } else if (game_.getState() == GameState::GAME_OVER) {
                requestRestart();
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_MOVE) {
            for (int p = 0; p < motionEvent.pointerCount; p++) {
                auto &pointer = motionEvent.pointers[p];
                if (pointer.id == joystickPointerId_) {
                    float dx = GameActivityPointerAxes_getX(&pointer) - joyPixelX_, dy = GameActivityPointerAxes_getY(&pointer) - joyPixelY_;
                    float dist = std::sqrt(dx*dx + dy*dy);
                    if (dist > 5.0f) {
                        game_.setRotation(std::atan2(-dy, dx));
                        float maxD = 180.0f; float clampD = std::min(dist, maxD);
                        joystickTiltX_ = (dx/dist)*(clampD/maxD); joystickTiltY_ = -(dy/dist)*(clampD/maxD);
                    }
                }
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_UP || actionMasked == AMOTION_EVENT_ACTION_POINTER_UP || actionMasked == AMOTION_EVENT_ACTION_CANCEL) {
            int32_t id = motionEvent.pointers[pointerIndex].id;
            if (id == joystickPointerId_) { joystickPointerId_ = -1; joystickTiltX_ = 0; joystickTiltY_ = 0; }
            else if (id == boostPointerId_) boostPointerId_ = -1;
        }
    }

    if (game_.getState() == GameState::PAUSED) game_.setBoosting(false);
    else game_.setBoosting(boostPointerId_ != -1);

    android_app_clear_motion_events(inputBuffer);
}

void Renderer::triggerGameOver() {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return;
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "showGameOverDialog", "(I)V");
    if (method) env->CallVoidMethod(activityObj, method, (jint)game_.getScore());
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

void Renderer::restartGame() {
    game_.reset();
    game_.startGame();
    wasGameOver_ = false; pendingRestart_.store(false);
    joystickTiltX_ = 0; joystickTiltY_ = 0; joystickPointerId_ = -1; boostPointerId_ = -1;
}

void Renderer::goToMainMenu() {
    game_.reset();
    game_.setState(GameState::MODE_SELECTION);
    wasGameOver_ = false; pendingMainMenu_.store(false);
    joystickTiltX_ = 0; joystickTiltY_ = 0; joystickPointerId_ = -1; boostPointerId_ = -1;
}

void Renderer::initRenderer() {
    constexpr EGLint attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);
    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);
    EGLSurface surface = eglCreateWindowSurface(display, supportedConfigs[0], app_->window, nullptr);
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, supportedConfigs[0], nullptr, contextAttribs);
    eglMakeCurrent(display, surface, surface, context);
    display_ = display; surface_ = surface; context_ = context;
    shader_ = std::unique_ptr<Shader>(Shader::loadShader(vertex, fragment, "inPosition", "inUV", "uProjection"));
    if (shader_) shader_->activate();
    createModels();
    updateRenderArea();
}

void Renderer::updateRenderArea() {
    EGLint width, height;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
    if (width > 0 && height > 0 && (width != width_ || height != height_)) {
        width_ = width; height_ = height;
        glViewport(0, 0, width, height);
        shaderNeedsNewProjectionMatrix_ = true;
    }
}

void Renderer::createModels() {
    std::vector<Vertex> vertices = {
            Vertex(Vector3{-0.5f, -0.5f, 0}, Vector2{0, 0}),
            Vertex(Vector3{0.5f, -0.5f, 0}, Vector2{1, 0}),
            Vertex(Vector3{0.5f, 0.5f, 0}, Vector2{1, 1}),
            Vertex(Vector3{-0.5f, 0.5f, 0}, Vector2{0, 1})
    };
    std::vector<Index> indices = {0, 1, 2, 0, 2, 3};
    models_.emplace_back(vertices, indices, nullptr);
}
