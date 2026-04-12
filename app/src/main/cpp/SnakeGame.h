#ifndef NEON_SNAKE_GAME_H
#define NEON_SNAKE_GAME_H

#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <cmath>

enum class GameState {
    START_SCREEN,
    MODE_SELECTION,
    MENU_SETTINGS,
    PLAYING,
    PAUSED,
    STORE,
    SKIN_INVENTORY,
    GAME_OVER,
    CHALLENGE_CLEAR,
    CHALLENGE_SELECTION,

};
enum class GameMode {
    ENDLESS,      // 无尽模式
    CHALLENGE_1,   // 挑战关卡 1
    CHALLENGE_2,
    CHALLENGE_3
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
    std::string displayName;
};

struct RankEntry {
    int length = 0;
    bool isPlayer = false;
    int aiIndex = -1;
};

struct RankPanelRow {
    int rank = 0;
    int length = 0;
    bool isPlayer = false;
    int aiIndex = -1;
};

class SnakeGame {
public:
    SnakeGame(float worldWidth, float worldHeight);
    void startChallengeLevel2();
    void startChallengeLevel3();
    void update(float deltaTime);
    void setRotation(float angle);
    float getRotation() const { return rotation_; }
    void setBoosting(bool boosting);
    int getChallengeTarget() const { return challengeTargetScore_; }
    int getChallengeStars(GameMode mode) const;
    int calculateStars(GameMode mode, int score) const;
    void setEquippedSkin(int skinId) { equippedSkin_ = skinId; }
    void setEndlessArenaMode(bool enabled) { endlessArenaMode_ = enabled; }
    bool isEndlessArenaMode() const { return endlessArenaMode_; }
    void startChallengeLevel1();
    void reset();
// 核心新增：保存读取最高分的接口
    void setMaxScore(GameMode mode, int score) {
        if (mode == GameMode::CHALLENGE_1) maxScoreCh1_ = score;
        else if (mode == GameMode::CHALLENGE_2) maxScoreCh2_ = score;
        else if (mode == GameMode::CHALLENGE_3) maxScoreCh3_ = score;
    }
    int getMaxScore(GameMode mode) const {
        if (mode == GameMode::CHALLENGE_1) return maxScoreCh1_;
        else if (mode == GameMode::CHALLENGE_2) return maxScoreCh2_;
        else if (mode == GameMode::CHALLENGE_3) return maxScoreCh3_;
        return 0;
    }

    const std::vector<Vector2f>& getSnake() const { return snake_; }
    const std::vector<AISnake>& getAISnakes() const { return aiSnakes_; }
    const std::vector<Food>& getFoods() const { return foods_; }
    const std::vector<PowerUp>& getPowerUps() const { return powerUps_; }
    int getScore() const { return score_; }
    int getPlayerKillCount() const { return playerKillCount_; }
    GameState getState() const { return state_; }
    void setState(GameState s) { state_ = s; }
    void startGame() { state_ = GameState::PLAYING; }
    bool hasShield() const { return shieldTimer_ > 0.0f; }
    const std::vector<Vector2f>& getWalls() const { return walls_; }
    GameMode getGameMode() const { return currentMode_; }
    float getWorldWidth() const { return worldWidth_; }
    float getWorldHeight() const { return worldHeight_; }

    std::vector<RankEntry> getLengthLeaderboard() const;
    std::vector<RankPanelRow> getRankPanelRows() const;
    GameMode getCurrentMode() const { return currentMode_; }
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
    void checkAIVsAITail(); // AI 互撞身体检测

    int maxScoreCh1_ = 0;
    int maxScoreCh2_ = 0;
    int maxScoreCh3_ = 0;
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
    int playerKillCount_ = 0;
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
    std::vector<Vector2f> walls_;           // 存储所有墙壁的坐标
    GameMode currentMode_ = GameMode::ENDLESS; // 当前游戏模式
    int challengeTargetScore_ = 500;        // 过关目标分数
    float wallRadius_ = 1.0f;               // 墙壁的碰撞判定大小
};

#endif // NEON_SNAKE_GAME_H