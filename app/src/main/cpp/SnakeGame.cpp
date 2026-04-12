#include "SnakeGame.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
constexpr int kNumAiSnakes = 5;
constexpr int kMaxAiSnakes = 12;
constexpr float kAiSpawnIntervalSec = 3.0f;
constexpr int kAiMinSegments = 3;
constexpr int kAiMaxSegments = 22;
constexpr float kMinSpawnSeparation = 14.0f;

static const char* const kBotNamePool[] = {
        u8"\u75be\u98ce", u8"\u8d64\u7130", u8"\u6697\u5f71", u8"\u51b0\u7259", u8"\u96f7\u9e23",
        u8"\u5e7b\u5149", u8"\u661f\u5c51", u8"\u9010\u6708", u8"\u72c2\u6f9c", u8"\u6714\u96ea",
        u8"\u8ffd\u98ce\u5c11\u5e74", u8"\u6697\u591c\u730e\u624b", u8"\u9713\u8679\u9a7e\u9a76\u5458",
        u8"\u91cf\u5b50\u541e\u566c\u8005", u8"\u661f\u5c18\u6d41\u6d6a\u8005", u8"\u8d5b\u535a\u8d2a\u5403\u86c7",
        u8"\u673a\u68b0\u89c9\u9192\u8005", u8"\u6781\u5bd2\u730e\u98df\u8005", u8"\u539f\u5b50\u72c2\u60f3\u66f2",
        u8"\u8d85\u7a7a\u6f2b\u6e38\u8005", u8"\u6df1\u6d77\u6f2b\u6b65\u8005", u8"\u70c8\u7130\u5f81\u670d\u8005",
        u8"\u5e7d\u5f71\u523a\u5ba2", u8"\u78a7\u6ce2\u6f02\u6d41\u8005", u8"\u7d2b\u7535\u7a7f\u68ad\u8005",
        u8"\u91d1\u9cde\u7834\u98ce\u8005", u8"\u94f6\u6cb3\u5de1\u822a\u5458", u8"\u9ed1\u6d1e\u8fb9\u7f18\u5ba2",
        u8"\u5149\u901f\u5c0f\u961f\u957f", u8"\u5f02\u661f\u79fb\u6c11", u8"\u6570\u636e\u6d41\u6d6a\u86c7",
        u8"\u795e\u79d8\u89c2\u5bdf\u8005", u8"\u65f6\u7a7a\u9519\u4f4d\u8005", u8"\u865a\u7a7a\u6f2b\u6b65\u8005",
        u8"\u5fae\u5149\u95ea\u7535\u86c7", u8"\u5bd2\u971c\u4e4b\u5fc3", u8"\u70c8\u9633\u4e4b\u5f71",
        u8"\u5e7d\u6f6d\u5b88\u671b\u8005", u8"\u78a7\u6d9b\u4e4b\u5b50", u8"\u9713\u8679\u8fb9\u7f18",
        u8"\u91cf\u5b50\u8df3\u8dc3\u8005", u8"\u8109\u51b2\u5c0f\u80fd\u624b", u8"\u6da1\u6da1\u4e0d\u5c45",
        u8"\u6d41\u661f\u5212\u8fc7", u8"\u6781\u5ba2\u65e0\u540d", u8"\u591c\u9e20\u4e4b\u773c",
        u8"\u5e7d\u7075\u6f2b\u6b65", u8"\u5bd2\u971c\u4e4b\u5203", u8"\u8d64\u7130\u957f\u6b4c",
        u8"\u51b0\u7259\u5c11\u4e3b", u8"\u96f7\u9e23\u4e4b\u5b50", u8"\u5e7b\u5149\u5b88\u671b",
        u8"\u661f\u5c51\u96c6\u5408", u8"\u9010\u6708\u8005", u8"\u72c2\u6f9c\u4e4b\u5fc3",
};
static constexpr int kBotNameCount = static_cast<int>(sizeof(kBotNamePool) / sizeof(kBotNamePool[0]));

float wrapAngle(float a) {
    while (a > M_PI) a -= static_cast<float>(2.0 * M_PI);
    while (a < -M_PI) a += static_cast<float>(2.0 * M_PI);
    return a;
}

float lerpAngle(float from, float to, float t) {
    return from + wrapAngle(to - from) * t;
}
}

void SnakeGame::followSegments(std::vector<Vector2f>& segments, float segmentDistance) {
    for (size_t i = 1; i < segments.size(); ++i) {
        Vector2f& current = segments[i];
        Vector2f& prev = segments[i - 1];
        float dx = current.x - prev.x;
        float dy = current.y - prev.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > segmentDistance) {
            float ratio = segmentDistance / dist;
            current.x = prev.x + dx * ratio;
            current.y = prev.y + dy * ratio;
        }
    }
}

SnakeGame::SnakeGame(float worldWidth, float worldHeight)
        : worldWidth_(worldWidth), worldHeight_(worldHeight), rng_(std::chrono::system_clock::now().time_since_epoch().count()) {
    reset();
}

void SnakeGame::reset() {
    snake_.clear();
    pathHistory_.clear();
    aiSnakes_.clear();

    Vector2f head = {worldWidth_ / 2.0f, worldHeight_ / 2.0f};
    snake_.push_back(head);

    for (int i = 1; i < 5; ++i) {
        snake_.push_back({head.x, head.y - i * 0.5f});
    }

    rotation_ = M_PI / 2.0f;
    isBoosting_ = false;
    score_ = 0;
    playerKillCount_ = 0;
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

    if (endlessArenaMode_) {
        spawnAISnakes();
        aiSpawnTimer_ = kAiSpawnIntervalSec;
    } else {
        aiSpawnTimer_ = 0.0f;
    }
}

void SnakeGame::spawnAISnakes() {
    aiSnakes_.clear();
    for (int i = 0; i < kNumAiSnakes; ++i) {
        spawnOneAISnake();
    }
}

void SnakeGame::spawnOneAISnake() {
    if (snake_.empty()) return;
    if (aiSnakes_.size() >= static_cast<size_t>(kMaxAiSnakes)) return;

    std::uniform_real_distribution<float> distX(8.0f, worldWidth_ - 8.0f);
    std::uniform_real_distribution<float> distY(8.0f, worldHeight_ - 8.0f);

    auto tooCloseToAnything = [&](Vector2f p) -> bool {
        if ((p - snake_.front()).length() < kMinSpawnSeparation) return true;
        for (const auto& ai : aiSnakes_) {
            if (!ai.segments.empty() && (p - ai.segments.front()).length() < kMinSpawnSeparation * 0.85f) return true;
        }
        return false;
    };

    AISnake ai;
    const int slot = static_cast<int>(aiSnakes_.size());
    ai.paletteId = slot % 6;
    ai.score = 0;
    ai.pendingGrowth = 0.0f;
    ai.wanderTimer = 0.0f;

    Vector2f pos{};
    bool ok = false;
    for (int attempt = 0; attempt < 80; ++attempt) {
        pos = {distX(rng_), distY(rng_)};
        if (!tooCloseToAnything(pos)) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        pos = {15.0f + static_cast<float>(slot % 9) * 11.0f, 12.0f + static_cast<float>(slot % 6) * 9.0f};
    }

    std::uniform_int_distribution<int> segCountDist(kAiMinSegments, kAiMaxSegments);
    const int nSeg = segCountDist(rng_);
    ai.segments.push_back(pos);
    for (int j = 1; j < nSeg; ++j) {
        ai.segments.push_back({pos.x, pos.y - j * 0.5f});
    }
    std::uniform_real_distribution<float> ang(0.0f, static_cast<float>(2.0 * M_PI));
    ai.rotation = ang(rng_);
    std::uniform_int_distribution<int> namePick(0, kBotNameCount - 1);
    ai.displayName = kBotNamePool[namePick(rng_)];
    aiSnakes_.push_back(std::move(ai));
}

void SnakeGame::tickPeriodicAISpawn(float deltaTime) {
    if (!endlessArenaMode_ || state_ != GameState::PLAYING) return;
    aiSpawnTimer_ -= deltaTime;
    if (aiSpawnTimer_ > 0.0f) return;
    aiSpawnTimer_ = kAiSpawnIntervalSec;
    if (aiSnakes_.size() < static_cast<size_t>(kMaxAiSnakes)) {
        spawnOneAISnake();
    }
}

void SnakeGame::spawnFood() {
    std::uniform_real_distribution<float> distX(1.0f, worldWidth_ - 1.0f);
    std::uniform_real_distribution<float> distY(1.0f, worldHeight_ - 1.0f);
    std::uniform_int_distribution<int> distColor(0, 5);
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

std::vector<RankEntry> SnakeGame::getLengthLeaderboard() const {
    std::vector<RankEntry> rows;
    rows.push_back({static_cast<int>(snake_.size()), true, -1});
    for (size_t i = 0; i < aiSnakes_.size(); ++i) {
        rows.push_back({static_cast<int>(aiSnakes_[i].segments.size()), false, static_cast<int>(i)});
    }
    std::sort(rows.begin(), rows.end(), [](const RankEntry& a, const RankEntry& b) {
        return a.length > b.length;
    });
    return rows;
}

std::vector<RankPanelRow> SnakeGame::getRankPanelRows() const {
    struct Item {
        int len;
        bool pl;
        int ai;
    };
    std::vector<Item> items;
    items.push_back({static_cast<int>(snake_.size()), true, -1});
    for (size_t i = 0; i < aiSnakes_.size(); ++i) {
        items.push_back({static_cast<int>(aiSnakes_[i].segments.size()), false, static_cast<int>(i)});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.len > b.len; });

    std::vector<RankPanelRow> sorted;
    sorted.reserve(items.size());
    for (size_t k = 0; k < items.size(); ++k) {
        RankPanelRow r;
        r.rank = static_cast<int>(k + 1);
        r.length = items[k].len;
        r.isPlayer = items[k].pl;
        r.aiIndex = items[k].ai;
        sorted.push_back(r);
    }

    const int nShow = std::min(9, static_cast<int>(sorted.size()));
    std::vector<RankPanelRow> out;
    for (int i = 0; i < nShow; ++i) {
        out.push_back(sorted[static_cast<size_t>(i)]);
    }

    bool playerInTop9 = false;
    for (const auto& r : out) {
        if (r.isPlayer) playerInTop9 = true;
    }
    if (!playerInTop9) {
        for (const auto& r : sorted) {
            if (r.isPlayer) {
                out.push_back(r);
                break;
            }
        }
    }
    return out;
}

void SnakeGame::update(float deltaTime) {
    if (state_ != GameState::PLAYING) return;

    if (speedTimer_ > 0.0f) speedTimer_ = std::max(0.0f, speedTimer_ - deltaTime);
    if (shieldTimer_ > 0.0f) shieldTimer_ = std::max(0.0f, shieldTimer_ - deltaTime);
    if (magnetTimer_ > 0.0f) magnetTimer_ = std::max(0.0f, magnetTimer_ - deltaTime);

    move(deltaTime);
    if (state_ == GameState::GAME_OVER) return;

    float scaleBase = 1.0f + std::min(score_ * 0.02f, 2.0f);
    float eatRadius = 1.0f * scaleBase;
    Vector2f head = snake_.front();

    if (magnetTimer_ > 0.0f) {
        float magnetRadius = 4.0f * scaleBase;
        float pullSpeed = 25.0f * scaleBase;
        for (auto& food : foods_) {
            float dx = head.x - food.pos.x;
            float dy = head.y - food.pos.y;
            float dist = std::sqrt(dx * dx + dy * dy);
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

        float foodScale = 1.0f;
        if (it->value > 1) {
            foodScale = 1.0f + std::log10(static_cast<float>(it->value)) * 0.8f;
        }
        float currentFoodRadius = 0.4f * foodScale;

        if (std::sqrt(dx * dx + dy * dy) < eatRadius + currentFoodRadius) {
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
        if (std::sqrt(dx * dx + dy * dy) < eatRadius * 1.5f) {
            if (puIt->type == PowerUpType::SPEED) speedTimer_ = 3.0f;
            else if (puIt->type == PowerUpType::SHIELD) shieldTimer_ = 5.0f;
            else if (puIt->type == PowerUpType::MAGNET) magnetTimer_ = 3.0f;
            puIt = powerUps_.erase(puIt);
        } else {
            ++puIt;
        }
    }

    if (endlessArenaMode_) {
        tickPeriodicAISpawn(deltaTime);
        if (!aiSnakes_.empty()) {
            moveAISnakes(deltaTime);
            aiEatFood();
            checkAIHeadToHeadCollisions();
            checkAIvsPlayerTail();
            checkPlayerVsAI();
            if (state_ == GameState::GAME_OVER) return;
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

void SnakeGame::moveAISnakes(float deltaTime) {
    Vector2f worldCenter = {worldWidth_ * 0.5f, worldHeight_ * 0.5f};

    for (auto& ai : aiSnakes_) {
        if (ai.segments.empty()) continue;

        float scaleBase = 1.0f + std::min(ai.score * 0.02f, 2.0f);
        float speed = baseSpeed_ * 0.9f;
        Vector2f aHead = ai.segments.front();

        ai.wanderTimer -= deltaTime;
        if (ai.wanderTimer <= 0.0f) {
            std::uniform_real_distribution<float> d(-0.35f, 0.35f);
            ai.rotation = wrapAngle(ai.rotation + d(rng_));
            std::uniform_real_distribution<float> wt(0.4f, 1.2f);
            ai.wanderTimer = wt(rng_);
        }

        Food* nearest = nullptr;
        float bestD = 1e9f;
        for (auto& food : foods_) {
            float dx = food.pos.x - aHead.x;
            float dy = food.pos.y - aHead.y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < bestD) {
                bestD = d;
                nearest = &food;
            }
        }

        float desired = ai.rotation;
        if (nearest) {
            desired = std::atan2(nearest->pos.y - aHead.y, nearest->pos.x - aHead.x);
        }

        float margin = 6.0f;
        if (aHead.x < margin || aHead.x > worldWidth_ - margin || aHead.y < margin || aHead.y > worldHeight_ - margin) {
            Vector2f toC = worldCenter - aHead;
            float angCenter = std::atan2(toC.y, toC.x);
            desired = lerpAngle(desired, angCenter, 0.65f);
        }

        ai.rotation = lerpAngle(ai.rotation, desired, std::min(1.0f, 3.2f * deltaTime));

        Vector2f dir = {std::cos(ai.rotation), std::sin(ai.rotation)};
        Vector2f nextHead = aHead + dir * speed * deltaTime;

        if (nextHead.x < 0.5f) nextHead.x = 0.5f;
        else if (nextHead.x > worldWidth_ - 0.5f) nextHead.x = worldWidth_ - 0.5f;
        if (nextHead.y < 0.5f) nextHead.y = 0.5f;
        else if (nextHead.y > worldHeight_ - 0.5f) nextHead.y = worldHeight_ - 0.5f;

        ai.segments[0] = nextHead;
        float currentSegDist = 0.5f * scaleBase;
        followSegments(ai.segments, currentSegDist);
    }
}

void SnakeGame::aiEatFood() {
    for (auto& ai : aiSnakes_) {
        if (ai.segments.empty()) continue;
        Vector2f aHead = ai.segments.front();
        float scaleBase = 1.0f + std::min(ai.score * 0.02f, 2.0f);
        float eatRadius = 0.95f * scaleBase;

        auto it = foods_.begin();
        while (it != foods_.end()) {
            float foodScale = 1.0f;
            if (it->value > 1) {
                foodScale = 1.0f + std::log10(static_cast<float>(it->value)) * 0.8f;
            }
            float currentFoodRadius = 0.4f * foodScale;
            float dx = aHead.x - it->pos.x;
            float dy = aHead.y - it->pos.y;
            if (std::sqrt(dx * dx + dy * dy) < eatRadius + currentFoodRadius) {
                ai.score += it->value;
                float growthAmount = (float)it->value / scaleBase;
                ai.pendingGrowth += growthAmount;
                while (ai.pendingGrowth >= 1.0f) {
                    ai.segments.push_back(ai.segments.back());
                    ai.pendingGrowth -= 1.0f;
                }
                it = foods_.erase(it);
                if (foods_.size() < 90) spawnFood();
            } else {
                ++it;
            }
        }
    }
}

void SnakeGame::spawnFoodFromDeadAI(const AISnake& ai) {
    if (ai.segments.empty()) return;
    int totalMass = std::max(ai.score, static_cast<int>(ai.segments.size()));
    const int maxPellets = 40;
    int num = std::min(maxPellets, std::max(4, static_cast<int>(ai.segments.size()) / 2));
    if (num > static_cast<int>(ai.segments.size())) num = static_cast<int>(ai.segments.size());
    if (num < 1) num = 1;
    int baseVal = totalMass / num;
    int remainder = totalMass % num;
    int colorKey = ai.paletteId % 8;

    for (int k = 0; k < num; ++k) {
        size_t si = 0;
        if (num == 1) {
            si = ai.segments.size() / 2;
        } else {
            si = static_cast<size_t>((static_cast<int>(ai.segments.size()) - 1) * k / (num - 1));
        }
        int v = baseVal + (k < remainder ? 1 : 0);
        if (v < 1) v = 1;
        if (foods_.size() < 320) {
            foods_.push_back({ai.segments[si], true, colorKey, v});
        }
    }
}

void SnakeGame::checkAIHeadToHeadCollisions() {
    bool removed = true;
    while (removed) {
        removed = false;
        for (size_t i = 0; i < aiSnakes_.size() && !removed; ++i) {
            for (size_t j = i + 1; j < aiSnakes_.size() && !removed; ++j) {
                const AISnake& A = aiSnakes_[i];
                const AISnake& B = aiSnakes_[j];
                if (A.segments.empty() || B.segments.empty()) continue;

                float dx = A.segments[0].x - B.segments[0].x;
                float dy = A.segments[0].y - B.segments[0].y;
                float dist = std::sqrt(dx * dx + dy * dy);

                float sa = 1.0f + std::min(A.score * 0.02f, 2.0f);
                float sb = 1.0f + std::min(B.score * 0.02f, 2.0f);
                float ra = 0.45f * sa;
                float rb = 0.45f * sb;
                if (dist >= (ra + rb) * 1.15f) continue;

                const int lenA = static_cast<int>(A.segments.size());
                const int lenB = static_cast<int>(B.segments.size());

                if (lenA < lenB) {
                    spawnFoodFromDeadAI(aiSnakes_[i]);
                    aiSnakes_.erase(aiSnakes_.begin() + static_cast<std::ptrdiff_t>(i));
                    removed = true;
                } else if (lenB < lenA) {
                    spawnFoodFromDeadAI(aiSnakes_[j]);
                    aiSnakes_.erase(aiSnakes_.begin() + static_cast<std::ptrdiff_t>(j));
                    removed = true;
                } else {
                    spawnFoodFromDeadAI(aiSnakes_[j]);
                    aiSnakes_.erase(aiSnakes_.begin() + static_cast<std::ptrdiff_t>(j));
                    removed = true;
                }
            }
        }
    }
}

void SnakeGame::checkAIvsPlayerTail() {
    if (snake_.size() < 2) return;

    float pScale = 1.0f + std::min(score_ * 0.02f, 2.0f);
    for (size_t aiIdx = 0; aiIdx < aiSnakes_.size();) {
        AISnake& ai = aiSnakes_[aiIdx];
        if (ai.segments.empty()) {
            ++aiIdx;
            continue;
        }
        Vector2f aHead = ai.segments.front();
        float aiScale = 1.0f + std::min(ai.score * 0.02f, 2.0f);
        float headR = 0.45f * aiScale;

        bool dead = false;
        for (size_t pi = 1; pi < snake_.size(); ++pi) {
            float segR = 0.38f * pScale;
            float dx = aHead.x - snake_[pi].x;
            float dy = aHead.y - snake_[pi].y;
            if (std::sqrt(dx * dx + dy * dy) < headR + segR) {
                dead = true;
                break;
            }
        }
        if (dead) {
            spawnFoodFromDeadAI(ai);
            aiSnakes_.erase(aiSnakes_.begin() + static_cast<std::ptrdiff_t>(aiIdx));
            ++playerKillCount_;
            continue;
        }
        ++aiIdx;
    }
}

void SnakeGame::checkPlayerVsAI() {
    if (shieldTimer_ > 0.0f || snake_.empty()) return;
    Vector2f pHead = snake_.front();
    float pScale = 1.0f + std::min(score_ * 0.02f, 2.0f);
    float pRad = 0.42f * pScale;
    const int plen = static_cast<int>(snake_.size());

    for (size_t aiIdx = 0; aiIdx < aiSnakes_.size();) {
        const AISnake& ai = aiSnakes_[aiIdx];
        if (ai.segments.empty()) {
            ++aiIdx;
            continue;
        }
        float aiScale = 1.0f + std::min(ai.score * 0.02f, 2.0f);

        float dx0 = pHead.x - ai.segments[0].x;
        float dy0 = pHead.y - ai.segments[0].y;
        float headCombined = pRad + 0.45f * aiScale * 1.15f;
        if (std::sqrt(dx0 * dx0 + dy0 * dy0) < headCombined) {
            const int alen = static_cast<int>(ai.segments.size());
            if (plen < alen) {
                state_ = GameState::GAME_OVER;
                return;
            }
            spawnFoodFromDeadAI(aiSnakes_[aiIdx]);
            aiSnakes_.erase(aiSnakes_.begin() + static_cast<std::ptrdiff_t>(aiIdx));
            ++playerKillCount_;
            continue;
        }

        for (size_t i = 1; i < ai.segments.size(); ++i) {
            float dx = pHead.x - ai.segments[i].x;
            float dy = pHead.y - ai.segments[i].y;
            float r = pRad + 0.38f * aiScale;
            if (std::sqrt(dx * dx + dy * dy) < r) {
                state_ = GameState::GAME_OVER;
                return;
            }
        }
        ++aiIdx;
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

            int dropValue = 1 + score_ / 30;
            if (score_ < dropValue) dropValue = score_;
            score_ -= dropValue;

            if (foods_.size() < 300 && !snake_.empty()) {
                foods_.push_back({snake_.back(), true, equippedSkin_, dropValue});
            }

            float scaleBaseLoss = 1.0f + std::min(score_ * 0.02f, 2.0f);
            float lengthLoss = (float)dropValue / scaleBaseLoss;
            pendingGrowth_ -= lengthLoss;

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
    followSegments(snake_, currentSegDist);

    if (shieldTimer_ <= 0.0f && !endlessArenaMode_) {
        for (size_t i = 10; i < snake_.size(); ++i) {
            float dx = nextHead.x - snake_[i].x;
            float dy = nextHead.y - snake_[i].y;

            if (std::sqrt(dx * dx + dy * dy) < collisionRadius) {
                state_ = GameState::GAME_OVER;
                return;
            }
        }
    }
}

bool SnakeGame::checkCollisionWithSelf() const {
    return false;
}
