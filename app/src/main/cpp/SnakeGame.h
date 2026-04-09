#ifndef NEON_SNAKE_GAME_H
#define NEON_SNAKE_GAME_H

#include <vector>
#include <random>
#include <chrono>
#include <cmath>

enum class GameState {
    START_SCREEN,
    MODE_SELECTION,
    PLAYING,
    PAUSED,
    STORE,
    SKIN_INVENTORY,
    GAME_OVER
};

struct Vector2f {
    float x;
    float y;

    Vector2f operator+(const Vector2f& other) const { return {x + other.x, y + other.y}; }
    Vector2f operator-(const Vector2f& other) const { return {x - other.x, y - other.y}; }
    Vector2f operator*(float s) const { return {x * s, y * s}; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Food {
    Vector2f pos;
    bool isDropped;
    int colorType;
    int value; // --- 核心新增：记录食物的质量价值 ---
};

enum class PowerUpType {
    SPEED,
    SHIELD,
    MAGNET
};

struct PowerUp {
    Vector2f pos;
    PowerUpType type;
};

class SnakeGame {
public:
    SnakeGame(float worldWidth, float worldHeight);

    void update(float deltaTime);
    void setRotation(float angle);
    float getRotation() const { return rotation_; }
    void setBoosting(bool boosting);

    void setEquippedSkin(int skinId) { equippedSkin_ = skinId; }

    void reset();

    const std::vector<Vector2f>& getSnake() const { return snake_; }
    const std::vector<Food>& getFoods() const { return foods_; }
    const std::vector<PowerUp>& getPowerUps() const { return powerUps_; }
    int getScore() const { return score_; }
    GameState getState() const { return state_; }
    void setState(GameState s) { state_ = s; }
    void startGame() { state_ = GameState::PLAYING; }
    bool hasShield() const { return shieldTimer_ > 0.0f; }

    float getWorldWidth() const { return worldWidth_; }
    float getWorldHeight() const { return worldHeight_; }

private:
    void spawnFood();
    void spawnPowerUp();
    void move(float deltaTime);
    bool checkCollisionWithSelf() const;

    float worldWidth_;
    float worldHeight_;
    std::vector<Vector2f> snake_;
    std::vector<Vector2f> pathHistory_;

    float rotation_;
    bool isBoosting_;

    std::vector<Food> foods_;
    std::vector<PowerUp> powerUps_;

    int score_;
    GameState state_;

    int equippedSkin_ = 0;

    float baseSpeed_;
    float boostMultiplier_;
    float segmentDistance_;

    float pendingGrowth_;
    float pendingFoodLoss_;

    float speedTimer_;
    float shieldTimer_;
    float magnetTimer_;

    std::mt19937 rng_;
};

#endif // NEON_SNAKE_GAME_H