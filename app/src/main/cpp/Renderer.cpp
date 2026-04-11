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
uniform float uRadius;
uniform bool uUseTexture;
uniform sampler2D uTexture;
out vec4 outColor;

void main() {
    if (uUseTexture) {
        vec2 correctedUV = vec2(fragUV.x, 1.0 - fragUV.y);
        vec4 texColor = texture(uTexture, correctedUV);
        texColor = texColor.bgra;

        if (uColor.r > 0.99 && uColor.g > 0.99 && uColor.b > 0.99 && uColor.a > 0.99) {
            outColor = texColor;
        } else {
            outColor = vec4(uColor.rgb, min(uColor.a, 1.0) * texColor.a);
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
        width_(0), height_(0), shaderNeedsNewProjectionMatrix_(true), game_(180.0f, 120.0f),
        joystickTiltX_(0), joystickTiltY_(0), joystickPointerId_(-1), boostPointerId_(-1),
        joyPixelX_(0), joyPixelY_(0), wasGameOver_(false), pendingRestart_(false), pendingMainMenu_(false),
        startBackgroundTextureId_(0), gameBackgroundTextureId_(0), playingBackgroundTextureId_(0),
        speedTextureId_(0), shieldTextureId_(0), magnetTextureId_(0) {

    for(int i=0; i<=19; i++) { skinTex_[i] = 0; }
    dynamicCoinText_.id = 0;
    endlessSnakeCountTex_.id = 0;

    gRenderer = this;
    initRenderer();
    loadTextTextures();
    startBackgroundTextureId_ = loadBackgroundTexture("images/background.png");
    gameBackgroundTextureId_ = loadBackgroundTexture("images/main.png");
    playingBackgroundTextureId_ = loadBackgroundTexture("images/background_game.png");

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

    lastFrameTime_ = std::chrono::steady_clock::now();
}

Renderer::~Renderer() {
    if (gRenderer == this) gRenderer = nullptr;
    clearRankPanelCache();
    if (startBackgroundTextureId_) glDeleteTextures(1, &startBackgroundTextureId_);
    if (gameBackgroundTextureId_) glDeleteTextures(1, &gameBackgroundTextureId_);
    if (playingBackgroundTextureId_) glDeleteTextures(1, &playingBackgroundTextureId_);
    if (speedTextureId_) glDeleteTextures(1, &speedTextureId_);
    if (shieldTextureId_) glDeleteTextures(1, &shieldTextureId_);
    if (magnetTextureId_) glDeleteTextures(1, &magnetTextureId_);
    for(int i=8; i<=19; i++) {
        if(skinTex_[i]) glDeleteTextures(1, &skinTex_[i]);
    }
    if (dynamicCoinText_.id) glDeleteTextures(1, &dynamicCoinText_.id);
    if (endlessSnakeCountTex_.id) glDeleteTextures(1, &endlessSnakeCountTex_.id);
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

    textTextures_["store_btn"] = createTextTexture("商店", 40);
    textTextures_["inventory_btn"] = createTextTexture("皮肤", 40);
    textTextures_["store_title"] = createTextTexture("皮肤商店", 50);
    textTextures_["inventory_title"] = createTextTexture("皮肤库", 50);
    textTextures_["buy"] = createTextTexture("购买", 30);
    textTextures_["owned"] = createTextTexture("已拥有", 30);
    textTextures_["equip"] = createTextTexture("穿戴", 30);
    textTextures_["equipped"] = createTextTexture("已穿戴", 30);

    textTextures_["price_500"] = createTextTexture("500 金币", 30);
    textTextures_["price_1000"] = createTextTexture("1000 金币", 30);
    textTextures_["price_2000"] = createTextTexture("2000 金币", 30);
    textTextures_["price_3000"] = createTextTexture("3000 金币", 30);
    textTextures_["price_5000"] = createTextTexture("5000 金币", 30);
    textTextures_["price_6000"] = createTextTexture("6000 金币", 30);
    textTextures_["price_8000"] = createTextTexture("8000 金币", 30);
    textTextures_["price_10000"] = createTextTexture("10000 金币", 30);

    textTextures_["skin_0"] = createTextTexture("青色", 25);
    textTextures_["skin_1"] = createTextTexture("红色", 25);
    textTextures_["skin_2"] = createTextTexture("绿色", 25);
    textTextures_["skin_3"] = createTextTexture("黄色", 25);
    textTextures_["skin_4"] = createTextTexture("紫色", 25);
    textTextures_["skin_5"] = createTextTexture("粉色", 25);
    textTextures_["skin_6"] = createTextTexture("橙色", 25);
    textTextures_["skin_7"] = createTextTexture("褐色", 25);

    textTextures_["skin_8"] = createTextTexture("棉花糖", 25);
    textTextures_["skin_9"] = createTextTexture("毛绒球", 25);
    textTextures_["skin_10"] = createTextTexture("大西瓜", 25);
    textTextures_["skin_11"] = createTextTexture("等离子", 25);
    textTextures_["skin_12"] = createTextTexture("金刚体", 25);
    textTextures_["skin_13"] = createTextTexture("熔岩球", 25);
    textTextures_["skin_14"] = createTextTexture("霓虹体", 25);
    textTextures_["skin_15"] = createTextTexture("冰霜晶", 25);
    textTextures_["skin_16"] = createTextTexture("神鳞片", 25);
    textTextures_["skin_17"] = createTextTexture("珍珠贝", 25);
    textTextures_["skin_18"] = createTextTexture("暗星空", 25);
    textTextures_["skin_19"] = createTextTexture("纯金球", 25);

    textTextures_["rank_title"] = createTextTexture("实时长度·前五", 22);
    textTextures_["rank_fold"] = createTextTexture("点击收起", 18);
    textTextures_["rank_expand"] = createTextTexture("排行 展开", 20);
}

namespace {
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
    for (auto& t : rankLineTextures_) {
        if (t.id) glDeleteTextures(1, &t.id);
    }
    rankLineTextures_.clear();
}

void Renderer::clearRankPanelCache() {
    releaseRankLineTextures();
    rankSignature_.clear();
    if (endlessSnakeCountTex_.id) {
        glDeleteTextures(1, &endlessSnakeCountTex_.id);
        endlessSnakeCountTex_.id = 0;
    }
    lastEndlessSnakeTotal_ = -1;
}

void Renderer::syncRankPanelTextures() {
    if (!game_.isEndlessArenaMode()) return;
    auto board = game_.getLengthLeaderboard();
    constexpr size_t kRankTopN = 5;
    if (board.size() > kRankTopN) board.resize(kRankTopN);
    std::vector<int> sig;
    sig.reserve(board.size() * 3);
    for (const auto& row : board) {
        sig.push_back(row.length);
        sig.push_back(row.isPlayer ? 1 : 0);
        sig.push_back(row.aiIndex);
    }
    if (sig == rankSignature_) return;
    releaseRankLineTextures();
    rankSignature_ = std::move(sig);
    rankLineTextures_.reserve(board.size());
    for (size_t i = 0; i < board.size(); ++i) {
        std::string line = std::to_string(i + 1) + ". ";
        if (board[i].isPlayer) line += "你 ";
        else line += std::string("电脑") + static_cast<char>('A' + board[i].aiIndex) + " ";
        line += std::to_string(board[i].length);
        rankLineTextures_.push_back(createTextTexture(line, 24));
    }
}

void Renderer::drawEndlessRankPanel(float worldHalfWidth) {
    if (!game_.isEndlessArenaMode()) return;
    syncRankPanelTextures();

    const float left = -worldHalfWidth + 1.0f;
    const float topY = kProjectionHalfHeight - 2.2f;

    if (!rankingPanelExpanded_) {
        const float barW = 14.0f;
        const float barH = 3.0f;
        const float cx = left + barW * 0.5f;
        const float cy = topY - barH * 0.5f;
        drawShape(cx, cy, barW + 0.4f, barH + 0.4f, 0.15f, 0.75f, 1.0f, 0.22f, false, 0.14f);
        drawShape(cx, cy, barW, barH, 0.05f, 0.07f, 0.14f, 0.9f, false, 0.12f);
        if (textTextures_.count("rank_expand")) {
            auto& tex = textTextures_["rank_expand"];
            float tw = std::min(12.5f, tex.width);
            float th = tw * (tex.height / tex.width);
            drawShape(cx, cy, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex.id);
        }
        rankPanelHitL_ = left;
        rankPanelHitR_ = left + barW;
        rankPanelHitT_ = topY;
        rankPanelHitB_ = topY - barH;
        return;
    }

    const float padX = 0.55f;
    const float padY = 0.5f;
    const float gapTitle = 0.45f;
    const float lineSpacing = 0.65f;
    const float gapHint = 0.4f;

    float maxW = 0.0f;
    float titleTw = 0.0f;
    float titleTh = 0.0f;
    if (textTextures_.count("rank_title")) {
        auto& title = textTextures_["rank_title"];
        titleTw = std::min(11.0f, title.width);
        titleTh = titleTw * (title.height / title.width);
        maxW = std::max(maxW, titleTw);
    }

    std::vector<float> lineWs;
    std::vector<float> lineHs;
    lineWs.reserve(rankLineTextures_.size());
    lineHs.reserve(rankLineTextures_.size());
    for (const auto& rt : rankLineTextures_) {
        float lw = std::min(12.5f, rt.width);
        float lh = lw * (rt.height / rt.width);
        lineWs.push_back(lw);
        lineHs.push_back(lh);
        maxW = std::max(maxW, lw);
    }

    float hintW = 0.0f;
    float hintH = 0.0f;
    if (textTextures_.count("rank_fold")) {
        auto& hint = textTextures_["rank_fold"];
        hintW = std::min(10.0f, hint.width);
        hintH = hintW * (hint.height / hint.width);
        maxW = std::max(maxW, hintW);
    }

    float innerContentH = titleTh + gapTitle;
    for (size_t i = 0; i < lineHs.size(); ++i) {
        innerContentH += lineHs[i];
        if (i + 1 < lineHs.size()) innerContentH += lineSpacing;
    }
    innerContentH += gapHint + hintH;

    const float panelW = maxW + 2.0f * padX;
    const float panelH = innerContentH + 2.0f * padY;
    const float cx = left + panelW * 0.5f;
    const float cy = topY - panelH * 0.5f;

    drawShape(cx, cy, panelW + 0.5f, panelH + 0.5f, 0.2f, 0.85f, 1.0f, 0.28f, false, 0.14f);
    drawShape(cx, cy, panelW, panelH, 0.04f, 0.06f, 0.12f, 0.9f, false, 0.11f);

    const float innerTop = topY - padY;
    float yNextTop = innerTop - titleTh - gapTitle;
    if (textTextures_.count("rank_title") && titleTh > 0.0f) {
        float yTitleC = innerTop - titleTh * 0.5f;
        drawShape(cx, yTitleC, titleTw, titleTh, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, textTextures_["rank_title"].id);
    }

    for (size_t i = 0; i < lineHs.size(); ++i) {
        float yc = yNextTop - lineHs[i] * 0.5f;
        drawShape(cx, yc, lineWs[i], lineHs[i], 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, rankLineTextures_[i].id);
        yNextTop -= lineHs[i];
        if (i + 1 < lineHs.size()) yNextTop -= lineSpacing;
    }

    yNextTop -= gapHint;
    if (textTextures_.count("rank_fold") && hintH > 0.0f) {
        float yHintC = yNextTop - hintH * 0.5f;
        drawShape(cx, yHintC, hintW, hintH, 0.7f, 0.85f, 1.0f, 0.95f, false, 0.0f, textTextures_["rank_fold"].id);
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
        if (currentState != GameState::STORE && currentState != GameState::SKIN_INVENTORY) {
            if (gameBackgroundTextureId_) {
                drawShape(0, 0, worldHalfWidth * 2.0f, kProjectionHalfHeight * 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, gameBackgroundTextureId_);
            }
            drawButton(-32.0f, -8.0f, 18.0f, 18.0f, 0.1f, 0.6f, 1.0f, true, "endless");
            drawButton(0, -8.0f, 18.0f, 18.0f, 0.7f, 0.2f, 1.0f, false, "challenge");
            drawButton(32.0f, -8.0f, 18.0f, 18.0f, 0.2f, 0.9f, 0.4f, false, "more");

            drawButton(32.0f, 12.0f, 16.0f, 5.0f, 0.9f, 0.6f, 0.1f, true, "store_btn");
            drawButton(-32.0f, 12.0f, 16.0f, 5.0f, 0.2f, 0.8f, 0.5f, true, "inventory_btn");
        }
    }
    else if (renderState == GameState::PLAYING || renderState == GameState::GAME_OVER) {
        if (!game_.isEndlessArenaMode()) {
            if (endlessSnakeCountTex_.id) {
                glDeleteTextures(1, &endlessSnakeCountTex_.id);
                endlessSnakeCountTex_.id = 0;
            }
            lastEndlessSnakeTotal_ = -1;
        }

        const auto& snake = game_.getSnake();
        static Vector2f lastCamPos = {90.0f, 60.0f};

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

        for (const auto& pu : game_.getPowerUps()) {
            GLuint tex = 0;
            if (pu.type == PowerUpType::SPEED) tex = speedTextureId_;
            else if (pu.type == PowerUpType::SHIELD) tex = shieldTextureId_;
            else if (pu.type == PowerUpType::MAGNET) tex = magnetTextureId_;

            if (tex) {
                drawShape(pu.pos.x - camX, pu.pos.y - camY, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex);
            } else {
                drawShape(pu.pos.x - camX, pu.pos.y - camY, 1.8f, 1.8f, 1.0f, 0.5f, 0.0f, 1.0f, true);
            }
        }

        // 渲染食物（核心：大小按对数缩放，本体带皮肤贴图）
        for (const auto& food : game_.getFoods()) {
            float foodScale = 1.0f;
            if (food.value > 1) {
                foodScale = 1.0f + std::log10(static_cast<float>(food.value)) * 0.8f;
            }
            float foodSize = 0.8f * foodScale;

            if (food.isDropped) {
                int skinId = food.colorType;
                if (skinId <= 7) {
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
                    drawShape(food.pos.x - camX, food.pos.y - camY, foodSize, foodSize, r, g, b, 1.0f, true);
                } else {
                    GLuint tex = skinTex_[skinId];
                    drawShape(food.pos.x - camX, food.pos.y - camY, foodSize, foodSize, 1.0f, 1.0f, 1.0f, 1.0f, true, 0.0f, tex);
                }
            } else {
                float fr = 1.0f, fg = 1.0f, fb = 1.0f;
                switch(food.colorType) {
                    case 0: fr = 0.2f; fg = 0.5f; fb = 1.0f; break;
                    case 1: fr = 1.0f; fg = 0.4f; fb = 0.8f; break;
                    case 2: fr = 1.0f; fg = 0.2f; fb = 0.2f; break;
                    case 3: fr = 0.2f; fg = 1.0f; fb = 0.2f; break;
                    case 4: fr = 0.7f; fg = 0.2f; fb = 1.0f; break;
                    case 5: fr = 1.0f; fg = 0.9f; fb = 0.2f; break;
                }
                drawShape(food.pos.x - camX, food.pos.y - camY, foodSize, foodSize, fr, fg, fb, 1.0f, true);
            }
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
                float taper = 0.62f + 0.38f * (1.0f - t);
                float curSize = ((i == 0) ? 1.35f : 0.95f) * aiScale * taper;
                float ox = ai.segments[i].x - camX;
                float oy = ai.segments[i].y - camY;
                drawShape(ox, oy, curSize * 1.22f, curSize * 1.22f, ar * 0.2f, ag * 0.2f, ab * 0.25f, 1.08f, true);
                drawShape(ox, oy, curSize, curSize, ar * intensity, ag * intensity, ab * intensity, 1.0f, true);
                if (i == 0) {
                    drawShape(ox, oy, curSize * 1.55f, curSize * 1.55f, ar * 0.45f, ag * 0.45f, ab * 0.5f, 0.35f, true);
                }
            }
        }

        float scaleFactor = 1.0f + std::min(game_.getScore() * 0.02f, 2.0f);
        int equippedSkin = getEquippedSkin();

        for (size_t i = 0; i < snake.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(std::max<size_t>(snake.size() - 1, 1));
            float intensity = 1.0f - t * 0.48f;
            float taper = 0.68f + 0.32f * (1.0f - t);
            float curSize = (i == 0 ? 1.58f : 1.05f) * scaleFactor * taper;
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
                drawShape(px, py, curSize * 1.2f, curSize * 1.2f, r * 0.15f, g * 0.15f, b * 0.18f, 1.06f, true);
                drawShape(px, py, curSize, curSize, r * intensity, g * intensity, b * intensity, 1.0f, true);
                if (i == 0) {
                    drawShape(px, py, curSize * 1.75f, curSize * 1.75f, r * 0.5f, g * 0.55f, b * 0.6f, 0.32f, true);
                    drawShape(px, py, curSize * 1.15f, curSize * 1.15f, 1.0f, 1.0f, 1.0f, 0.22f, true);
                }
            } else {
                GLuint tex = skinTex_[equippedSkin];
                float angle = 0.0f;
                if (i == 0) {
                    angle = game_.getRotation();
                } else {
                    Vector2f dir = snake[i-1] - snake[i];
                    angle = std::atan2(dir.y, dir.x);
                }
                drawShape(px, py, curSize * 1.18f, curSize * 1.18f, 0.05f, 0.08f, 0.12f, 1.05f, true);
                drawShape(px, py, curSize, curSize, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, tex, false, false, angle);
                if (i == 0) {
                    drawShape(px, py, curSize * 1.72f, curSize * 1.72f, 0.4f, 0.75f, 1.0f, 0.28f, true);
                }
            }

            if (i == 0 && game_.hasShield()) {
                drawShape(px, py, curSize + 1.2f, curSize + 1.2f, 0.2f, 0.9f, 1.0f, 0.5f, true);
            }
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

        drawEndlessRankPanel(worldHalfWidth);

        if (game_.isEndlessArenaMode()) {
            int total = game_.getTotalSnakeCount();
            if (total != lastEndlessSnakeTotal_) {
                if (endlessSnakeCountTex_.id) glDeleteTextures(1, &endlessSnakeCountTex_.id);
                endlessSnakeCountTex_ = createTextTexture("蛇总数: " + std::to_string(total), 26);
                lastEndlessSnakeTotal_ = total;
            }
            if (endlessSnakeCountTex_.id != 0) {
                float tcx = worldHalfWidth - 13.5f;
                float tcy = kProjectionHalfHeight - 3.6f;
                float tw = std::min(15.0f, endlessSnakeCountTex_.width);
                float th = tw * (endlessSnakeCountTex_.height / endlessSnakeCountTex_.width);
                drawShape(tcx, tcy, tw + 1.2f, th + 0.9f, 0.03f, 0.06f, 0.12f, 0.82f, false, 0.12f);
                drawShape(tcx, tcy, tw, th, 1.0f, 1.0f, 1.0f, 1.0f, false, 0.0f, endlessSnakeCountTex_.id);
            }
        }
    }

    if (currentState != GameState::PAUSED && currentState != GameState::STORE && currentState != GameState::SKIN_INVENTORY && currentState != GameState::GAME_OVER) {
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
    else if (currentState == GameState::STORE) {
        int currentCoins = getCoins();
        if (currentCoins != lastCoins_) {
            lastCoins_ = currentCoins;
            if (dynamicCoinText_.id != 0) glDeleteTextures(1, &dynamicCoinText_.id);
            dynamicCoinText_ = createTextTexture("金币: " + std::to_string(currentCoins), 35);
        }

        drawShape(0, 0, worldHalfWidth * 4.0f, kProjectionHalfHeight * 4.0f, 0.65f, 0.8f, 0.95f, 1.01f);

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

        drawShape(0, 24.0f, worldHalfWidth * 4.0f, 20.0f, 0.65f, 0.8f, 0.95f, 1.01f);

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
        drawShape(0, 0, worldHalfWidth * 4.0f, kProjectionHalfHeight * 4.0f, 0.65f, 0.8f, 0.95f, 1.01f);

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

        drawShape(0, 24.0f, worldHalfWidth * 4.0f, 20.0f, 0.65f, 0.8f, 0.95f, 1.01f);

        auto& titleTex = textTextures_["inventory_title"];
        drawShape(0.3f, 17.3f, 16.0f, 16.0f * (titleTex.height / titleTex.width), 0.1f, 0.1f, 0.2f, 0.6f, false, 0.0f, titleTex.id);
        drawShape(0.0f, 17.5f, 16.0f, 16.0f * (titleTex.height / titleTex.width), 1.0f, 0.8f, 0.2f, 1.0f, false, 0.0f, titleTex.id);

        drawButton(26.0f, 16.5f, 12.0f, 4.0f, 0.8f, 0.3f, 0.3f, true, "main_menu");
    }

    eglSwapBuffers(display_, surface_);
}

void Renderer::drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a, bool isCircle, float radius, GLuint textureId, bool isGear, bool isLightning, float rotation) {
    if (!shader_ || models_.empty()) return;
    glUniform3f(glGetUniformLocation(shader_->getProgram(), "uOffset"), x, y, 0.0f);
    glUniform2f(glGetUniformLocation(shader_->getProgram(), "uScale"), sx, sy);
    glUniform4f(glGetUniformLocation(shader_->getProgram(), "uColor"), r, g, b, a);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsCircle"), isCircle ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsGear"), isGear ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsLightning"), isLightning ? 1 : 0);
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
            } else if (state == GameState::PAUSED || state == GameState::STORE || state == GameState::SKIN_INVENTORY) {
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

            if (game_.getState() == GameState::STORE || game_.getState() == GameState::SKIN_INVENTORY) {
                initialTouchX_ = nx;
                initialTouchY_ = ny;
                lastTouchY_ = ny;
                isDraggingUI_ = false;
                continue;
            }

            if (game_.getState() != GameState::PAUSED && game_.getState() != GameState::STORE && game_.getState() != GameState::SKIN_INVENTORY && game_.getState() != GameState::GAME_OVER) {
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
                if (abs(nx + 32.0f) < 9.0f && abs(ny + 8.0f) < 9.0f) {
                    endlessArenaActive_ = true;
                    game_.setEndlessArenaMode(true);
                    clearRankPanelCache();
                    playSfx(2);
                    game_.reset();
                    game_.startGame();
                } else if (abs(nx) < 9.0f && abs(ny + 8.0f) < 9.0f) {
                    endlessArenaActive_ = false;
                    game_.setEndlessArenaMode(false);
                    clearRankPanelCache();
                    playSfx(2);
                    game_.reset();
                    game_.startGame();
                } else if (abs(nx - 32.0f) < 9.0f && abs(ny + 8.0f) < 9.0f) {
                    endlessArenaActive_ = false;
                    game_.setEndlessArenaMode(false);
                    clearRankPanelCache();
                    playSfx(2);
                    game_.reset();
                    game_.startGame();
                }
                else if (abs(nx - 32.0f) < 8.0f && abs(ny - 12.0f) < 2.5f) {
                    previousState_ = game_.getState();
                    game_.setState(GameState::STORE);
                    storeScrollY_ = 0.0f;
                    playSfx(2);
                }
                else if (abs(nx + 32.0f) < 8.0f && abs(ny - 12.0f) < 2.5f) {
                    previousState_ = game_.getState();
                    game_.setState(GameState::SKIN_INVENTORY);
                    inventoryScrollY_ = 0.0f;
                    playSfx(2);
                }
            } else if (game_.getState() == GameState::GAME_OVER && game_.isEndlessArenaMode()) {
                if (hitEndlessRankPanel(nx, ny)) {
                    rankingPanelExpanded_ = !rankingPanelExpanded_;
                    playSfx(2);
                    continue;
                }
            } else if (game_.getState() == GameState::PLAYING) {
                if (game_.isEndlessArenaMode() && hitEndlessRankPanel(nx, ny)) {
                    rankingPanelExpanded_ = !rankingPanelExpanded_;
                    playSfx(2);
                    continue;
                }
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
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_MOVE) {
            for (int p = 0; p < motionEvent.pointerCount; p++) {
                auto &pointer = motionEvent.pointers[p];
                float px = GameActivityPointerAxes_getX(&pointer);
                float py = GameActivityPointerAxes_getY(&pointer);
                float aspect = (float)width_ / height_;
                float nx = (px / (float)width_ * 2.0f - 1.0f) * aspect * kProjectionHalfHeight;
                float ny = (1.0f - py / (float)height_ * 2.0f) * kProjectionHalfHeight;

                if (game_.getState() == GameState::STORE || game_.getState() == GameState::SKIN_INVENTORY) {
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

                if (pointer.id == joystickPointerId_ && game_.getState() == GameState::PLAYING) {
                    float dx = px - joyPixelX_, dy_joy = py - joyPixelY_;
                    float dist = std::sqrt(dx*dx + dy_joy*dy_joy);
                    if (dist > 5.0f) {
                        game_.setRotation(std::atan2(-dy_joy, dx));
                        float maxD = 180.0f; float clampD = std::min(dist, maxD);
                        joystickTiltX_ = (dx/dist)*(clampD/maxD); joystickTiltY_ = -(dy_joy/dist)*(clampD/maxD);
                    }
                }
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_UP || actionMasked == AMOTION_EVENT_ACTION_POINTER_UP || actionMasked == AMOTION_EVENT_ACTION_CANCEL) {
            int32_t id = motionEvent.pointers[pointerIndex].id;

            auto &pointer = motionEvent.pointers[pointerIndex];
            float px = GameActivityPointerAxes_getX(&pointer);
            float py = GameActivityPointerAxes_getY(&pointer);
            float aspect = (float)width_ / height_;
            float nx = (px / (float)width_ * 2.0f - 1.0f) * aspect * kProjectionHalfHeight;
            float ny = (1.0f - py / (float)height_ * 2.0f) * kProjectionHalfHeight;

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
                            if (j==8) price=500; else if (j==9) price=1000; else if (j>=10&&j<=12) price=2000;
                            else if (j>=13&&j<=15) price=3000; else if (j==16) price=5000; else if (j==17) price=6000;
                            else if (j==18) price=8000; else if (j==19) price=10000;

                            buySkin(j, price);
                        }
                    }
                    if (abs(nx - 26.0f) < 7.0f && abs(ny - 14.5f) < 3.0f) { game_.setState(previousState_); playSfx(2); }
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
                    if (abs(nx - 26.0f) < 7.0f && abs(ny - 16.0f) < 3.0f) { game_.setState(previousState_); playSfx(2); }
                }
            }

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

    int score = game_.getScore();
    int earnedCoins = score * 10;

    jmethodID method = env->GetMethodID(clazz, "showGameOverDialog", "(II)V");
    if (method) env->CallVoidMethod(activityObj, method, (jint)score, (jint)earnedCoins);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (needsDetach) app_->activity->vm->DetachCurrentThread();
}

void Renderer::restartGame() {
    game_.setEndlessArenaMode(endlessArenaActive_);
    game_.reset();
    game_.startGame();
    wasGameOver_ = false; pendingRestart_.store(false);
    joystickTiltX_ = 0; joystickTiltY_ = 0; joystickPointerId_ = -1; boostPointerId_ = -1;
}

void Renderer::goToMainMenu() {
    game_.setEndlessArenaMode(endlessArenaActive_);
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
