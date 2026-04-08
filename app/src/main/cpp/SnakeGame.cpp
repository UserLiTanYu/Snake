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

    for (int i = 1; i < 5; ++i) {
        snake_.push_back({head.x, head.y - i * 0.5f});
    }

    rotation_ = M_PI / 2.0f;
    isBoosting_ = false;
    score_ = 0;
    state_ = GameState::START_SCREEN;

    baseSpeed_ = 8.0f;
    boostMultiplier_ = 3.0f;
    segmentDistance_ = 0.5f;

    pendingGrowth_ = 0.0f;
    pendingFoodLoss_ = 0.0f;

    foods_.clear();
    for (int i = 0; i < 75; ++i) spawnFood();
}

void SnakeGame::spawnFood() {
    std::uniform_real_distribution<float> distX(1.0f, worldWidth_ - 1.0f);
    std::uniform_real_distribution<float> distY(1.0f, worldHeight_ - 1.0f);
    std::uniform_int_distribution<int> distColor(0, 5); // --- 新增：随机选取 0 到 5 的颜色编号 ---
    foods_.push_back({{distX(rng_), distY(rng_)}, false, distColor(rng_)});
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

    float scaleBase = 1.0f + std::min(score_ * 0.02f, 2.0f);
    float eatRadius = 1.0f * scaleBase;

    Vector2f head = snake_.front();
    auto it = foods_.begin();
    while (it != foods_.end()) {
        float dx = head.x - it->pos.x;
        float dy = head.y - it->pos.y;
        if (std::sqrt(dx*dx + dy*dy) < eatRadius) {
            score_++;
            it = foods_.erase(it);

            float growthAmount = 1.0f / scaleBase;
            pendingGrowth_ += growthAmount;

            while (pendingGrowth_ >= 1.0f) {
                snake_.push_back(snake_.back());
                pendingGrowth_ -= 1.0f;
            }

            if (foods_.size() < 45) spawnFood();
        } else {
            ++it;
        }
    }

    if (foods_.size() < 75 && std::uniform_real_distribution<float>(0, 1)(rng_) < 0.15f) {
        spawnFood();
    }
}

void SnakeGame::move(float deltaTime) {
    bool actuallyBoosting = false;
    if (isBoosting_ && score_ > 0) {
        actuallyBoosting = true;
    }

    float speed = baseSpeed_;
    if (actuallyBoosting) {
        speed *= boostMultiplier_;

        float foodPenaltyRate = 1.5f;
        pendingFoodLoss_ += foodPenaltyRate * deltaTime;

        while (pendingFoodLoss_ >= 1.0f && score_ > 0) {
            score_--;
            pendingFoodLoss_ -= 1.0f;

            if (foods_.size() < 150 && !snake_.empty()) {
                // 加速掉落的食物，颜色编号占位填 0 即可，渲染时会被 isDropped 覆盖
                foods_.push_back({snake_.back(), true, 0});
            }

            float scaleBase = 1.0f + std::min(score_ * 0.02f, 2.0f);
            float lengthLoss = 1.0f / scaleBase;
            pendingGrowth_ -= lengthLoss;

            while (pendingGrowth_ < 0.0f && snake_.size() > 5) {
                snake_.pop_back();
                pendingGrowth_ += 1.0f;
            }
        }
    }

    float scaleBase = 1.0f + std::min(score_ * 0.02f, 2.0f);
    float currentSegDist = 0.5f * scaleBase;
    float collisionRadius = 0.4f * scaleBase;

    Vector2f head = snake_.front();
    Vector2f direction = {std::cos(rotation_), std::sin(rotation_)};
    Vector2f nextHead = head + direction * speed * deltaTime;

    if (nextHead.x < 0 || nextHead.x > worldWidth_ || nextHead.y < 0 || nextHead.y > worldHeight_) {
        state_ = GameState::GAME_OVER;
        return;
    }

    snake_[0] = nextHead;
    for (size_t i = 1; i < snake_.size(); ++i) {
        Vector2f& current = snake_[i];
        Vector2f& prev = snake_[i-1];
        float dx = current.x - prev.x;
        float dy = current.y - prev.y;
        float dist = std::sqrt(dx*dx + dy*dy);

        if (dist > currentSegDist) {
            float ratio = currentSegDist / dist;
            current.x = prev.x + dx * ratio;
            current.y = prev.y + dy * ratio;
        }
    }

    for (size_t i = 10; i < snake_.size(); ++i) {
        float dx = nextHead.x - snake_[i].x;
        float dy = nextHead.y - snake_[i].y;

        if (std::sqrt(dx*dx + dy*dy) < collisionRadius) {
            state_ = GameState::GAME_OVER;
            return;
        }
    }
}