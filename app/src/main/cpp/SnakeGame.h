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

class SnakeGame {
public:
    SnakeGame(float worldWidth, float worldHeight);

    void update(float deltaTime);
    void setRotation(float angle); // 0 to 2*PI
    void setBoosting(bool boosting);
    void reset();

    const std::vector<Vector2f>& getSnake() const { return snake_; }
    const std::vector<Vector2f>& getFoods() const { return foods_; }
    int getScore() const { return score_; }
    GameState getState() const { return state_; }
    void setState(GameState s) { state_ = s; }
    void startGame() { state_ = GameState::PLAYING; }

    float getWorldWidth() const { return worldWidth_; }
    float getWorldHeight() const { return worldHeight_; }

private:
    void spawnFood();
    void move(float deltaTime);
    bool checkCollisionWithSelf() const;

    float worldWidth_;
    float worldHeight_;
    std::vector<Vector2f> snake_;
    std::vector<Vector2f> pathHistory_; // Used for smooth body trailing
    
    float rotation_; // Current movement angle
    bool isBoosting_;
    
    std::vector<Vector2f> foods_;
    int score_;
    GameState state_;

    float baseSpeed_;
    float boostMultiplier_;
    float segmentDistance_; // Distance between snake segments
    
    std::mt19937 rng_;
};

#endif // NEON_SNAKE_GAME_H
