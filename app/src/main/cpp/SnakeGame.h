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

struct AISnake {
    std::vector<Vector2f> segments;
    float rotation = 0.0f;
    int paletteId = 0;
    int score = 0;
    float pendingGrowth = 0.0f;
    float wanderTimer = 0.0f;
};

struct RankEntry {
    int length = 0;
    bool isPlayer = false;
    int aiIndex = -1;
};

class SnakeGame {
public:
    SnakeGame(float worldWidth, float worldHeight);

    void update(float deltaTime);
    void setRotation(float angle);
    float getRotation() const { return rotation_; }
    void setBoosting(bool boosting);

    void setEquippedSkin(int skinId) { equippedSkin_ = skinId; }
    void setEndlessArenaMode(bool enabled) { endlessArenaMode_ = enabled; }
    bool isEndlessArenaMode() const { return endlessArenaMode_; }

    void reset();

    const std::vector<Vector2f>& getSnake() const { return snake_; }
    const std::vector<AISnake>& getAISnakes() const { return aiSnakes_; }
    const std::vector<Food>& getFoods() const { return foods_; }
    const std::vector<PowerUp>& getPowerUps() const { return powerUps_; }
    int getScore() const { return score_; }
    GameState getState() const { return state_; }
    void setState(GameState s) { state_ = s; }
    void startGame() { state_ = GameState::PLAYING; }
    bool hasShield() const { return shieldTimer_ > 0.0f; }

    float getWorldWidth() const { return worldWidth_; }
    float getWorldHeight() const { return worldHeight_; }

    std::vector<RankEntry> getLengthLeaderboard() const;
    int getTotalSnakeCount() const { return 1 + static_cast<int>(aiSnakes_.size()); }

private:
    void spawnFood();
    void spawnPowerUp();
    void spawnAISnakes();
    void spawnOneAISnake();
    void tickPeriodicAISpawn(float deltaTime);
    void move(float deltaTime);
    void moveAISnakes(float deltaTime);
    void aiEatFood();
    void checkAIvsPlayerTail();
    void checkAIHeadToHeadCollisions();
    void checkPlayerVsAI();
    void spawnFoodFromDeadAI(const AISnake& ai);
    static void followSegments(std::vector<Vector2f>& segments, float segmentDistance);
    bool checkCollisionWithSelf() const;

    float worldWidth_;
    float worldHeight_;
    std::vector<Vector2f> snake_;
    std::vector<Vector2f> pathHistory_;

    float rotation_;
    bool isBoosting_;

    std::vector<Food> foods_;
    std::vector<PowerUp> powerUps_;
    std::vector<AISnake> aiSnakes_;
    bool endlessArenaMode_ = false;
    float aiSpawnTimer_ = 0.0f;

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