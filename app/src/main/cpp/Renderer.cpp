#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *vertex = R"vertex(#version 300 es
in vec3 inPosition;
in vec2 inUV;
out vec2 fragUV;
uniform mat4 uProjection;
uniform vec3 uOffset;
uniform vec2 uScale; // sx, sy separately

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

Renderer::~Renderer() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
    }
}

void Renderer::render() {
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastFrameTime_).count();
    lastFrameTime_ = now;

    if (deltaTime > 0.05f) deltaTime = 0.05f;

    game_.update(deltaTime);
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

    // 地图底色：保持一致的深蓝黑色
    glClearColor(0.015f, 0.015f, 0.025f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_->activate();
    
    float aspect = float(width_) / height_;
    float worldHalfHeight = kProjectionHalfHeight;
    float worldHalfWidth = worldHalfHeight * aspect;

    const auto& snake = game_.getSnake();
    Vector2f head = snake.empty() ? Vector2f{game_.getWorldWidth()/2, game_.getWorldHeight()/2} : snake[0];
    
    float camX = head.x;
    float camY = head.y;

    // --- 渲染边界 (红线) ---
    float w = game_.getWorldWidth();
    float h = game_.getWorldHeight();
    float lineThick = 0.3f;
    // 渲染地图底板（可选，目前底色一致，这里通过边界线界定）
    // 底边
    drawShape(w/2.0f - camX, 0 - camY, w, lineThick, 1.0f, 0.0f, 0.0f, 1.0f, false);
    // 顶边
    drawShape(w/2.0f - camX, h - camY, w, lineThick, 1.0f, 0.0f, 0.0f, 1.0f, false);
    // 左边
    drawShape(0 - camX, h/2.0f - camY, lineThick, h, 1.0f, 0.0f, 0.0f, 1.0f, false);
    // 右边
    drawShape(w - camX, h/2.0f - camY, lineThick, h, 1.0f, 0.0f, 0.0f, 1.0f, false);

    // --- 渲染食物 ---
    const auto& foods = game_.getFoods();
    for (const auto& food : foods) {
        drawShape(food.x - camX, food.y - camY, 0.8f, 0.8f, 1.0f, 0.8f, 0.0f, 1.0f, true);
    }

    // --- 渲染蛇身 ---
    for (size_t i = 0; i < snake.size(); ++i) {
        float intensity = 1.0f - (static_cast<float>(i) / snake.size() * 0.5f);
        float s = (i == 0) ? 1.3f : 1.0f;
        drawShape(snake[i].x - camX, snake[i].y - camY, s, s, 0.0f, 1.0f * intensity, 1.0f * intensity, 1.0f, true);
    }

    // --- 渲染 UI (固定屏幕空间) ---
    // 计算 UI 中心点的像素位置，用于精准输入逻辑
    float joyPosX = -worldHalfWidth + 12.0f;
    float joyPosY = -worldHalfHeight + 10.0f;
    joyPixelX_ = (joyPosX / worldHalfWidth + 1.0f) * 0.5f * width_;
    joyPixelY_ = (1.0f - (joyPosY / worldHalfHeight + 1.0f) * 0.5f) * height_;

    // 绘制外圈 (圆形)
    drawShape(joyPosX, joyPosY, 9.0f, 9.0f, 1.0f, 1.0f, 1.0f, 0.15f, true);
    // 绘制内球 (圆形 + 随倾斜偏移)
    drawShape(joyPosX + joystickTiltX_ * 3.5f, joyPosY + joystickTiltY_ * 3.5f, 4.0f, 4.0f, 1.0f, 1.0f, 1.0f, 0.4f, true);

    float boostPosX = worldHalfWidth - 10.0f;
    float boostPosY = -worldHalfHeight + 10.0f;
    float bA = (boostPointerId_ != -1) ? 0.9f : 0.4f;
    drawShape(boostPosX, boostPosY, 7.0f, 7.0f, 1.0f, 0.4f, 0.0f, bA, true);

    eglSwapBuffers(display_, surface_);
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
    for (int i = 0; i < numConfigs; ++i) {
        EGLint r, g, b;
        eglGetConfigAttrib(display, supportedConfigs[i], EGL_RED_SIZE, &r);
        eglGetConfigAttrib(display, supportedConfigs[i], EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(display, supportedConfigs[i], EGL_BLUE_SIZE, &b);
        if (r == 8 && g == 8 && b == 8) { config = supportedConfigs[i]; break; }
    }

    EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);

    eglMakeCurrent(display, surface, surface, context);
    display_ = display; surface_ = surface; context_ = context;

    shader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertex, fragment, "inPosition", "inUV", "uProjection"));
    if (shader_) shader_->activate();
    createModels();
}

void Renderer::updateRenderArea() {
    EGLint width, height;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
    if (width != width_ || height != height_) {
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
    glUniform2f(glGetUniformLocation(shader_->getProgram(), "uScale"), sx, sy); // Use separate x/y scale
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

void Renderer::handleInput() {
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) return;

    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        int action = motionEvent.action & AMOTION_EVENT_ACTION_MASK;
        
        for (int p = 0; p < motionEvent.pointerCount; p++) {
            auto &pointer = motionEvent.pointers[p];
            int32_t id = pointer.id;
            float x = GameActivityPointerAxes_getX(&pointer);
            float y = GameActivityPointerAxes_getY(&pointer);

            if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                if (x < width_ / 2.0f && joystickPointerId_ == -1) {
                    joystickPointerId_ = id;
                } else if (x >= width_ / 2.0f && boostPointerId_ == -1) {
                    boostPointerId_ = id;
                }
                if (game_.getState() != GameState::PLAYING) { game_.reset(); game_.startGame(); }
            }

            if (id == joystickPointerId_) {
                // 核心修复：直接使用同步的 joyPixelX/Y 作为输入原点
                float dx = x - joyPixelX_;
                float dy = y - joyPixelY_;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist > 5.0f) {
                    game_.setRotation(std::atan2(-dy, dx));
                    float maxDist = 120.0f;
                    float clampDist = std::min(dist, maxDist);
                    joystickTiltX_ = (dx / dist) * (clampDist / maxDist);
                    joystickTiltY_ = -(dy / dist) * (clampDist / maxDist); // 反转 Y 匹配 UI 绘制逻辑
                }
            }

            if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
                if (id == joystickPointerId_) { joystickPointerId_ = -1; joystickTiltX_ = 0; joystickTiltY_ = 0; }
                else if (id == boostPointerId_) { boostPointerId_ = -1; }
            }
        }
    }
    game_.setBoosting(boostPointerId_ != -1);
    android_app_clear_motion_events(inputBuffer);
    android_app_clear_key_events(inputBuffer);
}
