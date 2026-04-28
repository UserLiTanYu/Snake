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

JNIEXPORT jboolean JNICALL
Java_com_example_snake_MainActivity_nativeTryCloseMenuOverlay(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (gRenderer && gRenderer->getGameState() == GameState::MENU_SETTINGS) {
        gRenderer->closeMenuSettings();
        return JNI_TRUE;
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
uniform float uRotation;
void main() {
    fragUV = inUV;
    float c = cos(uRotation);
    float s = sin(uRotation);
    vec2 rotatedPos = vec2(
        inPosition.x * c - inPosition.y * s,
        inPosition.x * s + inPosition.y * c
    );
    vec3 pos = vec3(rotatedPos * uScale, inPosition.z) + uOffset;
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
uniform bool uIsStar;
uniform float uRadius;
uniform bool uUseTexture;
uniform sampler2D uTexture;
out vec4 outColor;

void main() {
    if (uUseTexture) {
        vec2 correctedUV = vec2(fragUV.x, 1.0 - fragUV.y);
        vec4 texColor = texture(uTexture, correctedUV);
        texColor = texColor.bgra;

        bool noRgbTint = uColor.r > 0.99 && uColor.g > 0.99 && uColor.b > 0.99;
        if (noRgbTint) {
            outColor = vec4(texColor.rgb, texColor.a * min(uColor.a, 1.0));
        } else {
            outColor = vec4(uColor.rgb, min(uColor.a, 1.0) * texColor.a);
        }
        return;
    }

    vec2 p = fragUV - vec2(0.5);
    float alpha = 1.0;

    if (uIsStar) {
        vec2 q = p * 2.0;
        float angle = atan(q.y, q.x) + 0.942478;
        float n = 5.0;
        float twopi = 6.2831853;
        float halfAtnt = twopi / (n * 2.0);
        float atnt = abs(mod(angle, twopi / n) - halfAtnt);
        float rad = 0.35 / cos(atnt + 0.6);
        float r = length(q);
        alpha = smoothstep(rad + 0.02, rad - 0.02, r);
    } else if (uIsGear) {
        float r = length(p);
        float a = atan(p.y, p.x);
        float gearRadius = 0.38 + 0.07 * smoothstep(-0.2, 0.2, cos(a * 8.0));
        float hole = smoothstep(0.12, 0.15, r);
        alpha = smoothstep(gearRadius + 0.01, gearRadius - 0.01, r) * hole;
    } else if (uIsLightning) {
        vec2 q = p * 2.0;
        float y = q.y;
        float x = -q.x;
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

    if (uColor.a > 1.005) {
        outColor = vec4(uColor.rgb, alpha);
    } else {
        float glow = 1.0 - length(p) * 1.2;
        outColor = vec4(uColor.rgb, uColor.a * alpha * (0.7 + 0.3 * glow));
    }
}
)fragment";

Renderer::Renderer(android_app *pApp) :
        app_(pApp), display_(EGL_NO_DISPLAY), surface_(EGL_NO_SURFACE), context_(EGL_NO_CONTEXT),
        width_(0), height_(0), game_(180.0f, 120.0f),
        joystickTiltX_(0), joystickTiltY_(0), joystickPointerId_(-1), boostPointerId_(-1),
        joyPixelX_(0), joyPixelY_(0), wasGameOver_(false), pendingRestart_(false), pendingMainMenu_(false),
        startBackgroundTextureId_(0), gameBackgroundTextureId_(0), playingBackgroundTextureId_(0),challengeBackgroundTextureId_(0),
        speedTextureId_(0), shieldTextureId_(0), magnetTextureId_(0) {

    for(int i=0; i<=19; i++) { skinTex_[i] = 0; }
    dynamicCoinText_.id = 0;
    gRenderer = this;
    initRenderer();
    loadTextTextures();
    startBackgroundTextureId_ = loadBackgroundTexture("images/background.png");
    gameBackgroundTextureId_ = loadBackgroundTexture("images/main.png");
    playingBackgroundTextureId_ = loadBackgroundTexture("images/background_game.png");
    challengeBackgroundTextureId_ = loadBackgroundTexture("images/background_challenge.png");
    speedTextureId_ = loadBackgroundTexture("images/speed.png");
    shieldTextureId_ = loadBackgroundTexture("images/shield.png");
    magnetTextureId_ = loadBackgroundTexture("images/magnet.png");

    skinTex_[8] = loadBackgroundTexture("images/skin_1.png");
    skinTex_[9] = loadBackgroundTexture("images/skin_2.png");
    skinTex_[10] = loadBackgroundTexture("images/skin_3.png");
    skinTex_[11] = loadBackgroundTexture("images/skin_4.png");
    skinTex_[12] = loadBackgroundTexture("images/skin_5.png");
    skinTex_[13] = loadBackgroundTexture("images/skin_6.png");
    skinTex_[14] = loadBackgroundTexture("images/skin_7.png");
    skinTex_[15] = loadBackgroundTexture("images/skin_8.png");
    skinTex_[16] = loadBackgroundTexture("images/skin_9.png");
    skinTex_[17] = loadBackgroundTexture("images/skin_10.png");
    skinTex_[18] = loadBackgroundTexture("images/skin_11.png");
    skinTex_[19] = loadBackgroundTexture("images/skin_12.png");
    game_.setMaxScore(GameMode::CHALLENGE_1, loadChallengeScore((int)GameMode::CHALLENGE_1));
    game_.setMaxScore(GameMode::CHALLENGE_2, loadChallengeScore((int)GameMode::CHALLENGE_2));
    game_.setMaxScore(GameMode::CHALLENGE_3, loadChallengeScore((int)GameMode::CHALLENGE_3));
    for(int m = (int)GameMode::CHALLENGE_4; m <= (int)GameMode::CHALLENGE_10; ++m) {
        game_.setMaxScore((GameMode)m, loadChallengeScore(m));
    }
    // --- [新增代码：加载 Boss 模式历史最高分] ---
    bossHighScore_ = loadChallengeScore((int)GameMode::BOSS_RAID);
    // --- [新增结束] ---
    lastFrameTime_ = std::chrono::steady_clock::now();
}

Renderer::~Renderer() {
    if (gRenderer == this) gRenderer = nullptr;
    clearRankPanelCache();
    releaseRankPlayerStatTextures();
    releaseHeadNameTexCache();
    if (timeProgressTex_.id) glDeleteTextures(1, &timeProgressTex_.id);
    if (challengeProgressTex_.id) glDeleteTextures(1, &challengeProgressTex_.id);
    if (startBackgroundTextureId_) glDeleteTextures(1, &startBackgroundTextureId_);
    if (gameBackgroundTextureId_) glDeleteTextures(1, &gameBackgroundTextureId_);
    if (playingBackgroundTextureId_) glDeleteTextures(1, &playingBackgroundTextureId_);
    if (challengeBackgroundTextureId_) glDeleteTextures(1, &challengeBackgroundTextureId_);
    if (speedTextureId_) glDeleteTextures(1, &speedTextureId_);
    if (shieldTextureId_) glDeleteTextures(1, &shieldTextureId_);
    if (magnetTextureId_) glDeleteTextures(1, &magnetTextureId_);
    for(int i=8; i<=19; i++) {
        if(skinTex_[i]) glDeleteTextures(1, &skinTex_[i]);
    }
    if (dynamicCoinText_.id) glDeleteTextures(1, &dynamicCoinText_.id);
    for (auto& pair : textTextures_) glDeleteTextures(1, &pair.second.id);
}

int Renderer::getCoins() {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "getCoins", "()I");
    int res = env->CallIntMethod(app_->activity->javaGameActivity, mid);
    if (nd) app_->activity->vm->DetachCurrentThread();
    return res;
}
void Renderer::addCoins(int amount) {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "addCoins", "(I)V");
    env->CallVoidMethod(app_->activity->javaGameActivity, mid, amount);
    if (nd) app_->activity->vm->DetachCurrentThread();
}
bool Renderer::isSkinOwned(int skinId) {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "isSkinOwned", "(I)Z");
    bool res = env->CallBooleanMethod(app_->activity->javaGameActivity, mid, skinId);
    if (nd) app_->activity->vm->DetachCurrentThread();
    return res;
}
bool Renderer::buySkin(int skinId, int price) {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "buySkin", "(II)Z");
    bool res = env->CallBooleanMethod(app_->activity->javaGameActivity, mid, skinId, price);
    if (nd) app_->activity->vm->DetachCurrentThread();
    return res;
}
int Renderer::getEquippedSkin() {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "getEquippedSkin", "()I");
    int res = env->CallIntMethod(app_->activity->javaGameActivity, mid);
    if (nd) app_->activity->vm->DetachCurrentThread();
    return res;
}
void Renderer::equipSkin(int skinId) {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "equipSkin", "(I)V");
    env->CallVoidMethod(app_->activity->javaGameActivity, mid, skinId);
    if (nd) app_->activity->vm->DetachCurrentThread();
}
void Renderer::saveChallengeScore(int mode, int score) {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "saveChallengeScore", "(II)V");
    if (mid) env->CallVoidMethod(app_->activity->javaGameActivity, mid, mode, score);
    if (nd) app_->activity->vm->DetachCurrentThread();
}

int Renderer::loadChallengeScore(int mode) {
    JNIEnv *env; bool nd = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) { app_->activity->vm->AttachCurrentThread(&env, nullptr); nd = true; }
    jmethodID mid = env->GetMethodID(env->GetObjectClass(app_->activity->javaGameActivity), "loadChallengeScore", "(I)I");
    int res = 0;
    if (mid) res = env->CallIntMethod(app_->activity->javaGameActivity, mid, mode);
    if (nd) app_->activity->vm->DetachCurrentThread();
    return res;
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
    tex.width = (float)texW / 40.0f;
    tex.height = (float)texH / 40.0f;

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

TextTexture Renderer::createTextTextureColored(const std::string& text, int fontSize, int argb) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }

    jstring jstr = env->NewStringUTF(text.c_str());
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "getTextPixelsColored", "(Ljava/lang/String;II)[I");

    TextTexture tex{};
    if (method && jstr) {
        jintArray pixelArray = (jintArray)env->CallObjectMethod(activityObj, method, jstr, (jint)fontSize, (jint)argb);
        if (pixelArray && !env->ExceptionCheck()) {
            jint* pixels = env->GetIntArrayElements(pixelArray, nullptr);
            if (pixels) {
                int texW = pixels[0];
                int texH = pixels[1];
                if (texW >= 1 && texH >= 1) {
                    tex.width = (float)texW / 40.0f;
                    tex.height = (float)texH / 40.0f;
                    glGenTextures(1, &tex.id);
                    glBindTexture(GL_TEXTURE_2D, tex.id);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[2]);
                }
                env->ReleaseIntArrayElements(pixelArray, pixels, JNI_ABORT);
            }
            env->DeleteLocalRef(pixelArray);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(jstr);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return tex;
}

TextTexture Renderer::createTextTextureLeft(const std::string& text, int fontSize) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }

    jstring jstr = env->NewStringUTF(text.c_str());
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "getTextPixelsLeft", "(Ljava/lang/String;I)[I");

    TextTexture tex{};
    if (method && jstr) {
        jintArray pixelArray = (jintArray)env->CallObjectMethod(activityObj, method, jstr, (jint)fontSize);
        if (pixelArray && !env->ExceptionCheck()) {
            jint* pixels = env->GetIntArrayElements(pixelArray, nullptr);
            if (pixels) {
                int texW = pixels[0];
                int texH = pixels[1];
                if (texW >= 1 && texH >= 1) {
                    tex.width = (float)texW / 40.0f;
                    tex.height = (float)texH / 40.0f;
                    glGenTextures(1, &tex.id);
                    glBindTexture(GL_TEXTURE_2D, tex.id);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[2]);
                }
                env->ReleaseIntArrayElements(pixelArray, pixels, JNI_ABORT);
            }
            env->DeleteLocalRef(pixelArray);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(jstr);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return tex;
}

TextTexture Renderer::createTextTextureColoredLeft(const std::string& text, int fontSize, int argb) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }

    jstring jstr = env->NewStringUTF(text.c_str());
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "getTextPixelsColoredLeft", "(Ljava/lang/String;II)[I");

    TextTexture tex{};
    if (method && jstr) {
        jintArray pixelArray = (jintArray)env->CallObjectMethod(activityObj, method, jstr, (jint)fontSize, (jint)argb);
        if (pixelArray && !env->ExceptionCheck()) {
            jint* pixels = env->GetIntArrayElements(pixelArray, nullptr);
            if (pixels) {
                int texW = pixels[0];
                int texH = pixels[1];
                if (texW >= 1 && texH >= 1) {
                    tex.width = (float)texW / 40.0f;
                    tex.height = (float)texH / 40.0f;
                    glGenTextures(1, &tex.id);
                    glBindTexture(GL_TEXTURE_2D, tex.id);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[2]);
                }
                env->ReleaseIntArrayElements(pixelArray, pixels, JNI_ABORT);
            }
            env->DeleteLocalRef(pixelArray);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(jstr);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return tex;
}

TextTexture Renderer::createTextTextureRight(const std::string& text, int fontSize) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        app_->activity->vm->AttachCurrentThread(&env, nullptr);
        needsDetach = true;
    }

    jstring jstr = env->NewStringUTF(text.c_str());
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID method = env->GetMethodID(clazz, "getTextPixelsRight", "(Ljava/lang/String;I)[I");

    TextTexture tex{};
    if (method && jstr) {
        jintArray pixelArray = (jintArray)env->CallObjectMethod(activityObj, method, jstr, (jint)fontSize);
        if (pixelArray && !env->ExceptionCheck()) {
            jint* pixels = env->GetIntArrayElements(pixelArray, nullptr);
            if (pixels) {
                int texW = pixels[0];
                int texH = pixels[1];
                if (texW >= 1 && texH >= 1) {
                    tex.width = (float)texW / 40.0f;
                    tex.height = (float)texH / 40.0f;
                    glGenTextures(1, &tex.id);
                    glBindTexture(GL_TEXTURE_2D, tex.id);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[2]);
                }
                env->ReleaseIntArrayElements(pixelArray, pixels, JNI_ABORT);
            }
            env->DeleteLocalRef(pixelArray);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(jstr);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return tex;
}

std::string Renderer::fetchPlayerDisplayName() {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return "玩家";
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID mid = env->GetMethodID(clazz, "getPlayerDisplayName", "()Ljava/lang/String;");
    std::string out = "玩家";
    if (mid) {
        jstring js = (jstring)env->CallObjectMethod(activityObj, mid);
        if (js && !env->ExceptionCheck()) {
            const char* utf = env->GetStringUTFChars(js, nullptr);
            if (utf) {
                out = utf;
                env->ReleaseStringUTFChars(js, utf);
            }
            env->DeleteLocalRef(js);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return out;
}

void Renderer::showPlayerNameEditorDialog() {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return;
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID mid = env->GetMethodID(clazz, "showPlayerNameEditor", "()V");
    if (mid) env->CallVoidMethod(activityObj, mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

bool Renderer::fetchShowSnakeHeadNames() {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return false;
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID mid = env->GetMethodID(clazz, "isShowSnakeHeadNames", "()Z");
    jboolean out = JNI_FALSE;
    if (mid) out = env->CallBooleanMethod(activityObj, mid);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        out = JNI_FALSE;
    }
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
    return out == JNI_TRUE;
}

void Renderer::setShowSnakeHeadNames(bool enabled) {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return;
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);
    jmethodID mid = env->GetMethodID(clazz, "setShowSnakeHeadNames", "(Z)V");
    if (mid) env->CallVoidMethod(activityObj, mid, enabled ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

void Renderer::releaseHeadNameTexCache() {
    for (auto& kv : headNameTexCache_) {
        if (kv.second.id) glDeleteTextures(1, &kv.second.id);
    }
    headNameTexCache_.clear();
}

TextTexture Renderer::getOrCreateHeadNameTexture(const std::string& name) {
    auto it = headNameTexCache_.find(name);
    if (it != headNameTexCache_.end()) return it->second;
    TextTexture t = createTextTexture(name, 68);
    headNameTexCache_[name] = t;
    return t;
}

void Renderer::drawSnakeHeadNameLabel(float wx, float wy, float headRadius, const std::string& name) {
    if (name.empty()) return;
    TextTexture tt = getOrCreateHeadNameTexture(name);
    if (!tt.id) return;
    float tw = std::min(10.0f, tt.width);
    float safeW = std::max(tt.width, 0.01f);
    float th = tw * (tt.height / safeW);
    float labelY = wy + headRadius * 1.2f + th * 0.5f;
    drawShape(wx, labelY, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tt.id);
}

void Renderer::closeMenuSettings() {
    if (game_.getState() != GameState::MENU_SETTINGS) return;
    game_.setState(menuSettingsReturnState_);
    playSfx(2);
}

void Renderer::loadTextTextures() {
    textTextures_["level_1"] = createTextTexture("第一关", 40);
    textTextures_["level_2"] = createTextTexture("第二关", 40);
    textTextures_["level_3"] = createTextTexture("第三关", 40);
    textTextures_["level_4"] = createTextTexture("第四关", 40);
    textTextures_["level_5"] = createTextTexture("第五关", 40);
    textTextures_["level_6"] = createTextTexture("第六关", 40);
    textTextures_["level_7"] = createTextTexture("第七关", 40);
    textTextures_["level_8"] = createTextTexture("第八关", 40);
    textTextures_["level_9"] = createTextTexture("第九关", 40);
    textTextures_["level_10"] = createTextTexture("第十关", 40);
    textTextures_["victory"] = createTextTexture("挑战成功!", 50);
    textTextures_["next_level"] = createTextTexture("下一关", 40);
    textTextures_["retry"] = createTextTexture("重新挑战", 40);
    textTextures_["start"] = createTextTexture("开始游戏", 50);
    textTextures_["endless"] = createTextTexture("无尽模式", 40);
    textTextures_["challenge"] = createTextTexture("挑战模式", 40);
    textTextures_["more"] = createTextTexture("BOSS模式", 40);
    textTextures_["music_on"] = createTextTexture("音乐: 开", 40);
    textTextures_["music_off"] = createTextTexture("音乐: 关", 40);
    textTextures_["sfx_on"] = createTextTexture("音效: 开", 40);
    textTextures_["sfx_off"] = createTextTexture("音效: 关", 40);
    textTextures_["resume"] = createTextTexture("继续游戏", 40);
    textTextures_["main_menu"] = createTextTexture("返回主界面", 40);
    textTextures_["quit"] = createTextTexture("退出游戏", 40);
    textTextures_["settings"] = createTextTexture("游戏设置", 50);
    textTextures_["start"] = createTextTexture("开始游戏", 200);
    textTextures_["endless"] = createTextTexture("无尽模式", 160);
    textTextures_["challenge"] = createTextTexture("挑战模式", 160);
    textTextures_["more"] = createTextTexture("BOSS模式", 160);
    textTextures_["music_on"] = createTextTexture("音乐: 开", 160);
    textTextures_["music_off"] = createTextTexture("音乐: 关", 160);
    textTextures_["sfx_on"] = createTextTexture("音效: 开", 160);
    textTextures_["sfx_off"] = createTextTexture("音效: 关", 160);
    textTextures_["resume"] = createTextTexture("继续游戏", 160);
    textTextures_["main_menu"] = createTextTexture("返回主界面", 160);
    textTextures_["quit"] = createTextTexture("退出游戏", 160);
    textTextures_["settings"] = createTextTexture("设置", 200);

    textTextures_["store_btn"] = createTextTexture("商店", 160);
    textTextures_["inventory_btn"] = createTextTexture("皮肤", 160);
    textTextures_["store_title"] = createTextTexture("皮肤商店", 200);
    textTextures_["inventory_title"] = createTextTexture("皮肤库", 200);
    textTextures_["buy"] = createTextTexture("购买", 120);
    textTextures_["owned"] = createTextTexture("已拥有", 120);
    textTextures_["equip"] = createTextTexture("穿戴", 120);
    textTextures_["equipped"] = createTextTexture("已穿戴", 120);

    textTextures_["price_500"] = createTextTexture("500 金币", 120);
    textTextures_["price_1000"] = createTextTexture("1000 金币", 120);
    textTextures_["price_2000"] = createTextTexture("2000 金币", 120);
    textTextures_["price_3000"] = createTextTexture("3000 金币", 120);
    textTextures_["price_5000"] = createTextTexture("5000 金币", 120);
    textTextures_["price_6000"] = createTextTexture("6000 金币", 120);
    textTextures_["price_8000"] = createTextTexture("8000 金币", 120);
    textTextures_["price_10000"] = createTextTexture("10000 金币", 120);

    textTextures_["skin_0"] = createTextTexture("青色", 100);
    textTextures_["skin_1"] = createTextTexture("红色", 100);
    textTextures_["skin_2"] = createTextTexture("绿色", 100);
    textTextures_["skin_3"] = createTextTexture("黄色", 100);
    textTextures_["skin_4"] = createTextTexture("紫色", 100);
    textTextures_["skin_5"] = createTextTexture("粉色", 100);
    textTextures_["skin_6"] = createTextTexture("橙色", 100);
    textTextures_["skin_7"] = createTextTexture("褐色", 100);

    textTextures_["skin_8"] = createTextTexture("棉花糖", 100);
    textTextures_["skin_9"] = createTextTexture("毛绒球", 100);
    textTextures_["skin_10"] = createTextTexture("大西瓜", 100);
    textTextures_["skin_11"] = createTextTexture("等离子", 100);
    textTextures_["skin_12"] = createTextTexture("金刚体", 100);
    textTextures_["skin_13"] = createTextTexture("熔岩球", 100);
    textTextures_["skin_14"] = createTextTexture("霓虹体", 100);
    textTextures_["skin_15"] = createTextTexture("冰霜晶", 100);
    textTextures_["skin_16"] = createTextTexture("神鳞片", 100);
    textTextures_["skin_17"] = createTextTexture("珍珠贝", 100);
    textTextures_["skin_18"] = createTextTexture("暗星空", 100);
    textTextures_["skin_19"] = createTextTexture("纯金球", 100);

    textTextures_["rank_title"] = createTextTexture("实时排行·前9", 64);
    textTextures_["rank_fold"] = createTextTexture("点击收起", 52);
    textTextures_["rank_expand"] = createTextTexture("排行榜 (点击展开)", 60);
    textTextures_["rank_close_hint"] = createTextTexture("排行榜 (点击关闭)", 60);

    textTextures_["edit_name"] = createTextTexture("修改昵称", 160);
    textTextures_["head_names_on"] = createTextTexture("显示名字: 开", 160);
    textTextures_["head_names_off"] = createTextTexture("显示名字: 关", 160);

    // --- [新增规则文字贴图] ---
    textTextures_["boss_rule_title"] = createTextTexture("虚空狩猎：霓虹之影", 60);
    textTextures_["boss_rule_1"] = createTextTexture("1. 吃🔴🟢🔵食物，获得同色光环", 45);
    textTextures_["boss_rule_2"] = createTextTexture("2. 光环颜色与Boss可攻击段匹配时，加速冲撞可造成伤害", 45);
    textTextures_["boss_rule_3"] = createTextTexture("3. 避开Boss头部和灰色装甲，否则会失败", 45);
    textTextures_["boss_rule_4"] = createTextTexture("4. Boss随血量降低会逐渐变得疯狂", 45);
    textTextures_["boss_rule_5"] = createTextTexture("5. 清空Boss血量即获得胜利", 45);
    textTextures_["boss_rule_6"] = createTextTexture("6. 有机会获得神秘限时闪电道具可斩断甚至击杀Boss", 45);
    textTextures_["boss_btn_ok"] = createTextTexture("已明白 (开始)", 50);
}

namespace {
    const char* utf8RankMedalPrefix(int rank) {
        switch (rank) {
            case 1: return "\xf0\x9f\xa5\x87 ";
            case 2: return "\xf0\x9f\xa5\x88 ";
            case 3: return "\xf0\x9f\xa5\x89 ";
            default: return "";
        }
    }

    constexpr float kSnakeHeadDrawDiam = 1.5f;
    constexpr float kSnakeBodyDrawDiam = 1.1f;

    inline float snakeSegmentDrawSize(float baseCurSize, bool isHead) {
        const float unit = baseCurSize / kSnakeBodyDrawDiam;
        return unit * (isHead ? kSnakeHeadDrawDiam : kSnakeBodyDrawDiam);
    }

    size_t utf8CodepointCount(const std::string& s) {
        size_t n = 0;
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t charLen = 1;
            if ((c & 0x80) == 0) {
                charLen = 1;
            } else if ((c & 0xE0) == 0xC0) {
                charLen = 2;
            } else if ((c & 0xF0) == 0xE0) {
                charLen = 3;
            } else if ((c & 0xF8) == 0xF0) {
                charLen = 4;
            } else {
                ++i;
                continue;
            }
            if (i + charLen > s.size()) break;
            i += charLen;
            ++n;
        }
        return n;
    }

    std::string utf8PrefixCodepoints(const std::string& s, size_t maxCp) {
        if (maxCp == 0) return {};
        size_t count = 0;
        size_t end = 0;
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t charLen = 1;
            if ((c & 0x80) == 0) {
                charLen = 1;
            } else if ((c & 0xE0) == 0xC0) {
                charLen = 2;
            } else if ((c & 0xF0) == 0xE0) {
                charLen = 3;
            } else if ((c & 0xF8) == 0xF0) {
                charLen = 4;
            } else {
                ++i;
                continue;
            }
            if (i + charLen > s.size()) break;
            if (count >= maxCp) break;
            end = i + charLen;
            i += charLen;
            ++count;
        }
        return s.substr(0, end);
    }

    std::string ellipsisRankDisplayName(const std::string& name, size_t maxCp) {
        const std::string ell = u8"\u2026";
        if (utf8CodepointCount(name) <= maxCp) return name;
        if (maxCp == 0) return ell;
        return utf8PrefixCodepoints(name, maxCp - 1) + ell;
    }

    void aiPaletteRgb(int paletteId, float& r, float& g, float& b) {
        switch (paletteId % 6) {
            case 0: r = 0.25f; g = 0.85f; b = 1.0f; break;
            case 1: r = 1.0f; g = 0.45f; b = 0.35f; break;
            case 2: r = 0.45f; g = 1.0f; b = 0.55f; break;
            case 3: r = 1.0f; g = 0.92f; b = 0.35f; break;
            case 4: r = 0.78f; g = 0.4f; b = 1.0f; break;
            default: r = 0.95f; g = 0.5f; b = 1.0f; break;
        }
    }

}

void Renderer::releaseRankLineTextures() {
    for (auto& t : rankLineLeftTextures_) {
        if (t.id) glDeleteTextures(1, &t.id);
    }
    rankLineLeftTextures_.clear();
    for (auto& t : rankLineLenTextures_) {
        if (t.id) glDeleteTextures(1, &t.id);
    }
    rankLineLenTextures_.clear();
}

void Renderer::clearRankPanelCache() {
    releaseRankLineTextures();
    rankSignature_.clear();
}

void Renderer::syncRankPanelTextures() {
    if (!game_.isEndlessArenaMode()) return;
    auto rows = game_.getRankPanelRows();
    std::string pname = fetchPlayerDisplayName();
    const auto& ais = game_.getAISnakes();

    std::vector<int> sig;
    sig.reserve(rows.size() * 5 + static_cast<int>(pname.size()) + 4);
    for (unsigned char c : pname) sig.push_back(static_cast<int>(c));
    sig.push_back(-4242);
    sig.push_back(5);
    for (const auto& row : rows) {
        sig.push_back(row.rank);
        sig.push_back(row.length);
        sig.push_back(row.isPlayer ? 1 : 0);
        sig.push_back(row.aiIndex);
    }
    if (sig == rankSignature_) return;
    releaseRankLineTextures();
    rankSignature_ = std::move(sig);

    constexpr int kPlayerBlueArgb = 0xFF33AAFF;
    constexpr int kRankLineFont = 52;
    constexpr size_t kRankNameMaxCp = 6;

    rankLineLeftTextures_.reserve(rows.size());
    rankLineLenTextures_.reserve(rows.size());
    for (const auto& row : rows) {
        std::string name;
        if (row.isPlayer) {
            name = pname;
        } else if (row.aiIndex >= 0 && static_cast<size_t>(row.aiIndex) < ais.size()) {
            name = ais[static_cast<size_t>(row.aiIndex)].displayName;
        }
        if (name.empty()) name = row.isPlayer ? "玩家" : "?";

        std::string shortName = ellipsisRankDisplayName(name, kRankNameMaxCp);

        std::string leftLine;
        if (row.rank <= 3) {
            leftLine = std::string(utf8RankMedalPrefix(row.rank)) + shortName;
        } else {
            std::string rankPad = (row.rank < 10) ? " " : "";
            leftLine = rankPad + std::to_string(row.rank) + ". " + shortName;
        }

        std::string lenStr = std::to_string(row.length);
        TextTexture lenTex = createTextTextureRight(lenStr, kRankLineFont);
        if (lenTex.id == 0) lenTex = createTextTextureLeft(lenStr, kRankLineFont);
        rankLineLenTextures_.push_back(lenTex);

        if (row.isPlayer) {
            TextTexture tt = createTextTextureColoredLeft(leftLine, kRankLineFont, kPlayerBlueArgb);
            if (tt.id == 0) tt = createTextTextureLeft(leftLine, kRankLineFont);
            if (tt.id == 0) tt = createTextTextureColored(leftLine, kRankLineFont, kPlayerBlueArgb);
            if (tt.id == 0) tt = createTextTexture(leftLine, kRankLineFont);
            rankLineLeftTextures_.push_back(tt);
        } else {
            TextTexture tt = createTextTextureLeft(leftLine, kRankLineFont);
            if (tt.id == 0) tt = createTextTexture(leftLine, kRankLineFont);
            rankLineLeftTextures_.push_back(tt);
        }
    }
}

void Renderer::releaseRankPlayerStatTextures() {
    if (rankPlayerLenTex_.id) {
        glDeleteTextures(1, &rankPlayerLenTex_.id);
        rankPlayerLenTex_ = {};
    }
    if (rankPlayerScoreTex_.id) {
        glDeleteTextures(1, &rankPlayerScoreTex_.id);
        rankPlayerScoreTex_ = {};
    }
    if (rankPlayerKillsTex_.id) {
        glDeleteTextures(1, &rankPlayerKillsTex_.id);
        rankPlayerKillsTex_ = {};
    }
    rankPlayerStatCacheLen_ = -1;
    rankPlayerStatCacheScore_ = -1;
    rankPlayerStatCacheKills_ = -1;
}

void Renderer::syncRankPlayerStatTextures() {
    if (!game_.isEndlessArenaMode()) return;
    int len = static_cast<int>(game_.getSnake().size());
    int score = game_.getScore();
    int kills = game_.getPlayerKillCount();
    if (len == rankPlayerStatCacheLen_ && score == rankPlayerStatCacheScore_ && kills == rankPlayerStatCacheKills_) return;
    rankPlayerStatCacheLen_ = len;
    rankPlayerStatCacheScore_ = score;
    rankPlayerStatCacheKills_ = kills;
    if (rankPlayerLenTex_.id) glDeleteTextures(1, &rankPlayerLenTex_.id);
    if (rankPlayerScoreTex_.id) glDeleteTextures(1, &rankPlayerScoreTex_.id);
    if (rankPlayerKillsTex_.id) glDeleteTextures(1, &rankPlayerKillsTex_.id);
    rankPlayerLenTex_ = {};
    rankPlayerScoreTex_ = {};
    rankPlayerKillsTex_ = {};
    std::string lenLine = std::string(u8"\u957f\u5ea6 ") + std::to_string(len);
    std::string scoreLine = std::string(u8"\u79ef\u5206 ") + std::to_string(score);
    std::string killsLine = std::string(u8"\u51fb\u6740 ") + std::to_string(kills);
    constexpr int kBlueArgb = 0xFF33AAFF;

    rankPlayerLenTex_ = createTextTextureColoredLeft(lenLine, 56, kBlueArgb);
    if (rankPlayerLenTex_.id == 0) rankPlayerLenTex_ = createTextTextureLeft(lenLine, 56);

    rankPlayerScoreTex_ = createTextTextureColoredLeft(scoreLine, 56, kBlueArgb);
    if (rankPlayerScoreTex_.id == 0) rankPlayerScoreTex_ = createTextTextureLeft(scoreLine, 56);

    rankPlayerKillsTex_ = createTextTextureColoredLeft(killsLine, 56, kBlueArgb);
    if (rankPlayerKillsTex_.id == 0) rankPlayerKillsTex_ = createTextTextureLeft(killsLine, 56);
}

void Renderer::drawEndlessRankPanel(float uiHalfWidth) {
    if (!game_.isEndlessArenaMode()) return;
    syncRankPanelTextures();

    const float left = -uiHalfWidth + 1.0f;
    const float topY = kProjectionHalfHeight - 2.2f;

    const float padX = 0.38f;
    const float padY = 0.35f;
    const float lineSpacing = 0.48f;
    const float gapHint = 0.28f;
    const float gapTitle = 0.4f;

    float maxW = 0.0f;

    float titleTw = 0.0f;
    float titleTh = 0.0f;
    std::string titleKey = rankingPanelExpanded_ ? "rank_close_hint" : "rank_expand";
    if (textTextures_.count(titleKey)) {
        auto& title = textTextures_[titleKey];
        titleTw = std::min(10.0f, title.width);
        titleTh = titleTw * (title.height / std::max(title.width, 0.01f));
        maxW = std::max(maxW, titleTw);
    }

    constexpr float kRankMidGap = 2.5f;
    std::vector<float> rowHs;
    rowHs.reserve(rankLineLeftTextures_.size());
    float maxRankRowInnerW = 0.0f;
    for (size_t i = 0; i < rankLineLeftTextures_.size(); ++i) {
        const auto& lt = rankLineLeftTextures_[i];
        const auto& rtex = rankLineLenTextures_[i];
        float lw = lt.width;
        float lh = lt.height;
        float rw = rtex.width;
        float rh = rtex.height;
        maxRankRowInnerW = std::max(maxRankRowInnerW, lw + kRankMidGap + rw);
        rowHs.push_back(std::max(lh, rh));
    }
    maxW = std::max(maxW, maxRankRowInnerW);

    float hintW = 0.0f;
    float hintH = 0.0f;
    if (rankingPanelExpanded_ && textTextures_.count("rank_fold")) {
        auto& hint = textTextures_["rank_fold"];
        hintW = std::min(7.5f, hint.width);
        hintH = hintW * (hint.height / std::max(hint.width, 0.01f));
        maxW = std::max(maxW, hintW);
    }

    const float panelW = maxW + 2.0f * padX;
    const float innerTop = topY - padY;

    if (!rankingPanelExpanded_) {
        float panelH = titleTh + 2.8f * padY;
        const float cx = left + panelW * 0.5f;
        const float cy = topY - panelH * 0.5f;

        drawShape(cx, cy, panelW + 0.35f, panelH + 0.35f, 0.2f, 0.85f, 1.0f, 0.08f, false, 0.11f);
        drawShape(cx, cy, panelW, panelH, 0.04f, 0.06f, 0.12f, 0.14f, false, 0.09f);

        if (titleTh > 0.0f) {
            float yTitleC = innerTop - titleTh * 0.5f;
            drawShape(cx, yTitleC, titleTw, titleTh, 0.6f, 0.8f, 1.0f, 0.9f, false, 0.0f, textTextures_[titleKey].id);
        }

        syncRankPlayerStatTextures();
        if (rankPlayerLenTex_.id && rankPlayerScoreTex_.id && rankPlayerKillsTex_.id) {
            const float margin = 0.22f;
            const float panelRight = left + panelW;
            float tw1 = std::min(7.8f, rankPlayerLenTex_.width);
            float th1 = tw1 * (rankPlayerLenTex_.height / std::max(rankPlayerLenTex_.width, 0.01f));
            float tw2 = std::min(7.8f, rankPlayerScoreTex_.width);
            float th2 = tw2 * (rankPlayerScoreTex_.height / std::max(rankPlayerScoreTex_.width, 0.01f));
            float tw3 = std::min(7.8f, rankPlayerKillsTex_.width);
            float th3 = tw3 * (rankPlayerKillsTex_.height / std::max(rankPlayerKillsTex_.width, 0.01f));

            float statLeftX = panelRight + margin;
            float cx1 = statLeftX + tw1 * 0.5f;
            float cx2 = statLeftX + tw2 * 0.5f;
            float cx3 = statLeftX + tw3 * 0.5f;

            float yLen = innerTop - th1 * 0.5f;
            float yScore = yLen - th1 * 0.5f - 0.25f - th2 * 0.5f;
            float yKills = yScore - th2 * 0.5f - 0.25f - th3 * 0.5f;

            drawShape(cx1, yLen, tw1, th1, 1.0f, 1.0f, 1.0f, 0.95f, false, 0.0f, rankPlayerLenTex_.id);
            drawShape(cx2, yScore, tw2, th2, 1.0f, 1.0f, 1.0f, 0.95f, false, 0.0f, rankPlayerScoreTex_.id);
            drawShape(cx3, yKills, tw3, th3, 1.0f, 1.0f, 1.0f, 0.95f, false, 0.0f, rankPlayerKillsTex_.id);
        }

        rankPanelHitL_ = left;
        rankPanelHitR_ = left + panelW;
        rankPanelHitT_ = topY;
        rankPanelHitB_ = topY - panelH;
        return;
    }

    float innerContentH = 0.0f;
    if (titleTh > 0.0f) {
        innerContentH += (titleTh + gapTitle);
    }

    for (size_t i = 0; i < rowHs.size(); ++i) {
        innerContentH += rowHs[i];
        if (i + 1 < rowHs.size()) innerContentH += lineSpacing;
    }
    innerContentH += gapHint + hintH;

    const float panelH = innerContentH + 2.0f * padY;
    const float cx = left + panelW * 0.5f;
    const float cy = topY - panelH * 0.5f;

    drawShape(cx, cy, panelW + 0.35f, panelH + 0.35f, 0.2f, 0.85f, 1.0f, 0.08f, false, 0.11f);
    drawShape(cx, cy, panelW, panelH, 0.04f, 0.06f, 0.12f, 0.14f, false, 0.09f);

    float yNextTop = innerTop;

    if (titleTh > 0.0f) {
        float yTitleC = yNextTop - titleTh * 0.5f;
        drawShape(cx, yTitleC, titleTw, titleTh, 0.6f, 0.8f, 1.0f, 0.9f, false, 0.0f, textTextures_[titleKey].id);
        yNextTop -= (titleTh + gapTitle);
    }

    const float innerL = left + padX;
    const float innerR = left + panelW - padX;
    for (size_t i = 0; i < rankLineLeftTextures_.size(); ++i) {
        const auto& lt = rankLineLeftTextures_[i];
        const auto& rtex = rankLineLenTextures_[i];
        float lw = lt.width;
        float lh = lt.height;
        float rw = rtex.width;
        float rh = rtex.height;
        float rowH = rowHs[i];
        float yc = yNextTop - rowH * 0.5f;

        if (lt.id) {
            drawShape(innerL + lw * 0.5f, yc, lw, lh, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, lt.id);
        }
        if (rtex.id) {
            drawShape(innerR - rw * 0.5f, yc, rw, rh, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, rtex.id);
        }
        yNextTop -= rowH;
        if (i + 1 < rowHs.size()) yNextTop -= lineSpacing;
    }

    yNextTop -= gapHint;
    if (textTextures_.count("rank_fold") && hintH > 0.0f) {
        float yHintC = yNextTop - hintH * 0.5f;
        drawShape(cx, yHintC, hintW, hintH, 0.7f, 0.85f, 1.0f, 0.55f, false, 0.0f, textTextures_["rank_fold"].id);
    }

    syncRankPlayerStatTextures();
    if (rankPlayerLenTex_.id && rankPlayerScoreTex_.id && rankPlayerKillsTex_.id) {
        const float margin = 0.22f;
        const float panelRight = left + panelW;
        float tw1 = std::min(7.8f, rankPlayerLenTex_.width);
        float th1 = tw1 * (rankPlayerLenTex_.height / std::max(rankPlayerLenTex_.width, 0.01f));
        float tw2 = std::min(7.8f, rankPlayerScoreTex_.width);
        float th2 = tw2 * (rankPlayerScoreTex_.height / std::max(rankPlayerScoreTex_.width, 0.01f));
        float tw3 = std::min(7.8f, rankPlayerKillsTex_.width);
        float th3 = tw3 * (rankPlayerKillsTex_.height / std::max(rankPlayerKillsTex_.width, 0.01f));

        float statLeftX = panelRight + margin;
        float cx1 = statLeftX + tw1 * 0.5f;
        float cx2 = statLeftX + tw2 * 0.5f;
        float cx3 = statLeftX + tw3 * 0.5f;

        float yLen = innerTop - th1 * 0.5f;
        float yScore = yLen - th1 * 0.5f - 0.25f - th2 * 0.5f;
        float yKills = yScore - th2 * 0.5f - 0.25f - th3 * 0.5f;

        drawShape(cx1, yLen, tw1, th1, 1.0f, 1.0f, 1.0f, 0.95f, false, 0.0f, rankPlayerLenTex_.id);
        drawShape(cx2, yScore, tw2, th2, 1.0f, 1.0f, 1.0f, 0.95f, false, 0.0f, rankPlayerScoreTex_.id);
        drawShape(cx3, yKills, tw3, th3, 1.0f, 1.0f, 1.0f, 0.95f, false, 0.0f, rankPlayerKillsTex_.id);
    }

    rankPanelHitL_ = left;
    rankPanelHitR_ = left + panelW;
    rankPanelHitT_ = topY;
    rankPanelHitB_ = topY - panelH;
}


bool Renderer::hitEndlessRankPanel(float nx, float ny) const {
    return nx > rankPanelHitL_ && nx < rankPanelHitR_ && ny < rankPanelHitT_ && ny > rankPanelHitB_;
}

void Renderer::drawButton(float x, float y, float w, float h, float r, float g, float b, bool active, const std::string& label) {
    float alpha = active ? 0.8f : 0.2f;
    float cornerRadius = 0.15f;

    drawShape(x, y, w + 0.6f, h + 0.6f, r, g, b, 0.1f, false, cornerRadius + 0.05f);
    drawShape(x, y, w, h, r, g, b, alpha, false, cornerRadius);
    drawShape(x, y, w, h, 1.0f, 1.0f, 1.0f, alpha * 0.3f, false, cornerRadius);

    if (textTextures_.count(label)) {
        auto& tex = textTextures_[label];
        float tw = tex.width;
        float th = tex.height;
        float maxW = w * 0.8f;
        float maxH = h * 0.65f;
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

    game_.setEquippedSkin(getEquippedSkin());
    game_.update(deltaTime);

    // --- 动态更新玩家名字按钮贴图 ---
    static std::string lastMenuPlayerName = "";
    static float nameUpdateTimer = 0.0f;
    nameUpdateTimer -= deltaTime;
    if (nameUpdateTimer <= 0.0f) {
        nameUpdateTimer = 0.5f;
        std::string currentName = fetchPlayerDisplayName();

        if (currentName.empty()) currentName = "玩家";

        if (currentName != lastMenuPlayerName) {
            lastMenuPlayerName = currentName;
            if (textTextures_.count("edit_name") && textTextures_["edit_name"].id != 0) {
                glDeleteTextures(1, &textTextures_["edit_name"].id);
            }
            textTextures_["edit_name"] = createTextTexture(currentName, 160);
        }
    }

    GameState currentState = game_.getState();
    GameState renderState = currentState;

    if (currentState == GameState::PAUSED || currentState == GameState::STORE || currentState == GameState::SKIN_INVENTORY) {
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
        int earnedCoins = game_.getScore() * 10;
        addCoins(earnedCoins);
        triggerGameOver();
    }

    updateRenderArea();

    // --- 核心修改：雙軌投影矩陣設置 ---
    float aspect = (float)width_ / height_;
    float uiProjHalfHeight = kProjectionHalfHeight; // 固定的 22.0f
    float worldProjHalfHeight = uiProjHalfHeight;

    // 如果是迷宮模式(第七關或第八關)，將主世界視角拉近一倍！
    if ((game_.getCurrentMode() == GameMode::CHALLENGE_7 || game_.getCurrentMode() == GameMode::CHALLENGE_8 || game_.getCurrentMode() == GameMode::CHALLENGE_9) &&
        (currentState == GameState::PLAYING || currentState == GameState::GAME_OVER || currentState == GameState::CHALLENGE_CLEAR || currentState == GameState::PAUSED)) {
        worldProjHalfHeight = 11.0f;
    }

    float uiHalfWidth = uiProjHalfHeight * aspect;

    float worldProjMatrix[16] = {0};
    Utility::buildOrthographicMatrix(worldProjMatrix, worldProjHalfHeight, aspect, -1.f, 1.f);

    float uiProjMatrix[16] = {0};
    Utility::buildOrthographicMatrix(uiProjMatrix, uiProjHalfHeight, aspect, -1.f, 1.f);

    glClearColor(0.01f, 0.01f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    shader_->activate();

    if (currentState == GameState::CHALLENGE_CLEAR && !wasChallengeClear_) {
        wasChallengeClear_ = true;
        //playSfx(1); // 播放過關音效

        int earnedCoins = game_.getScore() * 20;
        addCoins(earnedCoins);

        int curScore = game_.getScore();
        int savedMax = game_.getMaxScore(game_.getCurrentMode());
        int stars = 0; // 準備用來接收星星數

        JNIEnv *env;
        bool needsDetach = false;
        if (app_->activity->vm->GetEnv((void **) &env, JNI_VERSION_1_6) == JNI_EDETACHED) {
            if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return;
            needsDetach = true;
        }
        jobject activityObj = app_->activity->javaGameActivity;
        jclass clazz = env->GetObjectClass(activityObj);

        // --- 核心修復：根據模式分開處理結算邏輯 ---
        if (game_.getCurrentMode() == GameMode::CHALLENGE_4 ||
            game_.getCurrentMode() == GameMode::CHALLENGE_5 ||
            game_.getCurrentMode() == GameMode::CHALLENGE_6 ||
            game_.getCurrentMode() == GameMode::CHALLENGE_7 ||
            game_.getCurrentMode() == GameMode::CHALLENGE_8 ||
            game_.getCurrentMode() == GameMode::CHALLENGE_9) {

            // 迷宮/竞速模式：直接讀取真實耗時，越短越好
            int mazeTime = (int)game_.getMazeTimeElapsed();
            if (savedMax == 0 || mazeTime < savedMax) {
                game_.setMaxScore(game_.getCurrentMode(), mazeTime);
                saveChallengeScore((int)game_.getCurrentMode(), mazeTime);
            }
            // 使用時間來計算迷宮星星
            stars = game_.calculateStars(game_.getCurrentMode(), mazeTime);

            jmethodID method = env->GetMethodID(clazz, "showMazeClearDialog", "(II)V");
            if (method) env->CallVoidMethod(activityObj, method, (jint)stars, (jint)mazeTime);

        }
        else {
            // --- [新增代码：处理打败 Boss 时的结算] ---
            if (game_.getCurrentMode() == GameMode::BOSS_RAID) {
                if (curScore > bossHighScore_) {
                    bossHighScore_ = curScore;
                    saveChallengeScore((int)GameMode::BOSS_RAID, curScore);
                    if (bossHighScoreTex_.id) { glDeleteTextures(1, &bossHighScoreTex_.id); bossHighScoreTex_.id = 0; }
                }
                stars = 3; // 击败 Boss 直接给满星
                jmethodID method = env->GetMethodID(clazz, "showChallengeClearDialog", "(II)V");
                if (method) env->CallVoidMethod(activityObj, method, (jint)stars, (jint)curScore);
            }
                // --- [新增结束] ---
            else {
                // 其他普通模式：读取分数，越高越好
                if (curScore > savedMax) {
                    game_.setMaxScore(game_.getCurrentMode(), curScore);
                    saveChallengeScore((int)game_.getCurrentMode(), curScore);
                }
                // 使用分数来计算普通关卡星星
                stars = game_.getChallengeStars(game_.getCurrentMode());

                jmethodID method = env->GetMethodID(clazz, "showChallengeClearDialog", "(II)V");
                if (method) env->CallVoidMethod(activityObj, method, (jint)stars, (jint)curScore);
            }
        }

        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
        if (needsDetach) app_->activity->vm->DetachCurrentThread();
    }

    // --- 開始繪製不同狀態的畫面 ---
    if (renderState == GameState::START_SCREEN) {
        shader_->setProjectionMatrix(uiProjMatrix);
        if (startBackgroundTextureId_) {
            drawShape(0, 0, uiHalfWidth * 2.0f, uiProjHalfHeight * 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, startBackgroundTextureId_);
        }
        drawButton(0, -10.0f, 20.0f, 8.0f, 0.0f, 1.0f, 0.7f, true, "start");
    }
    else if (renderState == GameState::BOSS_HOW_TO_PLAY) {
        shader_->setProjectionMatrix(uiProjMatrix);
        drawBossHowToPlay();
    }
    else if (renderState == GameState::MODE_SELECTION) {
        shader_->setProjectionMatrix(uiProjMatrix);
        if (currentState != GameState::STORE && currentState != GameState::SKIN_INVENTORY) {
            if (gameBackgroundTextureId_) {
                drawShape(0, 0, uiHalfWidth * 2.0f, uiProjHalfHeight * 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, gameBackgroundTextureId_);
            }
            drawButton(-32.0f, -8.0f, 18.0f, 18.0f, 0.1f, 0.6f, 1.0f, true, "endless");
            drawButton(0, -8.0f, 18.0f, 18.0f, 0.7f, 0.2f, 1.0f, false, "challenge");
            // 修改最后一个按钮的 RGB 颜色：比如改为深紫色 (0.5f, 0.0f, 0.8f)
            drawButton(32.0f, -8.0f, 18.0f, 18.0f, 0.5f, 0.0f, 0.8f, false, "more");

            drawButton(32.0f, 12.0f, 16.0f, 5.0f, 0.9f, 0.6f, 0.1f, true, "store_btn");
            drawButton(32.0f, 5.0f, 16.0f, 5.0f, 0.2f, 0.8f, 0.5f, true, "inventory_btn");
            drawButton(-32.0f, 12.0f, 16.0f, 5.0f, 0.35f, 0.75f, 0.95f, true, "edit_name");
        }
    }
    else if (renderState == GameState::CHALLENGE_SELECTION) {
        shader_->setProjectionMatrix(uiProjMatrix);
        if (gameBackgroundTextureId_) {
            drawShape(0, 0, uiHalfWidth * 2.0f, uiProjHalfHeight * 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, challengeBackgroundTextureId_);
        }
        drawButton(-uiHalfWidth + 9.0f, 17.0f, 14.0f, 4.5f, 0.8f, 0.3f, 0.3f, true, "main_menu");
        int cols = 5;
        float boxW = 8.5f;
        float startX = -22.0f;
        float spacingX = 11.0f;
        float startY = -1.0f;
        float spacingY = 13.0f;

        for (int i = 0; i < 10; ++i) {
            int row = i / cols;
            int col = i % cols;
            float px = startX + col * spacingX;
            float py = startY - row * spacingY;

            float r=0.2f, g=0.8f, b=0.4f;
            if(i%3==1){ r=0.8f; g=0.6f; b=0.1f; }
            else if(i%3==2){ r=0.9f; g=0.2f; b=0.2f; }

            drawShape(px, py, boxW, boxW, r, g, b, 1.0f, true);

            std::string lvlKey = "level_" + std::to_string(i + 1);
            if (textTextures_.count(lvlKey)) {
                auto& tex = textTextures_[lvlKey];
                float tw = boxW * 0.75f;
                float th = tw * (tex.height / std::max(tex.width, 0.01f));
                drawShape(px, py + 1.0f, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex.id);
            }

            int stars = game_.getChallengeStars(static_cast<GameMode>(static_cast<int>(GameMode::CHALLENGE_1) + i));
            for (int s = 0; s < 3; s++) {
                float sx = px + (s - 1) * 2.2f;
                if (s < stars) {
                    drawShape(sx, py - 2.2f, 2.0f, 2.0f, 1.0f, 0.9f, 0.1f, 1.0f, false, 0.0f, 0, false, false, 0.0f, true);
                } else {
                    drawShape(sx, py - 2.2f, 2.0f, 2.0f, 0.3f, 0.3f, 0.3f, 1.0f, false, 0.0f, 0, false, false, 0.0f, true);
                }
            }
        }
    }
    else if (renderState == GameState::PLAYING || renderState == GameState::GAME_OVER|| renderState == GameState::CHALLENGE_CLEAR || renderState == GameState::BOSS_BATTLE) {

        // ========= 第一部分：繪製遊戲世界 =========
        shader_->setProjectionMatrix(worldProjMatrix); // 切換到世界視角

        const auto& snake = game_.getSnake();
        static Vector2f lastCamPos = {90.0f, 60.0f};

        if (!snake.empty()) {
            float dx = snake[0].x - lastCamPos.x;
            float dy = snake[0].y - lastCamPos.y;

            if (std::sqrt(dx*dx + dy*dy) > 30.0f) {
                lastCamPos = snake[0];
            } else {
                float lerpSpeed = 15.0f * deltaTime;
                if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
                lastCamPos.x += dx * lerpSpeed;
                lastCamPos.y += dy * lerpSpeed;
            }
        }
        float camX = lastCamPos.x, camY = lastCamPos.y;

        bool showHeadLabels = fetchShowSnakeHeadNames();
        std::string playerLabelName;
        if (showHeadLabels) {
            playerLabelName = fetchPlayerDisplayName();
            if (playerLabelName != lastHeadLabelPlayerName_) {
                lastHeadLabelPlayerName_ = playerLabelName;
                releaseHeadNameTexCache();
            }
        }

        float w = game_.getWorldWidth(), h = game_.getWorldHeight();
        if (playingBackgroundTextureId_) {
            drawShape(w/2.0f - camX, h/2.0f - camY, w + 0.8f, h + 0.8f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, playingBackgroundTextureId_);
        }

        drawShape(w/2.0f - camX, -0.2f - camY, w + 0.8f, 0.4f, 1.0f, 0.1f, 0.1f, 1.0f);
        drawShape(w/2.0f - camX, h + 0.2f - camY, w + 0.8f, 0.4f, 1.0f, 0.1f, 0.1f, 1.0f);
        drawShape(-0.2f - camX, h/2.0f - camY, 0.4f, h + 0.8f, 1.0f, 0.1f, 0.1f, 1.0f);
        drawShape(w + 0.2f - camX, h/2.0f - camY, 0.4f, h + 0.8f, 1.0f, 0.1f, 0.1f, 1.0f);

        for (const auto& pu : game_.getPowerUps()) {
            GLuint tex = 0;
            if (pu.type == PowerUpType::SPEED) tex = speedTextureId_;
            else if (pu.type == PowerUpType::SHIELD) tex = shieldTextureId_;
            else if (pu.type == PowerUpType::MAGNET) tex = magnetTextureId_;

            // --- [新增] 绘制等离子道具 ---
            if (pu.type == PowerUpType::PLASMA) {
                // 用 isLightning=true 画一个闪电标志的特效球 (利用现有着色器)
                drawShape(pu.pos.x - camX, pu.pos.y - camY, 2.5f, 2.5f, 0.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, 0, false, true);
                continue; // 画完直接进入下一个
            }

            if (tex) {
                drawShape(pu.pos.x - camX, pu.pos.y - camY, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex);
            } else {
                drawShape(pu.pos.x - camX, pu.pos.y - camY, 1.8f, 1.8f, 1.0f, 0.5f, 0.0f, 1.0f, true);
            }
        }

        for (const auto& food : game_.getFoods()) {
            float foodScale = 1.0f;
            if (food.value > 1) {
                foodScale = 1.0f + std::log10(static_cast<float>(food.value)) * 0.8f;
            }
            float foodSize = 0.8f * foodScale;

            if (game_.getCurrentMode() == GameMode::BOSS_RAID && !food.isDropped) {
                foodSize = 1.6f;
            }

            if (food.isDropped) {
                // ... (略过掉落食物的渲染)
            } else {
                float fr = 1.0f, fg = 1.0f, fb = 1.0f;
                // --- 修改点：将食物的颜色映射与光环对齐 ---
                switch(food.colorType) {
                    case 0: fr = 1.0f; fg = 0.0f; fb = 0.0f; break; // 红色 (对应索引0)
                    case 1: fr = 0.0f; fg = 1.0f; fb = 0.0f; break; // 绿色 (对应索引1)
                    case 2: fr = 0.0f; fg = 0.0f; fb = 1.0f; break; // 蓝色 (对应索引2)
                    case 3: fr = 1.0f; fg = 1.0f; fb = 0.0f; break; // 黄色
                    case 4: fr = 1.0f; fg = 0.0f; fb = 1.0f; break; // 紫色
                    case 5: fr = 0.0f; fg = 1.0f; fb = 1.0f; break; // 青色
                }
                drawShape(food.pos.x - camX, food.pos.y - camY, foodSize, foodSize, fr, fg, fb, 1.0f, true);
            }
        }

        for (const auto& wall : game_.getWalls()) {
            drawShape(wall.x - camX, wall.y - camY, 2.0f, 2.0f, 0.4f, 0.4f, 0.4f, 1.0f);
        }

        if (game_.getCurrentMode() == GameMode::CHALLENGE_7||game_.getCurrentMode() == GameMode::CHALLENGE_4||game_.getCurrentMode() == GameMode::CHALLENGE_5||game_.getCurrentMode() == GameMode::CHALLENGE_6||game_.getCurrentMode() == GameMode::CHALLENGE_8||game_.getCurrentMode() == GameMode::CHALLENGE_9) {

            Vector2f exitPos = game_.getMazeExit();
            static float portalRot = 0.0f;
            portalRot += deltaTime * 1.5f;
            drawShape(exitPos.x - camX, exitPos.y - camY, 7.0f, 7.0f, 0.2f, 1.0f, 0.4f, 0.8f, true, 0.0f, 0, false, false, portalRot, true);
            drawShape(exitPos.x - camX, exitPos.y - camY, 4.0f, 4.0f, 0.8f, 1.0f, 0.8f, 1.0f, true);
        }

        for (const auto& ai : game_.getAISnakes()) {
            if (ai.segments.empty()) continue;
            float aiScale = 1.0f + std::min(ai.score * 0.02f, 2.0f);
            float ar = 0.0f, ag = 0.0f, ab = 0.0f;
            aiPaletteRgb(ai.paletteId, ar, ag, ab);
            const size_t n = ai.segments.size();
            for (size_t i = 0; i < n; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(std::max<size_t>(n - 1, 1));
                float intensity = 1.0f - t * 0.45f;
                float curSize = 1.05f * aiScale;
                const float segSize = snakeSegmentDrawSize(curSize, i == 0);
                float ox = ai.segments[i].x - camX;
                float oy = ai.segments[i].y - camY;
                drawShape(ox, oy, segSize, segSize, ar * intensity, ag * intensity, ab * intensity, 1.0f, true);
                if (i == 0) {
                    drawSnakeHeadEyes(ox, oy, ai.rotation, segSize * 0.5f);
                    if (showHeadLabels) {
                        drawSnakeHeadNameLabel(ox, oy, segSize * 0.5f, ai.displayName);
                    }
                }
            }
        }

        float scaleFactor = 1.0f + std::min(game_.getScore() * 0.02f, 2.0f);
        int equippedSkin = getEquippedSkin();

        for (size_t i = 0; i < snake.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(std::max<size_t>(snake.size() - 1, 1));
            float intensity = 1.0f - t * 0.48f;
            float curSize = 1.18f * scaleFactor;
            const float segSize = snakeSegmentDrawSize(curSize, i == 0);
            float px = snake[i].x - camX;
            float py = snake[i].y - camY;

            if (equippedSkin <= 7) {
                float r = 1.f, g = 1.f, b = 1.f;
                switch(equippedSkin) {
                    case 0: r=0.0f; g=1.0f; b=1.0f; break;
                    case 1: r=1.0f; g=0.2f; b=0.2f; break;
                    case 2: r=0.2f; g=1.0f; b=0.2f; break;
                    case 3: r=1.0f; g=0.9f; b=0.2f; break;
                    case 4: r=0.7f; g=0.2f; b=1.0f; break;
                    case 5: r=1.0f; g=0.4f; b=0.8f; break;
                    case 6: r=1.0f; g=0.6f; b=0.0f; break;
                    case 7: r=0.6f; g=0.4f; b=0.2f; break;
                }
                drawShape(px, py, segSize, segSize, r * intensity, g * intensity, b * intensity, 1.0f, true);
            } else {
                GLuint tex = skinTex_[equippedSkin];
                float angle = 0.0f;
                if (i == 0) {
                    angle = game_.getRotation();
                } else {
                    Vector2f dir = snake[i-1] - snake[i];
                    angle = std::atan2(dir.y, dir.x);
                }
                drawShape(px, py, segSize, segSize, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex, false, false, angle);
            }

            if (i == 0) {
                drawSnakeHeadEyes(px, py, game_.getRotation(), segSize * 0.5f);
            }

            if (i == 0 && game_.hasShield()) {
                drawShape(px, py, segSize + 1.2f, segSize + 1.2f, 0.2f, 0.9f, 1.0f, 0.5f, true);
            }

            if (i == 0 && showHeadLabels) {
                drawSnakeHeadNameLabel(px, py, segSize * 0.5f, playerLabelName);
            }
        }



        // --- 绘制 Boss 战内容 ---
        if (game_.getState() == GameState::BOSS_BATTLE) {
            drawBoss(camX, camY);           // 修改：传入坐标偏移
            drawPlayerBuffAura(camX, camY); // 修改：传入坐标偏移

        }

        // ========= 第二部分：繪製懸浮 UI =========
        shader_->setProjectionMatrix(uiProjMatrix); // 切換回絕對 UI 視角

        // [新增]：在这里绘制血条，确保血条位置固定，不随摄像机移动
        if (game_.getState() == GameState::BOSS_BATTLE) {
            drawBossUI();
        }

        if (currentState == GameState::PLAYING || currentState == GameState::BOSS_BATTLE) {
            float joyX = -uiHalfWidth + 18.0f, joyY = -uiProjHalfHeight + 14.0f;
            joyPixelX_ = (joyX / uiHalfWidth + 1.0f) * 0.5f * (float)width_;
            joyPixelY_ = (1.0f - (joyY / uiProjHalfHeight + 1.0f) * 0.5f) * (float)height_;
            drawShape(joyX, joyY, 13.5f, 13.5f, 1.0f, 1.0f, 1.0f, 0.15f, true);
            drawShape(joyX + joystickTiltX_*5.0f, joyY + joystickTiltY_*5.0f, 6.0f, 6.0f, 1.0f, 1.0f, 1.0f, 0.5f, true);

            float bX = uiHalfWidth - 16.0f, bY = -uiProjHalfHeight + 14.0f;
            float boostAlpha = (boostPointerId_ != -1) ? 0.7f : 0.3f;

            drawShape(bX, bY, 11.0f, 11.0f, 1.0f, 1.0f, 1.0f, boostAlpha, true);
            drawShape(bX, bY, 6.0f, 6.0f, 1.0f, 0.8f, 0.0f, 1.0f, false, 0.0f, 0, false, true);
        }

        // ==== 绘制局内挑战目标进度 ====
        if (currentState == GameState::PLAYING && !game_.isEndlessArenaMode()) {
            GameMode curMode = game_.getCurrentMode();
            static int lastTimeVal = -1;
            static GameMode lastTimeMode = GameMode::ENDLESS;

            if (curMode == GameMode::CHALLENGE_4 || curMode == GameMode::CHALLENGE_5 ||
                curMode == GameMode::CHALLENGE_6 || curMode == GameMode::CHALLENGE_7 ||
                curMode == GameMode::CHALLENGE_8 || curMode == GameMode::CHALLENGE_9) {

                int curTime = (int)game_.getMazeTimeElapsed();
                if (curTime != lastTimeVal || curMode != lastTimeMode || timeProgressTex_.id == 0) {
                    lastTimeVal = curTime;
                    lastTimeMode = curMode;
                    if (timeProgressTex_.id) glDeleteTextures(1, &timeProgressTex_.id);
                    std::string timeStr = u8"时间: " + std::to_string(curTime) + "s";
                    timeProgressTex_ = createTextTextureColored(timeStr, 64, 0xFF55DDFF);
                }
                if (timeProgressTex_.id) {
                    float tw = std::min(12.0f, timeProgressTex_.width);
                    float th = tw * (timeProgressTex_.height / std::max(timeProgressTex_.width, 0.01f));
                    drawShape(0.0f, uiProjHalfHeight - 2.5f, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, timeProgressTex_.id);
                }

                // --- 新增：在左上角绘制具体的星级挑战目标 ---
                static GameMode lastGoalMode = GameMode::ENDLESS;
                static TextTexture goalTitleTex, goal3Tex, goal2Tex, goal1Tex;

                // 仅当关卡切换时才重新生成文字贴图，避免每帧生成造成掉帧
                if (curMode != lastGoalMode || goalTitleTex.id == 0) {
                    lastGoalMode = curMode;
                    if (goalTitleTex.id) glDeleteTextures(1, &goalTitleTex.id);
                    if (goal3Tex.id) glDeleteTextures(1, &goal3Tex.id);
                    if (goal2Tex.id) glDeleteTextures(1, &goal2Tex.id);
                    if (goal1Tex.id) glDeleteTextures(1, &goal1Tex.id);

                    int t3 = 0, t2 = 0, t1 = 0;
                    if (curMode == GameMode::CHALLENGE_4) { t3=25; t2=28; t1=32; }
                    else if (curMode == GameMode::CHALLENGE_5) { t3=38; t2=42; t1=48; }
                    else if (curMode == GameMode::CHALLENGE_6) { t3=80; t2=90; t1=100; }
                    else if (curMode == GameMode::CHALLENGE_7) { t3=120; t2=240; t1=360; }
                    else if (curMode == GameMode::CHALLENGE_8) { t3=90; t2=150; t1=240; }
                    else if (curMode == GameMode::CHALLENGE_9) { t3=180; t2=240; t1=360; }

                    goalTitleTex = createTextTextureColoredLeft(u8"挑战目标：", 48, 0xFFFFCC00);
                    // 使用实心文本星号 ★ (U+2605) 替换 Emoji ⭐
                    goal3Tex = createTextTextureColoredLeft(u8"★★★: \u2264" + std::to_string(t3) + u8"秒", 52, 0xFFFFFFFF);
                    goal2Tex = createTextTextureColoredLeft(u8"★★☆: \u2264" + std::to_string(t2) + u8"秒", 52, 0xFFDDDDDD);
                    goal1Tex = createTextTextureColoredLeft(u8"★☆☆: \u2264" + std::to_string(t1) + u8"秒", 52, 0xFFAAAAAA);
                }

                auto drawGoalTex = [&](const TextTexture& tex, float x, float y) {
                    if (tex.id) {
                        // --- 修改 1：增大缩放系数，数值越大文字越大 ---
                        float tw = tex.width * 1.2f;
                        float th = tw * (tex.height / std::max(tex.width, 0.01f));
                        drawShape(x + tw * 0.5f, y, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex.id);
                    }
                };


                // 设置左上角的起始坐标与行间距
                float startX = -uiHalfWidth + 1.0f;
                float startY = uiProjHalfHeight - 2.5f;
                float gap = 2.0f;

                drawGoalTex(goalTitleTex, startX, startY);
                drawGoalTex(goal3Tex, startX, startY - gap);
                drawGoalTex(goal2Tex, startX, startY - gap * 2.0f);
                drawGoalTex(goal1Tex, startX, startY - gap * 3.0f);
            }
                // ... (保留原本的其他模式倒计时绘制逻辑) ...
            else {
                if (game_.hasTimeLimit()) {
                    int curTime = (int)game_.getTimeRemaining();
                    if (curTime != lastTimeVal || curMode != lastTimeMode || timeProgressTex_.id == 0) {
                        lastTimeVal = curTime;
                        lastTimeMode = curMode;
                        if (timeProgressTex_.id) glDeleteTextures(1, &timeProgressTex_.id);

                        std::string timeStr = u8"倒计时: " + std::to_string(curTime) + "s";
                        int color = curTime <= 10 ? 0xFFFF4444 : 0xFFFFAA00;
                        timeProgressTex_ = createTextTextureColored(timeStr, 64, color);
                    }

                    if (timeProgressTex_.id) {
                        float tw = std::min(12.0f, timeProgressTex_.width);
                        float th = tw * (timeProgressTex_.height / std::max(timeProgressTex_.width, 0.01f));
                        float drawX = 0.0f;
                        drawShape(drawX, uiProjHalfHeight - 2.5f, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, timeProgressTex_.id);
                    }
                }

                static int lastProgScore = -1;
                static int lastTarget = -1;

                int curScore = game_.getScore();
                int target = game_.getChallengeTarget();

                if (curScore != lastProgScore || target != lastTarget || challengeProgressTex_.id == 0) {
                    lastProgScore = curScore;
                    lastTarget = target;
                    if (challengeProgressTex_.id) glDeleteTextures(1, &challengeProgressTex_.id);

                    std::string progStr = u8"吃到食物: " + std::to_string(curScore) + " / " + std::to_string(target);
                    challengeProgressTex_ = createTextTextureColoredLeft(progStr, 64, 0xFFFFD700);
                }
                if (challengeProgressTex_.id) {
                    float tw = std::min(15.0f, challengeProgressTex_.width);
                    float th = tw * (challengeProgressTex_.height / std::max(challengeProgressTex_.width, 0.01f));
                    float drawX = -uiHalfWidth + tw * 0.5f + 1.0f;
                    drawShape(drawX, uiProjHalfHeight - 2.5f, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, challengeProgressTex_.id);
                }
            }
        }
        drawEndlessRankPanel(uiHalfWidth);
    }

    if (currentState != GameState::PAUSED && currentState != GameState::STORE && currentState != GameState::SKIN_INVENTORY && currentState != GameState::GAME_OVER) {
        shader_->setProjectionMatrix(uiProjMatrix);
        float gearX = uiHalfWidth - 4.0f;
        float gearY = uiProjHalfHeight - 4.0f;
        drawShape(gearX, gearY, 3.5f, 3.5f, 0.8f, 0.8f, 0.8f, 0.8f, false, 0.0f, 0, true);
    }

    if (currentState == GameState::PAUSED) {
        shader_->setProjectionMatrix(uiProjMatrix);
        drawShape(0, 0, uiHalfWidth * 2.0f, uiProjHalfHeight * 2.0f, 0.0f, 0.0f, 0.0f, 0.6f);

        float boxW = 20.0f;
        float boxH = 34.0f;
        drawShape(0, -0.5f, boxW + 0.6f, boxH + 0.6f, 0.3f, 0.4f, 0.6f, 0.8f, false, 0.1f);
        drawShape(0, -0.5f, boxW, boxH, 0.1f, 0.12f, 0.18f, 0.95f, false, 0.1f);

        auto& titleTex = textTextures_["settings"];
        float titleW = 5.5f;
        float titleH = titleW * (titleTex.height / std::max(titleTex.width, 0.01f));
        float titleY = (previousState_ == GameState::PLAYING) ? 14.0f : 12.0f;
        drawShape(0, titleY, titleW, titleH, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, titleTex.id);

        std::string musicLabel = isMusicOn_ ? "music_on" : "music_off";
        std::string sfxLabel = isSfxOn_ ? "sfx_on" : "sfx_off";

        showSnakeHeadNamesCached_ = fetchShowSnakeHeadNames();
        std::string hnKey = showSnakeHeadNamesCached_ ? "head_names_on" : "head_names_off";

        float btnW = 16.0f;
        float btnH = 3.6f;
        float gap = 4.5f;

        if (previousState_ == GameState::PLAYING) {
            float startY = 8.5f;
            drawButton(0, startY, btnW, btnH, 0.2f, 0.6f, 1.0f, true, musicLabel);
            drawButton(0, startY - gap, btnW, btnH, 0.2f, 0.6f, 1.0f, true, sfxLabel);
            drawButton(0, startY - gap*2, btnW, btnH, 0.2f, 0.6f, 1.0f, true, hnKey);
            drawButton(0, startY - gap*3, btnW, btnH, 0.1f, 0.8f, 0.3f, true, "resume");
            drawButton(0, startY - gap*4, btnW, btnH, 0.8f, 0.4f, 0.1f, true, "main_menu");
            drawButton(0, startY - gap*5, btnW, btnH, 0.9f, 0.2f, 0.2f, true, "quit");
        } else {
            float startY = 6.25f;
            drawButton(0, startY, btnW, btnH, 0.2f, 0.6f, 1.0f, true, musicLabel);
            drawButton(0, startY - gap, btnW, btnH, 0.2f, 0.6f, 1.0f, true, sfxLabel);
            drawButton(0, startY - gap*2, btnW, btnH, 0.2f, 0.6f, 1.0f, true, hnKey);
            drawButton(0, startY - gap*3, btnW, btnH, 0.1f, 0.8f, 0.3f, true, "main_menu");
            drawButton(0, startY - gap*4, btnW, btnH, 0.9f, 0.2f, 0.2f, true, "quit");
        }
    }
    else if (currentState == GameState::STORE) {
        shader_->setProjectionMatrix(uiProjMatrix);
        int currentCoins = getCoins();
        if (currentCoins != lastCoins_) {
            lastCoins_ = currentCoins;
            if (dynamicCoinText_.id != 0) glDeleteTextures(1, &dynamicCoinText_.id);
            dynamicCoinText_ = createTextTexture("金币: " + std::to_string(currentCoins), 140);
        }

        drawShape(0, 0, uiHalfWidth * 4.0f, uiProjHalfHeight * 4.0f, 0.65f, 0.8f, 0.95f, 1.01f);

        int cols = 5;
        float boxW = 11.5f;
        float boxH = 14.5f;
        float startX = -24.0f;
        float spacingX = 13.0f;
        float startY = 4.0f;
        float spacingY = 16.5f;

        for (int i = 8; i <= 19; i++) {
            int index = i - 8;
            int col = index % cols;
            int row = index / cols;
            float px = startX + col * spacingX;
            float py = startY - row * spacingY + storeScrollY_;

            if (py > 25.0f || py < -25.0f) continue;

            drawShape(px, py, boxW + 0.4f, boxH + 0.4f, 0.8f, 0.8f, 0.8f, 1.0f, false, 0.2f);
            drawShape(px, py, boxW, boxH, 1.0f, 1.0f, 1.0f, 1.01f, false, 0.2f);

            drawShape(px, py + 3.0f, 5.0f, 5.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, skinTex_[i]);

            std::string nameKey = "skin_" + std::to_string(i);
            auto& dt = textTextures_[nameKey];
            drawShape(px, py - 0.9f, 5.5f, 5.5f * (dt.height/dt.width), 0.2f, 0.2f, 0.2f, 1.0f, false, 0.0f, dt.id);

            if (isSkinOwned(i)) {
                drawButton(px, py - 4.5f, 8.0f, 3.0f, 0.3f, 0.8f, 0.3f, false, "owned");
            } else {
                int price = 0;
                if (i==8) price=500; else if (i==9) price=1000; else if (i>=10&&i<=12) price=2000;
                else if (i>=13&&i<=15) price=3000; else if (i==16) price=5000; else if (i==17) price=6000;
                else if (i==18) price=8000; else if (i==19) price=10000;

                std::string priceLabel = "price_" + std::to_string(price);
                auto& pTex = textTextures_[priceLabel];
                drawShape(px, py - 2.5f, 4.5f, 4.5f * (pTex.height/pTex.width), 0.9f, 0.2f, 0.2f, 1.0f, false, 0.0f, pTex.id);
                drawButton(px, py - 5.0f, 8.0f, 2.5f, 0.9f, 0.6f, 0.1f, true, "buy");
            }
        }

        drawShape(0, 24.0f, uiHalfWidth * 4.0f, 20.0f, 0.65f, 0.8f, 0.95f, 1.01f);

        auto& titleTex = textTextures_["store_title"];
        drawShape(0.3f, 16.8f, 16.0f, 16.0f * (titleTex.height / titleTex.width), 0.1f, 0.1f, 0.2f, 0.6f, false, 0.0f, titleTex.id);
        drawShape(0.0f, 17.0f, 16.0f, 16.0f * (titleTex.height / titleTex.width), 1.0f, 0.8f, 0.2f, 1.0f, false, 0.0f, titleTex.id);

        if (dynamicCoinText_.id) {
            float tw = 12.0f;
            float th = tw * (dynamicCoinText_.height / dynamicCoinText_.width);
            drawShape(-24.0f, 15.5f, tw, th, 0.9f, 0.6f, 0.1f, 1.0f, false, 0.0f, dynamicCoinText_.id);
        }

        drawButton(26.0f, 16.5f, 12.0f, 4.0f, 0.8f, 0.3f, 0.3f, true, "main_menu");
    }
    else if (currentState == GameState::SKIN_INVENTORY) {
        shader_->setProjectionMatrix(uiProjMatrix);
        drawShape(0, 0, uiHalfWidth * 4.0f, uiProjHalfHeight * 4.0f, 0.65f, 0.8f, 0.95f, 1.01f);

        int equipped = getEquippedSkin();

        std::vector<int> ownedSkins;
        for (int i = 0; i <= 19; ++i) {
            if (isSkinOwned(i)) ownedSkins.push_back(i);
        }

        int cols = 5;
        float boxW = 9.5f;
        float boxH = 11.5f;
        float startX = -22.0f;
        float spacingX = 11.0f;
        float startY = 8.0f;
        float spacingY = 13.5f;

        for (size_t i = 0; i < ownedSkins.size(); ++i) {
            int skinId = ownedSkins[i];
            int row = i / cols;
            int col = i % cols;

            float px = startX + col * spacingX;
            float py = startY - row * spacingY + inventoryScrollY_;

            if (py > 25.0f || py < -25.0f) continue;

            drawShape(px, py, boxW + 0.4f, boxH + 0.4f, 0.8f, 0.8f, 0.8f, 1.0f, false, 0.2f);
            drawShape(px, py, boxW, boxH, 1.0f, 1.0f, 1.0f, 1.01f, false, 0.2f);

            if (skinId >= 8) {
                drawShape(px, py + 2.5f, 4.5f, 4.5f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, skinTex_[skinId]);
            } else {
                float r = 1.f, g = 1.f, b = 1.f;
                switch(skinId) {
                    case 0: r=0.0f; g=1.0f; b=1.0f; break;
                    case 1: r=1.0f; g=0.2f; b=0.2f; break;
                    case 2: r=0.2f; g=1.0f; b=0.2f; break;
                    case 3: r=1.0f; g=0.9f; b=0.2f; break;
                    case 4: r=0.7f; g=0.2f; b=1.0f; break;
                    case 5: r=1.0f; g=0.4f; b=0.8f; break;
                    case 6: r=1.0f; g=0.6f; b=0.0f; break;
                    case 7: r=0.6f; g=0.4f; b=0.2f; break;
                }
                drawShape(px, py + 2.5f, 4.5f, 4.5f, r, g, b, 1.0f, true);
            }

            std::string nameKey = "skin_" + std::to_string(skinId);
            auto& dt = textTextures_[nameKey];
            drawShape(px, py - 0.9f, 4.3f, 4.3f * (dt.height/dt.width), 0.2f, 0.2f, 0.2f, 1.0f, false, 0.0f, dt.id);

            if (equipped == skinId) {
                drawButton(px, py - 3.5f, 7.5f, 2.5f, 0.3f, 0.8f, 0.3f, false, "equipped");
            } else {
                drawButton(px, py - 3.5f, 7.5f, 2.5f, 0.2f, 0.6f, 1.0f, true, "equip");
            }
        }

        drawShape(0, 24.0f, uiHalfWidth * 4.0f, 20.0f, 0.65f, 0.8f, 0.95f, 1.01f);

        auto& titleTex = textTextures_["inventory_title"];
        drawShape(0.3f, 17.3f, 16.0f, 16.0f * (titleTex.height / titleTex.width), 0.1f, 0.1f, 0.2f, 0.6f, false, 0.0f, titleTex.id);
        drawShape(0.0f, 17.5f, 16.0f, 16.0f * (titleTex.height / titleTex.width), 1.0f, 0.8f, 0.2f, 1.0f, false, 0.0f, titleTex.id);

        drawButton(26.0f, 16.5f, 12.0f, 4.0f, 0.8f, 0.3f, 0.3f, true, "main_menu");
    }
    eglSwapBuffers(display_, surface_);

}

void Renderer::drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a, bool isCircle, float radius, GLuint textureId, bool isGear, bool isLightning, float rotation, bool isStar)  {
    if (!shader_ || models_.empty()) return;
    glUniform3f(glGetUniformLocation(shader_->getProgram(), "uOffset"), x, y, 0.0f);
    glUniform2f(glGetUniformLocation(shader_->getProgram(), "uScale"), sx, sy);
    glUniform4f(glGetUniformLocation(shader_->getProgram(), "uColor"), r, g, b, a);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsCircle"), isCircle ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsGear"), isGear ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsLightning"), isLightning ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsStar"), isStar ? 1 : 0);
    glUniform1f(glGetUniformLocation(shader_->getProgram(), "uRadius"), radius);
    glUniform1f(glGetUniformLocation(shader_->getProgram(), "uRotation"), rotation);
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

void Renderer::drawSnakeHeadEyes(float headX, float headY, float facingRad, float bodyRadius) {
    const float eyeR = bodyRadius * 0.65f;
    const float side = bodyRadius * 0.58f;
    const float fwd = bodyRadius * 0.38f;

    const float pupilR = eyeR * 0.72f;
    const float h1R = pupilR * 0.50f;

    const float fx = std::cos(facingRad);
    const float fy = std::sin(facingRad);
    const float lx = -std::sin(facingRad);
    const float ly = std::cos(facingRad);

    for (int s = -1; s <= 1; s += 2) {
        const float sf = static_cast<float>(s);
        const float ex = headX + fx * fwd + lx * (side * sf);
        const float ey = headY + fy * fwd + ly * (side * sf);

        drawShape(ex, ey, eyeR, eyeR, 0.98f, 0.98f, 1.0f, 1.0f, true);

        const float pupilCx = ex + fx * (eyeR * 0.12f);
        const float pupilCy = ey + fy * (eyeR * 0.12f);
        drawShape(pupilCx, pupilCy, pupilR, pupilR, 0.02f, 0.04f, 0.12f, 1.0f, true);

        const float h1OffX = -lx * (pupilR * 0.32f) + fx * (pupilR * 0.1f);
        const float h1OffY = -ly * (pupilR * 0.32f) + fy * (pupilR * 0.1f);
        drawShape(pupilCx + h1OffX, pupilCy + h1OffY, h1R, h1R, 1.0f, 1.0f, 1.0f, 1.0f, true);

        const float h2R = h1R * 0.4f;
        const float h2OffX = lx * (pupilR * 0.45f);
        const float h2OffY = ly * (pupilR * 0.45f);
        drawShape(pupilCx + h2OffX, pupilCy + h2OffY, h2R, h2R, 1.0f, 1.0f, 1.0f, 0.6f, true);
    }
}

void Renderer::handleInput() {
    gRenderer = this;
    if (width_ <= 0 || height_ <= 0) {
        if (app_->window) {
            width_ = ANativeWindow_getWidth(app_->window);
            height_ = ANativeWindow_getHeight(app_->window);
        }
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
            } else if (state == GameState::PAUSED || state == GameState::STORE ||
                       state == GameState::SKIN_INVENTORY) {
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
        int pointerIndex = (motionEvent.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        if (actionMasked == AMOTION_EVENT_ACTION_DOWN ||
            actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            auto &pointer = motionEvent.pointers[pointerIndex];
            float px = GameActivityPointerAxes_getX(&pointer);
            float py = GameActivityPointerAxes_getY(&pointer);
            float aspect = (float) width_ / height_;

            // 所有 UI 點擊交互仍然基於標準的 22.0f 投影座標
            float nx = (px / (float) width_ * 2.0f - 1.0f) * aspect * kProjectionHalfHeight;
            float ny = (1.0f - py / (float) height_ * 2.0f) * kProjectionHalfHeight;

            if (game_.getState() == GameState::STORE ||
                game_.getState() == GameState::SKIN_INVENTORY) {
                initialTouchX_ = nx;
                initialTouchY_ = ny;
                lastTouchY_ = ny;
                isDraggingUI_ = false;
                continue;
            }

            if (game_.getState() != GameState::PAUSED && game_.getState() != GameState::STORE &&
                game_.getState() != GameState::SKIN_INVENTORY &&
                game_.getState() != GameState::GAME_OVER) {
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
            }
            else if (game_.getState() == GameState::MODE_SELECTION) {
                if (abs(nx + 32.0f) < 9.0f && abs(ny + 8.0f) < 9.0f) {
                    endlessArenaActive_ = true;
                    game_.setEndlessArenaMode(true);
                    clearRankPanelCache();
                    playSfx(2);
                    game_.reset();
                    game_.startGame();
                } else if (abs(nx) < 9.0f && abs(ny + 8.0f) < 9.0f) {
                    playSfx(2);
                    game_.setState(GameState::CHALLENGE_SELECTION);
                } else if (abs(nx - 32.0f) < 9.0f && abs(ny + 8.0f) < 9.0f) {
                    endlessArenaActive_ = false;
                    game_.setEndlessArenaMode(false);
                    clearRankPanelCache();
                    playSfx(2);
                    // 修改点：不再调用通用的 reset/startGame，而是启动你的 Boss 战初始化
                    game_.startBossLevel();
                } else if (abs(nx - 32.0f) < 8.0f && abs(ny - 12.0f) < 2.5f) {
                    previousState_ = game_.getState();
                    game_.setState(GameState::STORE);
                    storeScrollY_ = 0.0f;
                    playSfx(2);
                } else if (abs(nx - 32.0f) < 8.0f && abs(ny - 5.0f) < 2.5f) {
                    previousState_ = game_.getState();
                    game_.setState(GameState::SKIN_INVENTORY);
                    inventoryScrollY_ = 0.0f;
                    playSfx(2);
                } else if (abs(nx + 32.0f) < 8.0f && abs(ny - 12.0f) < 2.5f) {
                    showPlayerNameEditorDialog();
                    clearRankPanelCache();
                    releaseHeadNameTexCache();
                    lastHeadLabelPlayerName_.clear();
                    playSfx(2);
                }
            }
            else if (game_.getState() == GameState::BOSS_HOW_TO_PLAY) {
                // 判定点击了屏幕中央的“已明白”按钮
                if (abs(nx) < 8.0f && abs(ny + 11.0f) < 2.5f) {
                    playSfx(2);
                    game_.setState(GameState::BOSS_BATTLE); // 真正开始战斗
                }
            }
            else if (game_.getState() == GameState::CHALLENGE_SELECTION) {
                float worldHalfWidth = (float) width_ / height_ * 22.0f;
                if (abs(nx - (-worldHalfWidth + 9.0f)) < 7.0f && abs(ny - 17.0f) < 3.0f) {
                    playSfx(2);
                    game_.setState(GameState::MODE_SELECTION);
                    continue;
                }

                float startX = -22.0f;
                float spacingX = 11.0f;
                float startY = -1.0f;
                float spacingY = 13.0f;

                for (int i = 0; i < 10; ++i) {
                    int row = i / 5;
                    int col = i % 5;
                    float px = startX + col * spacingX;
                    float py = startY - row * spacingY;
                    if (std::sqrt((nx - px)*(nx - px) + (ny - py)*(ny - py)) < 4.5f) {
                        playSfx(2);
                        if (i == 0) game_.startChallengeLevel1();
                        else if (i == 1) game_.startChallengeLevel2();
                        else if (i == 2) game_.startChallengeLevel3();
                        else if (i == 3) game_.startChallengeLevel4();
                        else if (i == 4) game_.startChallengeLevel5();
                        else if (i == 5) game_.startChallengeLevel6();
                        else if (i == 6) game_.startChallengeLevel7();
                        else if (i == 7) game_.startChallengeLevel8();
                        else if (i == 8) game_.startChallengeLevel9();
                        else if (i == 9) game_.startChallengeLevel10();
                    }
                }
            }
            else if (game_.getState() == GameState::PLAYING || game_.getState() == GameState::BOSS_BATTLE) {
                if (game_.isEndlessArenaMode() && hitEndlessRankPanel(nx, ny)) {
                    rankingPanelExpanded_ = !rankingPanelExpanded_;
                    playSfx(2);
                    continue;
                }

                if (px < (float) width_ * 0.5f && joystickPointerId_ == -1)
                    joystickPointerId_ = pointer.id;
                else if (px >= (float) width_ * 0.5f && boostPointerId_ == -1)
                    boostPointerId_ = pointer.id;
            }
            else if (game_.getState() == GameState::GAME_OVER && game_.isEndlessArenaMode()) {
                if (hitEndlessRankPanel(nx, ny)) {
                    rankingPanelExpanded_ = !rankingPanelExpanded_;
                    playSfx(2);
                    continue;
                }
            }
            else if (game_.getState() == GameState::PAUSED) {
                if (abs(nx) < 8.0f) {
                    float gap = 4.5f;
                    if (previousState_ == GameState::PLAYING) {
                        float startY = 8.5f;
                        if (abs(ny - startY) < 1.8f) {
                            isMusicOn_ = !isMusicOn_; setAudioSetting(1, isMusicOn_); playSfx(2);
                        } else if (abs(ny - (startY - gap)) < 1.8f) {
                            isSfxOn_ = !isSfxOn_; setAudioSetting(2, isSfxOn_); playSfx(2);
                        } else if (abs(ny - (startY - gap * 2)) < 1.8f) {
                            bool v = !fetchShowSnakeHeadNames();
                            setShowSnakeHeadNames(v); showSnakeHeadNamesCached_ = v;
                            releaseHeadNameTexCache(); playSfx(2);
                        } else if (abs(ny - (startY - gap * 3)) < 1.8f) {
                            game_.setState(previousState_); playSfx(2);
                        } else if (abs(ny - (startY - gap * 4)) < 1.8f) {
                            game_.setState(GameState::MODE_SELECTION); playSfx(2);
                        } else if (abs(ny - (startY - gap * 5)) < 1.8f) {
                            playSfx(2); pendingExitDialog_.store(true);
                        }
                    } else {
                        float startY = 6.25f;
                        if (abs(ny - startY) < 1.8f) {
                            isMusicOn_ = !isMusicOn_; setAudioSetting(1, isMusicOn_); playSfx(2);
                        } else if (abs(ny - (startY - gap)) < 1.8f) {
                            isSfxOn_ = !isSfxOn_; setAudioSetting(2, isSfxOn_); playSfx(2);
                        } else if (abs(ny - (startY - gap * 2)) < 1.8f) {
                            bool v = !fetchShowSnakeHeadNames();
                            setShowSnakeHeadNames(v); showSnakeHeadNamesCached_ = v;
                            releaseHeadNameTexCache(); playSfx(2);
                        } else if (abs(ny - (startY - gap * 3)) < 1.8f) {
                            game_.setState(GameState::MODE_SELECTION); playSfx(2);
                        } else if (abs(ny - (startY - gap * 4)) < 1.8f) {
                            playSfx(2); pendingExitDialog_.store(true);
                        }
                    }
                }
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_MOVE) {
            for (int p = 0; p < motionEvent.pointerCount; p++) {
                auto &pointer = motionEvent.pointers[p];
                float px = GameActivityPointerAxes_getX(&pointer);
                float py = GameActivityPointerAxes_getY(&pointer);
                float aspect = (float) width_ / height_;
                float nx = (px / (float) width_ * 2.0f - 1.0f) * aspect * kProjectionHalfHeight;
                float ny = (1.0f - py / (float) height_ * 2.0f) * kProjectionHalfHeight;

                if (game_.getState() == GameState::STORE ||
                    game_.getState() == GameState::SKIN_INVENTORY) {
                    float dy = ny - lastTouchY_;
                    lastTouchY_ = ny;
                    if (std::abs(ny - initialTouchY_) > 1.0f) isDraggingUI_ = true;

                    if (game_.getState() == GameState::STORE) {
                        storeScrollY_ += dy;
                        if (storeScrollY_ < 0.0f) storeScrollY_ = 0.0f;
                        if (storeScrollY_ > 35.0f) storeScrollY_ = 35.0f;
                    } else {
                        inventoryScrollY_ += dy;
                        if (inventoryScrollY_ < 0.0f) inventoryScrollY_ = 0.0f;
                        if (inventoryScrollY_ > 40.0f) inventoryScrollY_ = 40.0f;
                    }
                }

                if (pointer.id == joystickPointerId_ &&
                        (game_.getState() == GameState::PLAYING || game_.getState() == GameState::BOSS_BATTLE)) {
                    float dx = px - joyPixelX_, dy_joy = py - joyPixelY_;
                    float dist = std::sqrt(dx * dx + dy_joy * dy_joy);
                    if (dist > 5.0f) {
                        game_.setRotation(std::atan2(-dy_joy, dx));
                        float maxD = 180.0f;
                        float clampD = std::min(dist, maxD);
                        joystickTiltX_ = (dx / dist) * (clampD / maxD);
                        joystickTiltY_ = -(dy_joy / dist) * (clampD / maxD);
                    }
                }
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_UP ||
                   actionMasked == AMOTION_EVENT_ACTION_POINTER_UP ||
                   actionMasked == AMOTION_EVENT_ACTION_CANCEL) {
            int32_t id = motionEvent.pointers[pointerIndex].id;

            auto &pointer = motionEvent.pointers[pointerIndex];
            float px = GameActivityPointerAxes_getX(&pointer);
            float py = GameActivityPointerAxes_getY(&pointer);
            float aspect = (float) width_ / height_;
            float nx = (px / (float) width_ * 2.0f - 1.0f) * aspect * kProjectionHalfHeight;
            float ny = (1.0f - py / (float) height_ * 2.0f) * kProjectionHalfHeight;

            if (game_.getState() == GameState::STORE) {
                if (!isDraggingUI_) {
                    float startX = -24.0f;
                    float spacingX = 13.0f;
                    float startY = 4.0f;
                    float spacingY = 16.5f;

                    for (int j = 8; j <= 19; j++) {
                        int index = j - 8;
                        int col = index % 5;
                        int row = index / 5;
                        float bx = startX + col * spacingX;
                        float by = startY - row * spacingY + storeScrollY_;
                        float btnY = by - 5.0f;

                        if (ny < 12.0f && abs(nx - bx) < 5.0f && abs(ny - btnY) < 2.5f) {
                            int price = 0;
                            if (j == 8) price = 500;
                            else if (j == 9) price = 1000;
                            else if (j >= 10 && j <= 12) price = 2000;
                            else if (j >= 13 && j <= 15) price = 3000;
                            else if (j == 16) price = 5000;
                            else if (j == 17) price = 6000;
                            else if (j == 18) price = 8000; else if (j == 19) price = 10000;

                            buySkin(j, price);
                        }
                    }
                    if (abs(nx - 26.0f) < 7.0f && abs(ny - 14.5f) < 3.0f) {
                        game_.setState(previousState_);
                        playSfx(2);
                    }
                }
            } else if (game_.getState() == GameState::SKIN_INVENTORY) {
                if (!isDraggingUI_) {
                    std::vector<int> ownedSkins;
                    for (int j = 0; j <= 19; ++j) if (isSkinOwned(j)) ownedSkins.push_back(j);

                    float startX = -22.0f;
                    float spacingX = 11.0f;
                    float startY = 8.0f;
                    float spacingY = 13.5f;

                    for (size_t j = 0; j < ownedSkins.size(); ++j) {
                        int row = j / 5;
                        int col = j % 5;
                        float bx = startX + col * spacingX;
                        float by = startY - row * spacingY + inventoryScrollY_;
                        float btnY = by - 3.5f;

                        if (ny < 14.0f && abs(nx - bx) < 4.5f && abs(ny - btnY) < 2.0f) {
                            equipSkin(ownedSkins[j]);
                            playSfx(2);
                        }
                    }
                    if (abs(nx - 26.0f) < 7.0f && abs(ny - 16.0f) < 3.0f) {
                        game_.setState(previousState_);
                        playSfx(2);
                    }
                }
            }

            if (id == joystickPointerId_) {
                joystickPointerId_ = -1;
                joystickTiltX_ = 0;
                joystickTiltY_ = 0;
            } else if (id == boostPointerId_) boostPointerId_ = -1;
        }
    }

    if (game_.getState() == GameState::PAUSED) game_.setBoosting(false);
    else game_.setBoosting(boostPointerId_ != -1);

    android_app_clear_motion_events(inputBuffer);
}

void Renderer::triggerGameOver() {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void **) &env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return;
        needsDetach = true;
    }
    jobject activityObj = app_->activity->javaGameActivity;
    jclass clazz = env->GetObjectClass(activityObj);

    int score = game_.getScore();

    if (!game_.isEndlessArenaMode()) {
        // --- [新增代码：处理 Boss 战死亡时的分数保存] ---
        if (game_.getCurrentMode() == GameMode::BOSS_RAID) {
            // 弹出挑战失败结算框
            int stars = 0;
            jmethodID method = env->GetMethodID(clazz, "showChallengeFailDialog", "(II)V");
            if (method) env->CallVoidMethod(activityObj, method, (jint)stars, (jint)score);
        }
            // --- [新增结束] ---
        else {
            int savedMax = game_.getMaxScore(game_.getCurrentMode());
            if (score > savedMax) {
                game_.setMaxScore(game_.getCurrentMode(), score);
                jmethodID saveMethod = env->GetMethodID(clazz, "saveChallengeScore", "(II)V");
                if (saveMethod)
                    env->CallVoidMethod(activityObj, saveMethod, (jint) game_.getCurrentMode(),
                                        (jint) score);
            }

            if (game_.isTimeOut()) {
                int stars = game_.calculateStars(game_.getCurrentMode(), score);
                jmethodID method = env->GetMethodID(clazz, "showTimeOutDialog", "(II)V");
                if (method) env->CallVoidMethod(activityObj, method, (jint) stars, (jint) score);
            } else {
                int stars = game_.calculateStars(game_.getCurrentMode(), score);
                jmethodID method = env->GetMethodID(clazz, "showChallengeFailDialog", "(II)V");
                if (method) env->CallVoidMethod(activityObj, method, (jint) stars, (jint) score);
            }
        }
    }
    else {
        int earnedCoins = score * 10;
        jmethodID method = env->GetMethodID(clazz, "showGameOverDialog", "(II)V");
        if (method) env->CallVoidMethod(activityObj, method, (jint) score, (jint) earnedCoins);
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(clazz);
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

void Renderer::restartGame() {
    pendingRestart_.store(false);
    wasGameOver_ = false;
    wasChallengeClear_ = false;

    joystickTiltX_ = 0;
    joystickTiltY_ = 0;
    joystickPointerId_ = -1;
    boostPointerId_ = -1;

    if (game_.getCurrentMode() == GameMode::CHALLENGE_1) {
        game_.startChallengeLevel1();
    } else if (game_.getCurrentMode() == GameMode::CHALLENGE_2) {
        game_.startChallengeLevel2();
    } else if (game_.getCurrentMode() == GameMode::CHALLENGE_3) {
        game_.startChallengeLevel3();
    }else if (game_.getCurrentMode() == GameMode::CHALLENGE_4) game_.startChallengeLevel4();
    else if (game_.getCurrentMode() == GameMode::CHALLENGE_5) game_.startChallengeLevel5();
    else if (game_.getCurrentMode() == GameMode::CHALLENGE_6) game_.startChallengeLevel6();
    else if (game_.getCurrentMode() == GameMode::CHALLENGE_7) game_.startChallengeLevel7();
    else if (game_.getCurrentMode() == GameMode::CHALLENGE_8) game_.startChallengeLevel8();
    else if (game_.getCurrentMode() == GameMode::CHALLENGE_9) game_.startChallengeLevel9();
    else if (game_.getCurrentMode() == GameMode::CHALLENGE_10) {
        game_.startChallengeLevel10();
    }
        // --- [核心修改点]：在此处加入对 Boss 模式的判断 ---
    else if (game_.getCurrentMode() == GameMode::BOSS_RAID) {
        game_.startBossLevel(); // 重新调用你写的 Boss 战初始化逻辑
    }
        // --- [修改结束] ---
    else {
        game_.setEndlessArenaMode(endlessArenaActive_);
        game_.reset();
        game_.startGame();
    }
}

void Renderer::goToMainMenu() {
    game_.setEndlessArenaMode(endlessArenaActive_);
    game_.reset();
    game_.setState(GameState::MODE_SELECTION);
    wasGameOver_ = false;
    wasChallengeClear_ = false;
    pendingMainMenu_.store(false);
    joystickTiltX_ = 0;
    joystickTiltY_ = 0;
    joystickPointerId_ = -1;
    boostPointerId_ = -1;
}

void Renderer::initRenderer() {
    constexpr EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE,
                                  EGL_WINDOW_BIT, EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8,
                                  EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);
    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);
    EGLSurface surface = eglCreateWindowSurface(display, supportedConfigs[0], app_->window,
                                                nullptr);
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, supportedConfigs[0], nullptr,
                                          contextAttribs);
    eglMakeCurrent(display, surface, surface, context);
    display_ = display;
    surface_ = surface;
    context_ = context;
    shader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertex, fragment, "inPosition", "inUV", "uProjection"));
    if (shader_) shader_->activate();
    createModels();
    updateRenderArea();
}

void Renderer::updateRenderArea() {
    EGLint width, height;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
    if (width > 0 && height > 0 && (width != width_ || height != height_)) {
        width_ = width;
        height_ = height;
        glViewport(0, 0, width, height);
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

void Renderer::drawBoss(float camX, float camY) {
    const auto& boss = game_.getBoss();
    // 在循环开始前更新闪烁逻辑（或者在 update 里更新，这里简单处理）
    bool isHitFlashing = (boss.hitFlashTimer > 0.0f);
    if (!boss.active) return;

    for (const auto& seg : boss.segments) {
        float r = 0.2f, g = 0.2f, b = 0.2f; // 默认装甲颜色：深灰色
        bool isGlow = false;

        // 根据段位类型设置颜色和光效
        if (seg.type == BossSegmentType::HEAD) {
            r = 0.1f; g = 0.1f; b = 0.1f; // 头部：纯黑
        } else if (seg.type == BossSegmentType::CORE) {
            isGlow = true;
            // 核心颜色匹配：确保这里和食物/光环一致
            if (seg.colorType == 0) { r = 1.0f; g = 0.0f; b = 0.0f; }      // 红色
            else if (seg.colorType == 1) { r = 0.0f; g = 1.0f; b = 0.0f; } // 绿色
            else if (seg.colorType == 2) { r = 0.0f; g = 0.0f; b = 1.0f; } // 蓝色
        }

        // 绘制段位圆柱/圆形
        float size = (seg.type == BossSegmentType::HEAD) ? 2.5f : 1.8f;

        // 在绘制核心段之前加入
        if (seg.type == BossSegmentType::CORE) {
            float pulse = 1.0f + 0.1f * std::sin(std::chrono::steady_clock::now().time_since_epoch().count() * 0.00000001f);
            size *= pulse;
        }

        // 核心段增加一层外发光（Vibe 核心）
        if (isGlow) {
            drawShape(seg.pos.x - camX, seg.pos.y - camY, size * 1.5f, size * 1.5f, r, g, b, 0.3f, true);
        }

        // 如果正在闪烁，强制将颜色变为白色 (Vibe: 硬核打击感)
        if (isHitFlashing) {
            r = 1.0f; g = 1.0f; b = 1.0f;
        }

        drawShape(seg.pos.x - camX, seg.pos.y - camY, size, size, r, g, b, 1.0f, true);
    }
}

void Renderer::drawBossUI() {
    const auto& boss = game_.getBoss();
    if (!boss.active) return;

    // 绘制血条背景（黑色边框）
    drawShape(0.0f, 18.0f, 20.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.5f);

    // 计算血条比例
    float hpRate = boss.totalHP / boss.maxHP;
    // 绘制红色进度条（根据阶段改变颜色）
    float r = 1.0f, g = 0.0f, b = 0.0f;
    if (boss.phase == 3) { g = 0.5f; } // 狂暴阶段变为橙红色

    drawShape(-10.0f + 10.0f * hpRate, 18.0f, 20.0f * hpRate, 0.6f, r, g, b, 0.8f);

    // --- [新增代码：绘制 Boss 模式积分与最高分] ---
    int curScore = game_.getScore();

    // 动态更新当前得分的文字贴图
    if (curScore != lastBossScore_ || bossScoreTex_.id == 0) {
        lastBossScore_ = curScore;
        if (bossScoreTex_.id) glDeleteTextures(1, &bossScoreTex_.id);
        bossScoreTex_ = createTextTextureColoredLeft(u8"当前得分: " + std::to_string(curScore), 50, 0xFFFFFFFF);
    }

    // 初始化最高分的文字贴图
    if (bossHighScoreTex_.id == 0) {
        bossHighScoreTex_ = createTextTextureColoredLeft(u8"历史最高: " + std::to_string(bossHighScore_), 50, 0xFFFFCC00);
    }

    float aspect = (float)width_ / height_;
    float uiHalfWidth = kProjectionHalfHeight * aspect;

    // 在屏幕左上方绘制当前得分 (白色)
    if (bossScoreTex_.id) {
        float tw = std::min(10.0f, bossScoreTex_.width);
        float th = tw * (bossScoreTex_.height / std::max(bossScoreTex_.width, 0.01f));
        drawShape(-uiHalfWidth + 1.0f + tw/2.0f, 20.5f, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, bossScoreTex_.id);
    }
    // 在屏幕右上方绘制历史最高分 (金色)
    if (bossHighScoreTex_.id) {
        float tw = std::min(10.0f, bossHighScoreTex_.width);
        float th = tw * (bossHighScoreTex_.height / std::max(bossHighScoreTex_.width, 0.01f));
        drawShape(uiHalfWidth - 1.0f - tw/2.0f, 20.5f, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, bossHighScoreTex_.id);
    }
    // --- [新增结束] ---
}

void Renderer::drawPlayerBuffAura(float camX, float camY) {
    int colorIdx = game_.getPlayerBuffColor();
    if (colorIdx == -1) return;

    const auto& snake = game_.getSnake();
    if (snake.empty()) return;

    // --- [新增] 最高优先级视觉：等离子斩击状态 ---
    if (game_.getPlasmaTimer() > 0.0f) {
        // 画一个极大且耀眼的青白色斩击光环
        drawShape(snake[0].x - camX, snake[0].y - camY, 3.8f, 3.8f, 0.8f, 1.0f, 1.0f, 0.9f, true);
        return; // 在斩击状态下，不显示普通颜色 Buff
    }

    // 在玩家头部绘制一个半透明的颜色环，提示当前拥有的攻击属性
    // --- 修改点：统一光环的颜色映射 ---
    float r = 0, g = 0, b = 0;
    if (colorIdx == 0) { r = 1.0f; g = 0.0f; b = 0.0f; }      // 0 为红色
    else if (colorIdx == 1) { r = 0.0f; g = 1.0f; b = 0.0f; } // 1 为绿色
    else if (colorIdx == 2) { r = 0.0f; g = 0.0f; b = 1.0f; } // 2 为蓝色
        // 增加对其他索引的处理，防止吃错颜色后光环消失
    else { r = 0.5f; g = 0.5f; b = 0.5f; }

    // --- [核心修改：将原本的 2.0f 调大为 4.5f，并加上双层呼吸动画] ---
    float pulse = 1.0f + 0.12f * std::sin(std::chrono::steady_clock::now().time_since_epoch().count() * 0.000000015f);
    float auraSize = 4.5f * pulse; // 放大光环

    // 画两层光环：外层大而淡，内层小而亮，形成很明显的能量场 Vibe
    drawShape(snake[0].x - camX, snake[0].y - camY, auraSize, auraSize, r, g, b, 0.25f, true);
    drawShape(snake[0].x - camX, snake[0].y - camY, auraSize * 0.7f, auraSize * 0.7f, r, g, b, 0.55f, true);
}

void Renderer::drawBossHowToPlay() {
    float aspect = (float)width_ / height_;
    float uiHalfWidth = kProjectionHalfHeight * aspect;

    // 1. 背景遮罩 (深蓝色半透明)
    drawShape(0, 0, uiHalfWidth * 2.0f, kProjectionHalfHeight * 2.0f, 0.05f, 0.05f, 0.1f, 0.85f);

    // 2. 装饰边框 (做成可爱的圆角矩形效果)
    drawShape(0, 0, 32.0f, 36.0f, 0.2f, 0.6f, 1.0f, 0.2f, false, 0.2f); // 外发光
    drawShape(0, 0, 30.0f, 34.0f, 0.1f, 0.12f, 0.18f, 0.98f, false, 0.1f); // 主体

    // 3. 绘制文字 (逐行排列)
    float startY = 15.0f; // 略微上提到 15.0
    float gap = 3.8f;     // 行间距微调为 3.8

    // 标题 (金色)
    drawShape(0, startY, textTextures_["boss_rule_title"].width, textTextures_["boss_rule_title"].height, 1.0f, 0.8f, 0.2f, 1.0f, false, 0.0f, textTextures_["boss_rule_title"].id);

    // 规则 1-6 (白色)
    drawShape(0, startY - gap, textTextures_["boss_rule_1"].width, textTextures_["boss_rule_1"].height, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, textTextures_["boss_rule_1"].id);
    drawShape(0, startY - gap*2, textTextures_["boss_rule_2"].width, textTextures_["boss_rule_2"].height, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, textTextures_["boss_rule_2"].id);
    drawShape(0, startY - gap*3, textTextures_["boss_rule_3"].width, textTextures_["boss_rule_3"].height, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, textTextures_["boss_rule_3"].id);
    drawShape(0, startY - gap*4, textTextures_["boss_rule_4"].width, textTextures_["boss_rule_4"].height, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, textTextures_["boss_rule_4"].id);
    drawShape(0, startY - gap*5, textTextures_["boss_rule_5"].width, textTextures_["boss_rule_5"].height, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, textTextures_["boss_rule_5"].id);
    drawShape(0, startY - gap*6, textTextures_["boss_rule_6"].width, textTextures_["boss_rule_6"].height, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, textTextures_["boss_rule_6"].id);
    // 4. "已明白" 按钮 (绿色)
    drawButton(0, -12.5f, 16.0f, 4.5f, 0.2f, 0.8f, 0.4f, true, "boss_btn_ok");
}
