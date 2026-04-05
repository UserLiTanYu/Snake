#include "SnakeGame.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SnakeGame::SnakeGame(float worldWidth, float worldHeight)
    : worldWidth_(worldWidth), worldHeight_(worldHeight), rng_(std::chrono::system_clock::now().time_since_epoch().count()) {
    reset();
}

void SnakeGame::reset() {
    snake_.clear();
    pathHistory_.clear();
    
    Vector2f head = {worldWidth_ / 2.0f, worldHeight_ / 2.0f};
    snake_.push_back(head);
    
    // Initial tail
    for (int i = 1; i < 5; ++i) {
        snake_.push_back({head.x, head.y - i * 0.5f});
    }
    
    rotation_ = M_PI / 2.0f; // Initial direction: UP
    isBoosting_ = false;
    score_ = 0;
    state_ = GameState::MENU;
    baseSpeed_ = 5.0f;
    boostMultiplier_ = 2.0f;
    segmentDistance_ = 0.5f;
    
    foods_.clear();
    for (int i = 0; i < 5; ++i) spawnFood();
}

void SnakeGame::spawnFood() {
    std::uniform_real_distribution<float> distX(1.0f, worldWidth_ - 1.0f);
    std::uniform_real_distribution<float> distY(1.0f, worldHeight_ - 1.0f);
    foods_.push_back({distX(rng_), distY(rng_)});
}

void SnakeGame::setRotation(float angle) {
    rotation_ = angle;
}

void SnakeGame::setBoosting(bool boosting) {
    isBoosting_ = boosting;
}

void SnakeGame::update(float deltaTime) {
    if (state_ != GameState::PLAYING) return;
    
    move(deltaTime);
    
    // Food collision
    Vector2f head = snake_.front();
    auto it = foods_.begin();
    while (it != foods_.end()) {
        float dx = head.x - it->x;
        float dy = head.y - it->y;
        if (std::sqrt(dx*dx + dy*dy) < 1.0f) {
            score_++;
            it = foods_.erase(it);
            // Add a segment (just grow by duplicate for now, it'll smooth out)
            snake_.push_back(snake_.back());
            if (foods_.size() < 3) spawnFood();
        } else {
            ++it;
        }
    }
    
    if (foods_.size() < 5 && std::uniform_real_distribution<float>(0, 1)(rng_) < 0.01f) {
        spawnFood();
    }
}

void SnakeGame::move(float deltaTime) {
    float speed = baseSpeed_;
    if (isBoosting_) speed *= boostMultiplier_;
    
    Vector2f head = snake_.front();
    Vector2f direction = {std::cos(rotation_), std::sin(rotation_)};
    Vector2f nextHead = head + direction * speed * deltaTime;
    
    // Wall collision
    if (nextHead.x < 0 || nextHead.x > worldWidth_ || nextHead.y < 0 || nextHead.y > worldHeight_) {
        state_ = GameState::GAME_OVER;
        return;
    }

    // Smooth body trailing logic
    // We update segments to follow the one in front at a fixed distance
    snake_[0] = nextHead;
    for (size_t i = 1; i < snake_.size(); ++i) {
        Vector2f& current = snake_[i];
        Vector2f& prev = snake_[i-1];
        float dx = current.x - prev.x;
        float dy = current.y - prev.y;
        float dist = std::sqrt(dx*dx + dy*dy);
        if (dist > segmentDistance_) {
            float ratio = segmentDistance_ / dist;
            current.x = prev.x + dx * ratio;
            current.y = prev.y + dy * ratio;
        }
    }
    
    // Self-collision (skip head and nearby segments)
    for (size_t i = 10; i < snake_.size(); ++i) {
        float dx = nextHead.x - snake_[i].x;
        float dy = nextHead.y - snake_[i].y;
        if (std::sqrt(dx*dx + dy*dy) < 0.4f) {
            state_ = GameState::GAME_OVER;
            return;
        }
    }
}
