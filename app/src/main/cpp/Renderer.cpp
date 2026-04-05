#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
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
JNIEXPORT void JNICALL
Java_com_example_snake_MainActivity_nativeRestartGame(JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    if (gRenderer) gRenderer->requestRestart();
}
}

Renderer::Renderer(android_app *pApp) :
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
        joyPixelX_(0), joyPixelY_(0),
        wasGameOver_(false),
        pendingRestart_(false) {
    gRenderer = this;
    initRenderer();
    lastFrameTime_ = std::chrono::steady_clock::now();
}

Renderer::~Renderer() {
    if (gRenderer == this) gRenderer = nullptr;
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
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
out vec4 outColor;
void main() {
    if (uIsCircle) {
        float dist = distance(fragUV, vec2(0.5, 0.5));
        if (dist > 0.5) discard;
        float edge = smoothstep(0.5, 0.48, dist);
        outColor = vec4(uColor.rgb, uColor.a * edge);
    } else {
        outColor = uColor;
    }
}
)fragment";

static constexpr float kProjectionHalfHeight = 22.f; 
static constexpr float kProjectionNearPlane = -1.f;
static constexpr float kProjectionFarPlane = 1.f;

void Renderer::render() {
    if (pendingRestart_.load()) {
        restartGame();
    }

    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastFrameTime_).count();
    lastFrameTime_ = now;

    if (deltaTime > 0.033f) deltaTime = 0.033f;

    game_.update(deltaTime);

    if (game_.getState() == GameState::GAME_OVER && !wasGameOver_) {
        wasGameOver_ = true;
        triggerGameOver();
    }

    updateRenderArea();

    if (shaderNeedsNewProjectionMatrix_) {
        float projectionMatrix[16] = {0};
        Utility::buildOrthographicMatrix(
                projectionMatrix,
                kProjectionHalfHeight,
                float(width_) / height_,
                kProjectionNearPlane,
                kProjectionFarPlane);
        shader_->activate();
        shader_->setProjectionMatrix(projectionMatrix);
        shaderNeedsNewProjectionMatrix_ = false;
    }

    glClearColor(0.01f, 0.01f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_->activate();
    
    float aspect = float(width_) / height_;
    float worldHalfHeight = kProjectionHalfHeight;
    float worldHalfWidth = worldHalfHeight * aspect;

    const auto& snake = game_.getSnake();
    static Vector2f lastCamPos = {60.0f, 40.0f};
    if (!snake.empty()) {
        lastCamPos = snake[0];
    }
    
    float camX = lastCamPos.x;
    float camY = lastCamPos.y;

    float w = game_.getWorldWidth();
    float h = game_.getWorldHeight();
    float lineThick = 0.4f;
    drawShape(w/2.0f - camX, 0 - camY, w, lineThick, 1.0f, 0.1f, 0.1f, 1.0f, false);
    drawShape(w/2.0f - camX, h - camY, w, lineThick, 1.0f, 0.1f, 0.1f, 1.0f, false);
    drawShape(0 - camX, h/2.0f - camY, lineThick, h, 1.0f, 0.1f, 0.1f, 1.0f, false);
    drawShape(w - camX, h/2.0f - camY, lineThick, h, 1.0f, 0.1f, 0.1f, 1.0f, false);

    for (const auto& food : game_.getFoods()) {
        drawShape(food.x - camX, food.y - camY, 0.8f, 0.8f, 1.0f, 0.9f, 0.2f, 1.0f, true);
    }

    for (size_t i = 0; i < snake.size(); ++i) {
        float intensity = 1.0f - (static_cast<float>(i) / snake.size() * 0.5f);
        float s = (i == 0) ? 1.5f : 1.1f;
        drawShape(snake[i].x - camX, snake[i].y - camY, s, s, 0.0f, 1.0f * intensity, 1.0f * intensity, 1.0f, true);
    }

    float joyPosX = -worldHalfWidth + 18.0f;
    float joyPosY = -worldHalfHeight + 14.0f;
    joyPixelX_ = (joyPosX / worldHalfWidth + 1.0f) * 0.5f * width_;
    joyPixelY_ = (1.0f - (joyPosY / worldHalfHeight + 1.0f) * 0.5f) * height_;

    drawShape(joyPosX, joyPosY, 13.5f, 13.5f, 1.0f, 1.0f, 1.0f, 0.15f, true);
    drawShape(joyPosX + joystickTiltX_ * 5.0f, joyPosY + joystickTiltY_ * 5.0f, 6.0f, 6.0f, 1.0f, 1.0f, 1.0f, 0.5f, true);

    float boostPosX = worldHalfWidth - 16.0f;
    float boostPosY = -worldHalfHeight + 14.0f;
    float bA = (boostPointerId_ != -1) ? 0.9f : 0.4f;
    drawShape(boostPosX, boostPosY, 10.5f, 10.5f, 1.0f, 0.4f, 0.0f, bA, true);

    eglSwapBuffers(display_, surface_);
}

void Renderer::triggerGameOver() {
    JNIEnv *env;
    bool needsDetach = false;
    if (app_->activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != 0) return;
        needsDetach = true;
    }

    jobject activityObj = app_->activity->javaGameActivity;

    if (activityObj) {
        jclass clazz = env->GetObjectClass(activityObj);
        if (clazz) {
            jmethodID method = env->GetMethodID(clazz, "showGameOverDialog", "(I)V");
            if (method) {
                env->CallVoidMethod(activityObj, method, (jint)game_.getScore());
            }
        }
    }
    
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (needsDetach) {
        app_->activity->vm->DetachCurrentThread();
    }
}

void Renderer::restartGame() {
    game_.reset();
    game_.startGame();
    wasGameOver_ = false;
    pendingRestart_.store(false); 
    joystickTiltX_ = 0; joystickTiltY_ = 0;
    joystickPointerId_ = -1;
    boostPointerId_ = -1;
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

    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        int actionMasked = motionEvent.action & AMOTION_EVENT_ACTION_MASK;
        int pointerIndex = (motionEvent.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        
        if (actionMasked == AMOTION_EVENT_ACTION_DOWN || actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            auto &pointer = motionEvent.pointers[pointerIndex];
            float x = GameActivityPointerAxes_getX(&pointer);
            if (x < width_ * 0.5f && joystickPointerId_ == -1) {
                joystickPointerId_ = pointer.id;
            } else if (x >= width_ * 0.5f && boostPointerId_ == -1) {
                boostPointerId_ = pointer.id;
            }
            
            if (game_.getState() == GameState::GAME_OVER) {
                requestRestart();
            } else if (game_.getState() == GameState::MENU) {
                game_.startGame();
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_MOVE) {
            for (int p = 0; p < motionEvent.pointerCount; p++) {
                auto &pointer = motionEvent.pointers[p];
                if (pointer.id == joystickPointerId_) {
                    float dx = GameActivityPointerAxes_getX(&pointer) - joyPixelX_;
                    float dy = GameActivityPointerAxes_getY(&pointer) - joyPixelY_;
                    float dist = std::sqrt(dx*dx + dy*dy);
                    if (dist > 5.0f) {
                        game_.setRotation(std::atan2(-dy, dx));
                        float maxDist = 180.0f;
                        float clampDist = std::min(dist, maxDist);
                        joystickTiltX_ = (dx / dist) * (clampDist / maxDist);
                        joystickTiltY_ = -(dy / dist) * (clampDist / maxDist);
                    }
                }
            }
        } else if (actionMasked == AMOTION_EVENT_ACTION_UP || actionMasked == AMOTION_EVENT_ACTION_POINTER_UP || actionMasked == AMOTION_EVENT_ACTION_CANCEL) {
            int32_t currentId = motionEvent.pointers[pointerIndex].id;
            if (currentId == joystickPointerId_) {
                joystickPointerId_ = -1;
                joystickTiltX_ = 0; joystickTiltY_ = 0;
            } else if (currentId == boostPointerId_) {
                boostPointerId_ = -1;
            }
        }
    }
    game_.setBoosting(boostPointerId_ != -1);
    android_app_clear_motion_events(inputBuffer);
}

void Renderer::initRenderer() {
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);
    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);
    EGLConfig config = supportedConfigs[0];
    EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);
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

void Renderer::drawShape(float x, float y, float sx, float sy, float r, float g, float b, float a, bool isCircle) {
    if (!shader_ || models_.empty()) return;
    glUniform3f(glGetUniformLocation(shader_->getProgram(), "uOffset"), x, y, 0.0f);
    glUniform2f(glGetUniformLocation(shader_->getProgram(), "uScale"), sx, sy);
    glUniform4f(glGetUniformLocation(shader_->getProgram(), "uColor"), r, g, b, a);
    glUniform1i(glGetUniformLocation(shader_->getProgram(), "uIsCircle"), isCircle ? 1 : 0);
    GLint posAttr = shader_->getPosition();
    GLint uvAttr = shader_->getUV();
    const auto& model = models_[0];
    glVertexAttribPointer(posAttr, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), model.getVertexData());
    glEnableVertexAttribArray(posAttr);
    if (uvAttr != -1) {
        glVertexAttribPointer(uvAttr, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (uint8_t*)model.getVertexData() + sizeof(Vector3));
        glEnableVertexAttribArray(uvAttr);
    }
    glDrawElements(GL_TRIANGLES, model.getIndexCount(), GL_UNSIGNED_SHORT, model.getIndexData());
}
