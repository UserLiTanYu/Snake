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

    speedTimer_ = 0.0f;
    shieldTimer_ = 0.0f;
    magnetTimer_ = 0.0f;
    powerUps_.clear();

    foods_.clear();
    for (int i = 0; i < 150; ++i) spawnFood();
    for (int i = 0; i < 3; ++i) spawnPowerUp();
}

void SnakeGame::spawnFood() {
    std::uniform_real_distribution<float> distX(1.0f, worldWidth_ - 1.0f);
    std::uniform_real_distribution<float> distY(1.0f, worldHeight_ - 1.0f);
    std::uniform_int_distribution<int> distColor(0, 5);
    // 默认食物价值为 1
    foods_.push_back({{distX(rng_), distY(rng_)}, false, distColor(rng_), 1});
}

void SnakeGame::spawnPowerUp() {
    std::uniform_real_distribution<float> distX(2.0f, worldWidth_ - 2.0f);
    std::uniform_real_distribution<float> distY(2.0f, worldHeight_ - 2.0f);
    std::uniform_int_distribution<int> distType(0, 2);
    powerUps_.push_back({{distX(rng_), distY(rng_)}, static_cast<PowerUpType>(distType(rng_))});
}

void SnakeGame::setRotation(float angle) {
    rotation_ = angle;
}

void SnakeGame::setBoosting(bool boosting) {
    isBoosting_ = boosting;
}

void SnakeGame::update(float deltaTime) {
    if (state_ != GameState::PLAYING) return;

    if (speedTimer_ > 0.0f) speedTimer_ = std::max(0.0f, speedTimer_ - deltaTime);
    if (shieldTimer_ > 0.0f) shieldTimer_ = std::max(0.0f, shieldTimer_ - deltaTime);
    if (magnetTimer_ > 0.0f) magnetTimer_ = std::max(0.0f, magnetTimer_ - deltaTime);

    move(deltaTime);

    float scaleBase = 1.0f + std::min(score_ * 0.02f, 2.0f);
    float eatRadius = 1.0f * scaleBase;
    Vector2f head = snake_.front();

    if (magnetTimer_ > 0.0f) {
        float magnetRadius = 4.0f * scaleBase;
        float pullSpeed = 25.0f * scaleBase;
        for (auto& food : foods_) {
            float dx = head.x - food.pos.x;
            float dy = head.y - food.pos.y;
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist > 0.1f && dist < magnetRadius) {
                food.pos.x += (dx / dist) * pullSpeed * deltaTime;
                food.pos.y += (dy / dist) * pullSpeed * deltaTime;
            }
        }
    }

    auto it = foods_.begin();
    while (it != foods_.end()) {
        float dx = head.x - it->pos.x;
        float dy = head.y - it->pos.y;

        // 核心修改：动态适应大食物的吃取半径
        float foodScale = 1.0f;
        if (it->value > 1) {
            foodScale = 1.0f + std::log10(static_cast<float>(it->value)) * 0.8f;
        }
        float currentFoodRadius = 0.4f * foodScale;

        if (std::sqrt(dx*dx + dy*dy) < eatRadius + currentFoodRadius) {
            // 完美还原失去的质量
            score_ += it->value;
            float growthAmount = (float)it->value / scaleBase;
            pendingGrowth_ += growthAmount;

            while (pendingGrowth_ >= 1.0f) {
                snake_.push_back(snake_.back());
                pendingGrowth_ -= 1.0f;
            }

            it = foods_.erase(it);
            if (foods_.size() < 90) spawnFood();
        } else {
            ++it;
        }
    }

    auto puIt = powerUps_.begin();
    while (puIt != powerUps_.end()) {
        float dx = head.x - puIt->pos.x;
        float dy = head.y - puIt->pos.y;
        if (std::sqrt(dx*dx + dy*dy) < eatRadius * 1.5f) {
            if (puIt->type == PowerUpType::SPEED) speedTimer_ = 3.0f;
            else if (puIt->type == PowerUpType::SHIELD) shieldTimer_ = 5.0f;
            else if (puIt->type == PowerUpType::MAGNET) magnetTimer_ = 3.0f;
            puIt = powerUps_.erase(puIt);
        } else {
            ++puIt;
        }
    }

    if (foods_.size() < 150 && std::uniform_real_distribution<float>(0, 1)(rng_) < 0.15f) {
        spawnFood();
    }

    while (powerUps_.size() < 3) spawnPowerUp();
    if (powerUps_.size() < 5 && std::uniform_real_distribution<float>(0, 1)(rng_) < 0.01f) {
        spawnPowerUp();
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
        float foodPenaltyRate = 1.0f;
        pendingFoodLoss_ += foodPenaltyRate * deltaTime;

        while (pendingFoodLoss_ >= 1.0f && score_ > 0) {
            pendingFoodLoss_ -= 1.0f;

            // 核心修改：蛇越大，排泄的单块质量（Value）越高！
            int dropValue = 1 + score_ / 30;
            if (score_ < dropValue) dropValue = score_;
            score_ -= dropValue;

            if (foods_.size() < 300 && !snake_.empty()) {
                // 将 equippedSkin_ 作为食物颜色记录下来，并带上它的价值！
                foods_.push_back({snake_.back(), true, equippedSkin_, dropValue});
            }

            float scaleBase = 1.0f + std::min(score_ * 0.02f, 2.0f);
            float lengthLoss = (float)dropValue / scaleBase;
            pendingGrowth_ -= lengthLoss;

            // 扣除掉对应的蛇身长度
            while (pendingGrowth_ < 0.0f && snake_.size() > 5) {
                snake_.pop_back();
                pendingGrowth_ += 1.0f;
            }
        }
    } else if (speedTimer_ > 0.0f) {
        speed *= boostMultiplier_;
    }

    float scaleBase = 1.0f + std::min(score_ * 0.02f, 2.0f);
    float currentSegDist = 0.5f * scaleBase;
    float collisionRadius = 0.4f * scaleBase;

    Vector2f head = snake_.front();
    Vector2f direction = {std::cos(rotation_), std::sin(rotation_)};
    Vector2f nextHead = head + direction * speed * deltaTime;

    if (shieldTimer_ > 0.0f) {
        if (nextHead.x < 0) nextHead.x = 0;
        else if (nextHead.x > worldWidth_) nextHead.x = worldWidth_;
        if (nextHead.y < 0) nextHead.y = 0;
        else if (nextHead.y > worldHeight_) nextHead.y = worldHeight_;
    } else {
        if (nextHead.x < 0 || nextHead.x > worldWidth_ || nextHead.y < 0 || nextHead.y > worldHeight_) {
            state_ = GameState::GAME_OVER;
            return;
        }
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

    if (shieldTimer_ <= 0.0f) {
        for (size_t i = 10; i < snake_.size(); ++i) {
            float dx = nextHead.x - snake_[i].x;
            float dy = nextHead.y - snake_[i].y;

            if (std::sqrt(dx*dx + dy*dy) < collisionRadius) {
                state_ = GameState::GAME_OVER;
                return;
            }
        }
    }
}