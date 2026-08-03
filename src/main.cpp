#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 13
#define LED_COUNT 33

#define BTN_BLUE 12
#define BTN_RED 14
#define BTN_YELLOW 27

#define LED_BRIGHTNESS 51

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

constexpr int MAX_BULLETS = 20;
constexpr int TOTAL_ENEMIES = 10;
constexpr int MAX_MINIONS = TOTAL_ENEMIES + MAX_BULLETS;

constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;
constexpr unsigned long BULLET_MOVE_MS = 35;
constexpr unsigned long SPAWN_DELAY_MS = 80;
constexpr unsigned long VICTORY_SWEEP_MS = 25;
constexpr unsigned long VICTORY_FLASH_MS = 90;
constexpr int VICTORY_FLASH_COUNT = 4;

constexpr int STARTING_ENEMY_DELAY = 600;
// LEDs 0-7 are reserved as a physical buffer in front of the player.
constexpr int PLAYER_BUFFER_LEDS = 8;
constexpr int BULLET_START_LED = PLAYER_BUFFER_LEDS;
// Keep the first enemy thirteen LEDs beyond the bullet starting point.
constexpr int ENEMY_START_GAP_LEDS = 13;
constexpr int INITIAL_ENEMY_START_LED = BULLET_START_LED + ENEMY_START_GAP_LEDS;
constexpr int INITIAL_ENEMIES = min(TOTAL_ENEMIES, LED_COUNT - INITIAL_ENEMY_START_LED);

uint32_t COLOR_BLUE;
uint32_t COLOR_RED;
uint32_t COLOR_YELLOW;

enum class GamePhase { Hiatus, Spawning, Playing };

GamePhase gamePhase = GamePhase::Hiatus;
unsigned long lastEnemyMove = 0;
unsigned long lastBulletMove = 0;
unsigned long lastSpawn = 0;
int enemyDelay = STARTING_ENEMY_DELAY;

struct Bullet {
    int pos;
    uint32_t color;
};

Bullet bullets[MAX_BULLETS];
int bulletCount = 0;

struct Minion {
    int pos;
    uint32_t color;
};

Minion minions[MAX_MINIONS];
int minionCount = 0;
int spawnIndex = 0;

struct ButtonState {
    uint8_t pin;
    bool previousReading;
    bool stableState;
    unsigned long lastChange;
};

ButtonState blueButton = {BTN_BLUE, HIGH, HIGH, 0};
ButtonState redButton = {BTN_RED, HIGH, HIGH, 0};
ButtonState yellowButton = {BTN_YELLOW, HIGH, HIGH, 0};

bool buttonPressed(ButtonState &button)
{
    bool reading = digitalRead(button.pin);

    if (reading != button.previousReading)
    {
        button.lastChange = millis();
        button.previousReading = reading;
    }

    if (millis() - button.lastChange >= BUTTON_DEBOUNCE_MS &&
        reading != button.stableState)
    {
        button.stableState = reading;
        return button.stableState == LOW;
    }

    return false;
}

bool anyButtonPressed()
{
    bool bluePressed = buttonPressed(blueButton);
    bool redPressed = buttonPressed(redButton);
    bool yellowPressed = buttonPressed(yellowButton);

    if (bluePressed)
    {
        Serial.println("Blue button");
    }
    if (redPressed)
    {
        Serial.println("Red button");
    }
    if (yellowPressed)
    {
        Serial.println("Yellow button");
    }

    return bluePressed || redPressed || yellowPressed;
}

void rainbow()
{
    static uint16_t offset = 0;
    static unsigned long lastRainbowUpdate = 0;

    if (millis() - lastRainbowUpdate < 20)
    {
        return;
    }

    lastRainbowUpdate = millis();

    for (int i = 0; i < LED_COUNT; i++)
    {
        uint16_t hue = static_cast<uint16_t>(
            (i * 65536UL / LED_COUNT + offset) & 0xFFFF
        );
        strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(
            hue, 255, LED_BRIGHTNESS
        )));
    }

    strip.show();
    offset += 500;
}

void victory()
{
    strip.clear();
    for (int pixel = 0; pixel < LED_COUNT; pixel++)
    {
        strip.setPixelColor(pixel, strip.gamma32(strip.ColorHSV(
            pixel * 65536UL / LED_COUNT, 255, LED_BRIGHTNESS
        )));
        strip.show();
        delay(VICTORY_SWEEP_MS);
    }

    for (int flash = 0; flash < VICTORY_FLASH_COUNT; flash++)
    {
        for (int pixel = 0; pixel < LED_COUNT; pixel++)
        {
            strip.setPixelColor(pixel, strip.gamma32(strip.ColorHSV(
                pixel * 65536UL / LED_COUNT, 255, LED_BRIGHTNESS
            )));
        }
        strip.show();
        delay(VICTORY_FLASH_MS);

        if (flash < VICTORY_FLASH_COUNT - 1)
        {
            strip.clear();
            strip.show();
            delay(VICTORY_FLASH_MS);
        }
    }
}

void resetGame()
{
    enemyDelay = STARTING_ENEMY_DELAY;
    bulletCount = 0;
    minionCount = 0;
    spawnIndex = 0;
    strip.clear();
    strip.show();
}

void spawnWave()
{
    bulletCount = 0;
    minionCount = 0;
    spawnIndex = 0;
    gamePhase = GamePhase::Spawning;
    lastSpawn = millis();
}

uint32_t randomGameColor()
{
    switch (random(3))
    {
        case 0: return COLOR_BLUE;
        case 1: return COLOR_RED;
        default: return COLOR_YELLOW;
    }
}

void addMinion(int position, uint32_t color)
{
    if (minionCount >= MAX_MINIONS || position < 0 || position >= LED_COUNT)
    {
        return;
    }

    minions[minionCount++] = {position, color};
}

void updateSpawning()
{
    if (spawnIndex < INITIAL_ENEMIES)
    {
        if (millis() - lastSpawn >= SPAWN_DELAY_MS)
        {
            addMinion(INITIAL_ENEMY_START_LED + spawnIndex, randomGameColor());
            spawnIndex++;
            lastSpawn = millis();
        }
        return;
    }

    gamePhase = GamePhase::Playing;
    lastEnemyMove = millis();
    lastBulletMove = millis();
}

void addBullet(uint32_t color)
{
    if (bulletCount < MAX_BULLETS)
    {
        bullets[bulletCount++] = {BULLET_START_LED, color};
    }
}

void removeBullet(int index)
{
    bullets[index] = bullets[--bulletCount];
}

void removeMinion(int index)
{
    minions[index] = minions[--minionCount];
}

void moveEnemiesBackward(int distance)
{
    for (int i = 0; i < minionCount; i++)
    {
        minions[i].pos -= distance;
    }
}

void checkBulletCollisions()
{
    for (int bulletIndex = 0; bulletIndex < bulletCount; bulletIndex++)
    {
        if (bullets[bulletIndex].pos >= LED_COUNT)
        {
            moveEnemiesBackward(2);
            removeBullet(bulletIndex--);
            continue;
        }

        int matchingMinion = -1;
        bool hitMinion = false;

        for (int minionIndex = 0; minionIndex < minionCount; minionIndex++)
        {
            if (bullets[bulletIndex].pos != minions[minionIndex].pos)
            {
                continue;
            }

            hitMinion = true;
            if (bullets[bulletIndex].color == minions[minionIndex].color)
            {
                matchingMinion = minionIndex;
                break;
            }
        }

        if (!hitMinion)
        {
            continue;
        }

        if (matchingMinion >= 0)
        {
            removeMinion(matchingMinion);
        }

        // A wrong-colour shot is consumed, but it must not create another
        // enemy; otherwise the visible wave can be cleared without meeting
        // the victory condition.
        removeBullet(bulletIndex--);
    }
}

void updateBullets()
{
    if (millis() - lastBulletMove < BULLET_MOVE_MS)
    {
        return;
    }

    lastBulletMove = millis();
    for (int i = 0; i < bulletCount; i++)
    {
        bullets[i].pos++;
    }
    checkBulletCollisions();
}

void updateEnemies()
{
    if (millis() - lastEnemyMove < static_cast<unsigned long>(enemyDelay))
    {
        return;
    }

    lastEnemyMove = millis();
    for (int i = 0; i < minionCount; i++)
    {
        minions[i].pos--;
    }

    // Detect contacts caused by enemy movement before bullets move again.
    // Without this, an adjacent enemy and bullet can swap positions between
    // updates and the bullet appears to pass through the enemy.
    checkBulletCollisions();

    if (spawnIndex < TOTAL_ENEMIES)
    {
        addMinion(LED_COUNT - 1, randomGameColor());
        spawnIndex++;
    }
}

void updatePlayerInput()
{
    if (buttonPressed(blueButton))
    {
        Serial.println("Blue button");
        addBullet(COLOR_BLUE);
    }
    if (buttonPressed(redButton))
    {
        Serial.println("Red button");
        addBullet(COLOR_RED);
    }
    if (buttonPressed(yellowButton))
    {
        Serial.println("Yellow button");
        addBullet(COLOR_YELLOW);
    }
}

bool enemiesReachedPlayer()
{
    for (int i = 0; i < minionCount; i++)
    {
        if (minions[i].pos <= PLAYER_BUFFER_LEDS)
        {
            return true;
        }
    }
    return false;
}

void gameOver()
{
    gamePhase = GamePhase::Hiatus;
    bulletCount = 0;
    minionCount = 0;
    strip.clear();
    strip.show();
}

void updatePlaying()
{
    updatePlayerInput();
    updateEnemies();
    updateBullets();

    if (enemiesReachedPlayer())
    {
        gameOver();
        return;
    }

    if (minionCount == 0 && spawnIndex == TOTAL_ENEMIES)
    {
        victory();
        gamePhase = GamePhase::Hiatus;
    }
}

void drawGame()
{
    strip.clear();

    for (int i = 0; i < minionCount; i++)
    {
        if (minions[i].pos >= 0 && minions[i].pos < LED_COUNT)
        {
            strip.setPixelColor(minions[i].pos, minions[i].color);
        }
    }

    for (int i = 0; i < bulletCount; i++)
    {
        if (bullets[i].pos >= 0 && bullets[i].pos < LED_COUNT)
        {
            strip.setPixelColor(bullets[i].pos, bullets[i].color);
        }
    }

    strip.show();
}

void setup()
{
    Serial.begin(115200);

    pinMode(BTN_BLUE, INPUT_PULLUP);
    pinMode(BTN_RED, INPUT_PULLUP);
    pinMode(BTN_YELLOW, INPUT_PULLUP);

    strip.begin();
    strip.setBrightness(LED_BRIGHTNESS);
    strip.clear();
    strip.show();

    COLOR_BLUE = strip.Color(0, 0, 255);
    COLOR_RED = strip.Color(255, 0, 0);
    COLOR_YELLOW = strip.Color(255, 180, 0);

    randomSeed(micros());

    Serial.println();
    Serial.println("ESP32-S3 LED Color Game");
    Serial.println("Press any button to start.");
}

void loop()
{
    switch (gamePhase)
    {
        case GamePhase::Hiatus:
            rainbow();
            if (anyButtonPressed())
            {
                resetGame();
                spawnWave();
            }
            break;

        case GamePhase::Spawning:
            updateSpawning();
            drawGame();
            break;

        case GamePhase::Playing:
            updatePlaying();
            if (gamePhase == GamePhase::Playing)
            {
                drawGame();
            }
            break;
    }
}
