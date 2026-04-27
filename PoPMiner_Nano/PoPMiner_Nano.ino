/**
 * ============================================================================
 * PoPMiner Nano - Kaspa Lottery Miner for ESP32 Cheap Yellow Display (CYD)
 * ============================================================================
 *
 * Hardware: ESP32-2432S028R (ESP32-WROOM-32, 2.8" 240x320 ILI9341, XPT2046 touch)
 *
 * Mining core ported from KASDeck (Proof of Prints).
 *
 * ============================================================================
 * BUILD NOTES (Arduino IDE)
 * ============================================================================
 * Board: ESP32 Dev Module
 *   Flash Size: 4MB (32Mb)
 *   Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
 *   PSRAM: Disabled
 *   CPU Freq: 240MHz, Flash Freq: 80MHz, Flash Mode: QIO
 *
 * Libraries:
 *   - TFT_eSPI by Bodmer        (configure with bundled User_Setup.h - see file)
 *   - XPT2046_Touchscreen by Paul Stoffregen
 *   - lvgl v8.3.x               (drop bundled lv_conf.h next to this .ino)
 *   - WiFiManager by tzapu
 *   - ArduinoJson v6.21.x       (NOT v7 - uses DynamicJsonDocument API)
 *
 * First-boot flow:
 *   1. Device starts AP "PoPMinerNano" (pwd: kaspa123)
 *   2. Connect with phone -> captive portal
 *   3. Enter WiFi creds + Kaspa wallet + pool URL + worker name
 *   4. Save -> device reboots, connects to WiFi + pool
 *   5. Tap MINE button on screen to start hashing
 *
 * Factory reset: tap the gear icon (top-right) -> "Reset Config"
 * ============================================================================
 */

#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>
#include <esp_system.h>

#define LV_CONF_INCLUDE_SIMPLE
#include "lv_conf.h"
#include <lvgl.h>

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ==================== CONFIGURATION ====================
#define FW_VERSION         "0.1.0"
#define AP_NAME            "PoPMinerNano"
#define AP_PASSWORD        "kaspa123"
#define DEFAULT_POOL       "pool.proofofprints.com:5558"
#define DEFAULT_WORKER     "PoPMinerNano"

// Landscape: 320 wide x 240 tall (USB on left)
#define SCREEN_W 320
#define SCREEN_H 240

// CYD touch pins (XPT2046 on VSPI - separate from display HSPI)
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Touch calibration in landscape rotation 1.
// Watch Serial monitor when you tap each corner - adjust these so the
// reported raw values map cleanly to (0,0)..(SCREEN_W,SCREEN_H).
uint16_t TOUCH_X_MIN = 300;
uint16_t TOUCH_X_MAX = 3800;
uint16_t TOUCH_Y_MIN = 200;
uint16_t TOUCH_Y_MAX = 3700;

// ==================== COLORS (proofofprints.com palette) ====================
#define COLOR_BG          0x000000  // pure black - matches website's deepest gradient
#define COLOR_CARD        0x0E1430  // very dark navy card surface, slightly above BG
#define COLOR_TURQUOISE   0x49D9D3  // primary brand (site)
#define COLOR_YELLOW      0xEAB308  // amber/yellow (site - Tailwind yellow-500)
#define COLOR_PURPLE      0xA887E0  // difficulty accent (PoPMobile-only - site has no purple)
#define COLOR_RED_BRIGHT  0xEF4444  // bright red (Tailwind red-500) - JOBS card text
#define COLOR_STOP        0xDC2626  // button red (Tailwind red-600) - STOP button
#define COLOR_ORANGE      0xFF9933  // warnings
#define COLOR_TEXT        0xFFFFFF  // primary text
#define COLOR_DIM         0x9CA3AF  // dim labels (site --tw-text-color)
#define COLOR_GREEN       0x22C55E  // success state (site - Tailwind green-500)
#define COLOR_RED         0x7F1D1D  // dark red for error backgrounds (site)

// Old name kept for back-compat with the JOBS card constructor call below.
#define COLOR_CORAL       COLOR_RED_BRIGHT

// Aliases preserved for code that referenced old names
#define COLOR_BORDER      COLOR_TURQUOISE
#define COLOR_LABEL       COLOR_TURQUOISE

// ==================== MINING STATE ====================
uint8_t  currentHeaderHash[32] = {0};
uint8_t  currentTarget[32]     = {0};
char     currentJobId[65]      = "";
uint64_t currentTimestamp      = 0;
bool     hasJob                = false;
double   currentDifficulty     = 1.0;

volatile uint32_t hashes_done    = 0;
volatile uint64_t totalHashes    = 0;
volatile uint32_t sharesSubmitted= 0;
volatile uint32_t sharesAccepted = 0;
volatile uint32_t sharesRejected = 0;
volatile uint32_t blocksFound    = 0;
volatile uint32_t jobsReceived   = 0;
volatile float    currentHashrate= 0.0f;

unsigned long last_hash_check = 0;

bool             miningEnabled  = false;
bool             poolConnected  = false;
bool             isAuthorized   = false;
bool             wifiReady      = false;

WiFiClient       stratumClient;
SemaphoreHandle_t miningStateMutex = NULL;
TaskHandle_t     MinerTask = NULL;

Preferences      prefs;

// User config (loaded from NVS on boot, written by WiFiManager save callback)
char cfgWallet[101] = "";
char cfgPool[81]    = DEFAULT_POOL;
char cfgWorker[33]  = DEFAULT_WORKER;

// ==================== UI HANDLES ====================
TFT_eSPI       tft = TFT_eSPI();
SPIClass       touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t lv_buf1[SCREEN_W * 40];

lv_obj_t *uiHashrateLabel = nullptr;
lv_obj_t *uiSharesLabel   = nullptr;
lv_obj_t *uiDiffLabel     = nullptr;
lv_obj_t *uiHashesLabel   = nullptr;
lv_obj_t *uiStatusLabel   = nullptr;
lv_obj_t *uiMineBtnLabel  = nullptr;
lv_obj_t *uiMineBtn       = nullptr;
lv_obj_t *uiTitleLabel    = nullptr;

unsigned long statusClearAt = 0;
unsigned long lastUiUpdate  = 0;

// ==================== FORWARD DECLS ====================
void connectToPool();
void handleStratumMessages();
void submitShare(const char* jobId, uint64_t nonce);
void setTargetFromDifficulty(double difficulty);
void miningLoopTask(void *pvParameters);
void startMiningTask();
void stopMiningTask();
void createUI();
void updateUI();
void setStatus(const char* msg, uint32_t color);
void mine_btn_event_cb(lv_event_t *e);
void gear_btn_event_cb(lv_event_t *e);
void factory_reset_now();
void loadConfig();
void saveConfig();
void wifiSetupBlocking();

// ============================================================================
// KHEAVYHASH IMPLEMENTATION (verbatim from KASDeck - rusty-kaspa compatible)
// ============================================================================

static const uint64_t KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808AULL, 0x8000000080008000ULL,
    0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008AULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

static const int KECCAK_ROTC[24] = {
     1,  3,  6, 10, 15, 21, 28, 36,
    45, 55,  2, 14, 27, 41, 56,  8,
    25, 43, 62, 18, 39, 61, 20, 44
};

static const int KECCAK_PILN[24] = {
    10,  7, 11, 17, 18,  3,  5, 16,
     8, 21, 24,  4, 15, 23, 19, 13,
    12,  2, 20, 14, 22,  9,  6,  1
};

static inline uint64_t rotl64(uint64_t x, int y) {
    return (x << y) | (x >> (64 - y));
}

void keccakf(uint64_t state[25]) {
    uint64_t t, bc[5];
    for (int round = 0; round < 24; round++) {
        for (int i = 0; i < 5; i++)
            bc[i] = state[i] ^ state[i+5] ^ state[i+10] ^ state[i+15] ^ state[i+20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i+4)%5] ^ rotl64(bc[(i+1)%5], 1);
            for (int j = 0; j < 25; j += 5) state[j+i] ^= t;
        }
        t = state[1];
        for (int i = 0; i < 24; i++) {
            int j = KECCAK_PILN[i];
            bc[0] = state[j];
            state[j] = rotl64(t, KECCAK_ROTC[i]);
            t = bc[0];
        }
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++) bc[i] = state[j+i];
            for (int i = 0; i < 5; i++) state[j+i] ^= (~bc[(i+1)%5]) & bc[(i+2)%5];
        }
        state[0] ^= KECCAK_RC[round];
    }
}

// XoShiRo256++ PRNG seeded from pre_pow_hash
class XoShiRo256PlusPlus {
private:
    uint64_t s[4];
public:
    XoShiRo256PlusPlus(const uint8_t* hash) {
        for (int i = 0; i < 4; i++) {
            s[i] = 0;
            for (int j = 0; j < 8; j++) {
                s[i] |= ((uint64_t)hash[i * 8 + j]) << (j * 8);
            }
        }
    }
    uint64_t next() {
        const uint64_t result = rotl64(s[0] + s[3], 23) + s[0];
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl64(s[3], 45);
        return result;
    }
};

struct HeavyMatrix {
    uint16_t data[64][64];

    void fill(XoShiRo256PlusPlus& rng) {
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 64; j += 16) {
                uint64_t val = rng.next();
                for (int shift = 0; shift < 16; shift++) {
                    data[i][j + shift] = (val >> (4 * shift)) & 0x0F;
                }
            }
        }
    }

    int computeRank() {
        const double RANK_EPS = 1e-9;
        // No PSRAM on CYD - use regular heap (32KB)
        double (*mat)[64] = (double (*)[64])malloc(64 * 64 * sizeof(double));
        if (!mat) return 0;
        bool row_selected[64] = {false};
        int rank = 0;
        for (int i = 0; i < 64; i++)
            for (int j = 0; j < 64; j++)
                mat[i][j] = (double)data[i][j];
        for (int i = 0; i < 64; i++) {
            int j = 0;
            while (j < 64) {
                if (!row_selected[j] && fabs(mat[j][i]) > RANK_EPS) break;
                j++;
            }
            if (j != 64) {
                rank++;
                row_selected[j] = true;
                for (int p = i + 1; p < 64; p++) mat[j][p] /= mat[j][i];
                for (int k = 0; k < 64; k++) {
                    if (k != j && fabs(mat[k][i]) > RANK_EPS) {
                        for (int p = i + 1; p < 64; p++)
                            mat[k][p] -= mat[j][p] * mat[k][i];
                    }
                }
            }
        }
        free(mat);
        return rank;
    }

    void generate(const uint8_t* hash) {
        XoShiRo256PlusPlus rng(hash);
        do { fill(rng); } while (computeRank() != 64);
    }
};

// Pre-computed cSHAKE256 initial states (from rusty-kaspa)
// Domain init + finalization padding baked in - allows single keccakf per hash
static const uint64_t POW_HASH_STATE[25] = {
    1242148031264380989ULL, 3008272977830772284ULL, 2188519011337848018ULL,
    1992179434288343456ULL, 8876506674959887717ULL, 5399642050693751366ULL,
    1745875063082670864ULL, 8605242046444978844ULL, 17936695144567157056ULL,
    3343109343542796272ULL, 1123092876221303306ULL, 4963925045340115282ULL,
    17037383077651887893ULL, 16629644495023626889ULL, 12833675776649114147ULL,
    3784524041015224902ULL, 1082795874807940378ULL, 13952716920571277634ULL,
    13411128033953605860ULL, 15060696040649351053ULL, 9928834659948351306ULL,
    5237849264682708699ULL, 12825353012139217522ULL, 6706187291358897596ULL,
    196324915476054915ULL
};

static const uint64_t HEAVY_HASH_STATE[25] = {
    4239941492252378377ULL, 8746723911537738262ULL, 8796936657246353646ULL,
    1272090201925444760ULL, 16654558671554924250ULL, 8270816933120786537ULL,
    13907396207649043898ULL, 6782861118970774626ULL, 9239690602118867528ULL,
    11582319943599406348ULL, 17596056728278508070ULL, 15212962468105129023ULL,
    7812475424661425213ULL, 3370482334374859748ULL, 5690099369266491460ULL,
    8596393687355028144ULL, 570094237299545110ULL, 9119540418498120711ULL,
    16901969272480492857ULL, 13372017233735502424ULL, 14372891883993151831ULL,
    5171152063242093102ULL,
    10573107899694386186ULL, 6096431547456407061ULL, 1592359455985097269ULL
};

struct KHeavyHashState {
    HeavyMatrix matrix;
    uint8_t lastPrePowHash[32];
    bool matrixValid;

    KHeavyHashState() : matrixValid(false) {
        memset(lastPrePowHash, 0, 32);
    }

    void compute(const uint8_t* input, uint8_t* output) {
        uint8_t hash1[32];
        uint8_t vec[64];
        uint8_t product[32];

        if (!matrixValid || memcmp(input, lastPrePowHash, 32) != 0) {
            matrix.generate(input);
            memcpy(lastPrePowHash, input, 32);
            matrixValid = true;
        }

        // PoW hash
        {
            uint64_t ks[25];
            memcpy(ks, POW_HASH_STATE, sizeof(ks));
            for (int i = 0; i < 10; i++) {
                uint64_t word;
                memcpy(&word, input + i * 8, 8);
                ks[i] ^= word;
            }
            keccakf(ks);
            memcpy(hash1, ks, 32);
        }

        // Split into nibbles
        for (int i = 0; i < 32; i++) {
            vec[2 * i]     = hash1[i] >> 4;
            vec[2 * i + 1] = hash1[i] & 0x0F;
        }

        // Matrix-vector multiply
        for (int i = 0; i < 32; i++) {
            uint16_t sum1 = 0, sum2 = 0;
            for (int j = 0; j < 64; j++) {
                sum1 += matrix.data[2 * i][j] * vec[j];
                sum2 += matrix.data[2 * i + 1][j] * vec[j];
            }
            product[i] = (uint8_t)(((sum1 >> 10) << 4) | (sum2 >> 10));
        }

        // XOR with intermediate hash
        for (int i = 0; i < 32; i++) product[i] ^= hash1[i];

        // Heavy hash
        {
            uint64_t ks[25];
            memcpy(ks, HEAVY_HASH_STATE, sizeof(ks));
            for (int i = 0; i < 4; i++) {
                uint64_t word;
                memcpy(&word, product + i * 8, 8);
                ks[i] ^= word;
            }
            keccakf(ks);
            memcpy(output, ks, 32);
        }
    }
};

KHeavyHashState kheavyState;

// hash <= target (little-endian comparison, byte 31 is MSB)
inline bool checkDifficulty(const uint8_t* hash, const uint8_t* target) {
    for (int i = 31; i >= 0; i--) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;
}

void setTargetFromDifficulty(double difficulty) {
    if (difficulty <= 0) difficulty = 0.0001;
    currentDifficulty = difficulty;
    memset(currentTarget, 0, 32);
    double multiplier = 1.0 / difficulty;
    if (multiplier >= 4294967296.0) {
        memset(currentTarget, 0xFF, 32);
    } else {
        uint64_t mult_int = (uint64_t)multiplier;
        if (mult_int == 0) mult_int = 1;
        uint64_t carry = 0;
        for (int i = 0; i < 28; i++) {
            uint64_t val = (uint64_t)0xFF * mult_int + carry;
            currentTarget[i] = (uint8_t)(val & 0xFF);
            carry = val >> 8;
        }
        for (int i = 28; i < 32; i++) {
            currentTarget[i] = (uint8_t)(carry & 0xFF);
            carry >>= 8;
        }
        if (carry > 0) memset(currentTarget, 0xFF, 32);
    }

    // Log most-significant target bytes so we can see how aggressive the
    // pool's difficulty is. byte[31] = MSB. Higher = easier to find a share.
    Serial.printf("[TARGET] diff=%.6f target MSB[31..24]= %02x %02x %02x %02x %02x %02x %02x %02x\n",
                  difficulty,
                  currentTarget[31], currentTarget[30], currentTarget[29], currentTarget[28],
                  currentTarget[27], currentTarget[26], currentTarget[25], currentTarget[24]);
}

// ============================================================================
// STRATUM PROTOCOL
// ============================================================================

void submitShare(const char* jobId, uint64_t nonce) {
    char nonceHex[19];
    sprintf(nonceHex, "0x%016llx", nonce);

    String walletWorker = String(cfgWallet) + "." + String(cfgWorker);
    String msg = "{\"id\":4,\"method\":\"mining.submit\",\"params\":[\"" +
                 walletWorker + "\",\"" + String(jobId) + "\",\"" + String(nonceHex) + "\"]}\n";

    Serial.printf("[STRATUM] Submitting share - Job: %s Nonce: %s\n", jobId, nonceHex);
    stratumClient.print(msg);
    sharesSubmitted++;
    setStatus("Share submitted", COLOR_YELLOW);
}

void connectToPool() {
    if (stratumClient.connected()) return;

    if (strlen(cfgWallet) == 0) {
        Serial.println("[POOL] No wallet configured");
        setStatus("No wallet configured", COLOR_RED);
        poolConnected = false;
        return;
    }

    String addr = String(cfgPool);
    addr.replace("stratum+tcp://", "");
    int colonIndex = addr.indexOf(':');
    if (colonIndex == -1) {
        Serial.println("[POOL] Invalid pool URL format");
        setStatus("Invalid pool URL", COLOR_RED);
        return;
    }
    String host = addr.substring(0, colonIndex);
    int port = addr.substring(colonIndex + 1).toInt();

    Serial.printf("[POOL] Connecting to %s:%d ...\n", host.c_str(), port);
    setStatus("Connecting to pool...", COLOR_YELLOW);

    if (stratumClient.connect(host.c_str(), port)) {
        Serial.println("[POOL] Connected. Sending handshake...");

        String sub = "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"" +
                     String(cfgWorker) + "\"]}\n";
        stratumClient.print(sub);
        stratumClient.flush();
        delay(800);

        String auth = "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"" +
                      String(cfgWallet) + "." + String(cfgWorker) + "\"]}\n";
        stratumClient.print(auth);
        stratumClient.flush();

        poolConnected = true;
        setStatus("Pool connected", COLOR_GREEN);
    } else {
        Serial.println("[POOL] Failed to connect");
        setStatus("Pool connect failed", COLOR_RED);
        poolConnected = false;
    }
}

void handleStratumMessages() {
    int messagesProcessed = 0;
    const int MAX_MESSAGES_PER_LOOP = 5;

    while (stratumClient.available() && messagesProcessed < MAX_MESSAGES_PER_LOOP) {
        static char line[1024];
        int lineLen = 0;
        while (stratumClient.available() && lineLen < 1023) {
            char c = stratumClient.read();
            if (c == '\n') break;
            if (c != '\r') line[lineLen++] = c;
        }
        line[lineLen] = '\0';
        if (lineLen == 0) continue;
        messagesProcessed++;

        DynamicJsonDocument doc(2048);
        if (deserializeJson(doc, line)) continue;

        // mining.notify - new job
        if (doc.containsKey("method") && doc["method"] == "mining.notify") {
            if (xSemaphoreTake(miningStateMutex, portMAX_DELAY) == pdTRUE) {
                JsonArray params = doc["params"];

                const char* jobIdFromJson = params[0].as<const char*>();
                strncpy(currentJobId, jobIdFromJson, 64);
                currentJobId[64] = '\0';

                if (params[1].is<JsonArray>()) {
                    JsonArray headerArray = params[1];
                    for (int i = 0; i < 4 && i < (int)headerArray.size(); i++) {
                        uint64_t value;
                        if (headerArray[i].is<const char*>()) {
                            value = strtoull(headerArray[i].as<const char*>(), nullptr, 10);
                        } else {
                            char numBuf[24];
                            serializeJson(headerArray[i], numBuf, sizeof(numBuf));
                            value = strtoull(numBuf, nullptr, 10);
                        }
                        for (int j = 0; j < 8; j++) {
                            currentHeaderHash[i * 8 + j] = (value >> (j * 8)) & 0xFF;
                        }
                    }
                } else {
                    const char* headerHex = params[1].as<const char*>();
                    if (headerHex) {
                        int hexLen = strlen(headerHex);
                        for (int i = 0; i < 32 && i * 2 < hexLen; i++) {
                            char hexByte[3] = {headerHex[i * 2], headerHex[i * 2 + 1], '\0'};
                            currentHeaderHash[i] = strtoul(hexByte, nullptr, 16);
                        }
                    }
                }

                if (params.size() > 2) {
                    char tsBuf[24];
                    serializeJson(params[2], tsBuf, sizeof(tsBuf));
                    currentTimestamp = strtoull(tsBuf, nullptr, 10);
                } else {
                    currentTimestamp = 0;
                }

                hasJob = true;
                poolConnected = true;
                jobsReceived++;
                xSemaphoreGive(miningStateMutex);
            }
            Serial.printf("[STRATUM] New job: %s\n", currentJobId);
        }

        // mining.set_difficulty
        if (doc.containsKey("method") && doc["method"] == "mining.set_difficulty") {
            double diff = doc["params"][0].as<double>();
            Serial.printf("[STRATUM] Difficulty: %.6f\n", diff);
            if (xSemaphoreTake(miningStateMutex, portMAX_DELAY) == pdTRUE) {
                setTargetFromDifficulty(diff);
                xSemaphoreGive(miningStateMutex);
            }
        }

        // Auth response (id:2)
        if (doc.containsKey("id") && doc["id"] == 2 && doc.containsKey("result")) {
            isAuthorized = doc["result"].as<bool>();
            poolConnected = isAuthorized;
            Serial.printf("[STRATUM] Authorization: %s\n", isAuthorized ? "OK" : "FAILED");
            setStatus(isAuthorized ? "Authorized" : "Auth failed",
                      isAuthorized ? COLOR_GREEN : COLOR_RED);
        }

        // Share response (id:4)
        if (doc.containsKey("id") && doc["id"] == 4 && !doc.containsKey("method")) {
            if (doc.containsKey("result") && doc["result"].is<bool>() && doc["result"].as<bool>()) {
                sharesAccepted++;
                bool isBlock = false;
                if (doc.containsKey("error") && !doc["error"].isNull()) {
                    const char* errorStr = doc["error"].as<const char*>();
                    if (errorStr && (strstr(errorStr, "block") || strstr(errorStr, "BLOCK"))) {
                        isBlock = true;
                        blocksFound++;
                    }
                }
                Serial.printf("[STRATUM] Share accepted%s\n", isBlock ? " - BLOCK!" : "");
                setStatus(isBlock ? "BLOCK FOUND!" : "Share accepted", COLOR_GREEN);
            } else {
                sharesRejected++;
                const char* reason = "rejected";
                if (doc.containsKey("error") && !doc["error"].isNull()) {
                    if (doc["error"].is<JsonArray>()) {
                        JsonArray err = doc["error"];
                        if (err.size() > 1) {
                            const char* errMsg = err[1].as<const char*>();
                            if (errMsg) reason = errMsg;
                        }
                    } else {
                        const char* errMsg = doc["error"].as<const char*>();
                        if (errMsg) reason = errMsg;
                    }
                }
                Serial.printf("[STRATUM] Share rejected: %s\n", reason);
                char buf[64];
                snprintf(buf, sizeof(buf), "Rejected: %s", reason);
                setStatus(buf, COLOR_RED);
            }
        }
    }
}

// ============================================================================
// MINING TASK
// ============================================================================

void miningLoopTask(void *pvParameters) {
    uint8_t hash_output[32];
    uint8_t work_buffer[80] = {0};
    uint64_t nonce = ((uint64_t)esp_random() << 32) | esp_random();
    char lastJobId[65] = "";
    static uint8_t local_target[32];
    static char local_jobId[65] = "";
    bool first_job_received = false;

    Serial.printf("[MINER] Task started on core %d\n", xPortGetCoreID());

    uint32_t hash_counter = 0;

    while (true) {
        if (miningEnabled && hasJob) {
            hash_counter++;
            if (!first_job_received || hash_counter >= 1000) {
                hash_counter = 0;
                vTaskDelay(1);  // Feed watchdog
                first_job_received = true;

                if (xSemaphoreTake(miningStateMutex, portMAX_DELAY) == pdTRUE) {
                    bool jobChanged = (strcmp(currentJobId, lastJobId) != 0);
                    if (jobChanged) {
                        memset(work_buffer, 0, 80);
                        memcpy(work_buffer, currentHeaderHash, 32);
                        memcpy(work_buffer + 32, &currentTimestamp, 8);
                        strncpy(lastJobId, currentJobId, 64);
                        lastJobId[64] = '\0';
                    }
                    memcpy(local_target, currentTarget, 32);
                    strncpy(local_jobId, currentJobId, 64);
                    local_jobId[64] = '\0';
                    xSemaphoreGive(miningStateMutex);
                } else {
                    vTaskDelay(100);
                    continue;
                }
            }

            *((uint64_t*)(work_buffer + 72)) = nonce;
            kheavyState.compute(work_buffer, hash_output);

            if (checkDifficulty(hash_output, local_target)) {
                Serial.printf("[MINER] Share found! Nonce: 0x%016llx Job: %s\n",
                              nonce, local_jobId);
                submitShare(local_jobId, nonce);
            }

            hashes_done++;
            totalHashes++;
            nonce++;
        } else {
            vTaskDelay(100);
        }
    }
}

void startMiningTask() {
    if (MinerTask == NULL) {
        // 24KB stack: matrix on heap, working set is small
        xTaskCreatePinnedToCore(miningLoopTask, "Miner", 24000, NULL, 1, &MinerTask, 0);
        Serial.println("[MINER] Task created on core 0");
    }
}

void stopMiningTask() {
    if (MinerTask != NULL) {
        vTaskDelete(MinerTask);
        MinerTask = NULL;
        delay(50);
        Serial.println("[MINER] Task stopped");
    }
}

// ============================================================================
// LVGL DISPLAY/TOUCH BRIDGE
// ============================================================================

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        data->point.x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W), 0, SCREEN_W - 1);
        data->point.y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H), 0, SCREEN_H - 1);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ============================================================================
// UI CONSTRUCTION
// ============================================================================

// Build a card with its own value color and a turquoise outline (matches
// the proofofprints.com card style with subtle glowing border).
static lv_obj_t* makeCard(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *labelText, uint32_t valueColor,
                          lv_obj_t **valueLabelOut) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_TURQUOISE), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, "—");
    lv_obj_set_style_text_color(val, lv_color_hex(valueColor), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_28, 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    *valueLabelOut = val;
    return card;
}

// PoP cubes logo (32x32 RGB565+alpha). Generated by convert_logo.py from
// L:\PoPManager\src-tauri\icons\32x32.png. The .c file lives next to this .ino
// and is picked up automatically by the Arduino build (it compiles all .c/.cpp
// files in the sketch folder).
LV_IMG_DECLARE(pop_logo);

void createUI() {
    // Landscape 320x240 layout:
    //   y=  0..32   header (32px) - logo + title + gear
    //   y= 34..104  row 1 cards (70px) - HASHRATE | SHARES
    //   y=106..176  row 2 cards (70px) - DIFFICULTY | HASHES
    //   y=178..210  mine button (32px)
    //   y=212..234  status strip (22px, scrolling)

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header (32px) ──────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_W, 32);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // PoP logo (top-left, 32x32 from pop_logo.c)
    lv_obj_t *logoImg = lv_img_create(hdr);
    lv_img_set_src(logoImg, &pop_logo);
    lv_obj_align(logoImg, LV_ALIGN_LEFT_MID, 2, 0);

    // Title (centered between logo and gear)
    uiTitleLabel = lv_label_create(hdr);
    lv_label_set_text(uiTitleLabel, "PoPMiner Nano");
    lv_obj_set_style_text_color(uiTitleLabel, lv_color_hex(COLOR_TURQUOISE), 0);
    lv_obj_set_style_text_font(uiTitleLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(uiTitleLabel, LV_ALIGN_CENTER, 0, 0);

    // Gear (top-right)
    lv_obj_t *gear = lv_btn_create(hdr);
    lv_obj_set_size(gear, 30, 28);
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(gear, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_border_color(gear, lv_color_hex(COLOR_TURQUOISE), 0);
    lv_obj_set_style_border_width(gear, 1, 0);
    lv_obj_set_style_border_opa(gear, LV_OPA_60, 0);
    lv_obj_set_style_radius(gear, 4, 0);
    lv_obj_set_style_pad_all(gear, 0, 0);
    lv_obj_t *gearLbl = lv_label_create(gear);
    lv_label_set_text(gearLbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gearLbl, lv_color_hex(COLOR_DIM), 0);
    lv_obj_center(gearLbl);
    lv_obj_add_event_cb(gear, gear_btn_event_cb, LV_EVENT_CLICKED, NULL);

    // ── 2x2 grid of cards ──────────────────────────────────
    const int CARD_W = 154;
    const int CARD_H = 70;
    const int LEFT_X = 4;
    const int RIGHT_X = 162;

    makeCard(scr, LEFT_X,  34, CARD_W, CARD_H, "HASHRATE",   COLOR_TURQUOISE, &uiHashrateLabel);
    makeCard(scr, RIGHT_X, 34, CARD_W, CARD_H, "SHARES",     COLOR_YELLOW,    &uiSharesLabel);
    makeCard(scr, LEFT_X, 106, CARD_W, CARD_H, "DIFFICULTY", COLOR_PURPLE,    &uiDiffLabel);
    makeCard(scr, RIGHT_X,106, CARD_W, CARD_H, "HASHES",     COLOR_RED_BRIGHT,&uiHashesLabel);

    lv_label_set_text(uiHashrateLabel, "0 H/s");
    lv_label_set_text(uiSharesLabel,   "0/0");
    lv_label_set_text(uiDiffLabel,     "—");
    lv_label_set_text(uiHashesLabel,   "0");

    // ── Mine button (32px) ─────────────────────────────────
    uiMineBtn = lv_btn_create(scr);
    lv_obj_set_size(uiMineBtn, SCREEN_W - 8, 32);
    lv_obj_set_pos(uiMineBtn, 4, 178);
    lv_obj_set_style_bg_color(uiMineBtn, lv_color_hex(COLOR_TURQUOISE), 0);
    lv_obj_set_style_border_width(uiMineBtn, 0, 0);
    lv_obj_set_style_radius(uiMineBtn, 6, 0);
    lv_obj_add_event_cb(uiMineBtn, mine_btn_event_cb, LV_EVENT_CLICKED, NULL);

    uiMineBtnLabel = lv_label_create(uiMineBtn);
    lv_label_set_text(uiMineBtnLabel, LV_SYMBOL_PLAY "  START MINING");
    lv_obj_set_style_text_font(uiMineBtnLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(uiMineBtnLabel, lv_color_hex(COLOR_BG), 0);
    lv_obj_center(uiMineBtnLabel);

    // ── Status strip (22px, scrolling) ─────────────────────
    uiStatusLabel = lv_label_create(scr);
    lv_label_set_long_mode(uiStatusLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(uiStatusLabel, SCREEN_W - 8);
    lv_obj_set_pos(uiStatusLabel, 4, 214);
    lv_label_set_text(uiStatusLabel, "Ready - tap START MINING");
    lv_obj_set_style_text_color(uiStatusLabel, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_text_font(uiStatusLabel, &lv_font_montserrat_14, 0);
}

void setStatus(const char* msg, uint32_t color) {
    if (!uiStatusLabel) return;
    lv_label_set_text(uiStatusLabel, msg);
    lv_obj_set_style_text_color(uiStatusLabel, lv_color_hex(color), 0);
    statusClearAt = millis() + 5000;
}

void updateUI() {
    // Hashrate calc - sample once per second
    unsigned long now = millis();
    unsigned long timeDiff = now - last_hash_check;
    if (timeDiff >= 1000) {
        uint32_t snap = hashes_done;
        hashes_done = 0;
        currentHashrate = (float)snap / (timeDiff / 1000.0f);
        last_hash_check = now;
    }

    char buf[48];

    // HASHRATE card
    if (currentHashrate >= 1000.0f) {
        snprintf(buf, sizeof(buf), "%.2f KH/s", currentHashrate / 1000.0f);
    } else {
        snprintf(buf, sizeof(buf), "%.0f H/s", currentHashrate);
    }
    if (uiHashrateLabel) lv_label_set_text(uiHashrateLabel, buf);

    // SHARES card: accepted/submitted (compact "9/12" form like PoPMobile)
    if (sharesRejected > 0) {
        snprintf(buf, sizeof(buf), "%u/%ur", (unsigned)sharesAccepted, (unsigned)sharesRejected);
    } else {
        snprintf(buf, sizeof(buf), "%u/%u", (unsigned)sharesAccepted, (unsigned)sharesSubmitted);
    }
    if (uiSharesLabel) lv_label_set_text(uiSharesLabel, buf);

    // DIFFICULTY card
    if (currentDifficulty >= 1.0) {
        snprintf(buf, sizeof(buf), "%.3f", currentDifficulty);
    } else {
        snprintf(buf, sizeof(buf), "%.6f", currentDifficulty);
    }
    if (uiDiffLabel) lv_label_set_text(uiDiffLabel, buf);

    // HASHES card - cumulative total formatted as K / M / G
    uint64_t th = totalHashes;
    if (th >= 1000000000ULL) {
        snprintf(buf, sizeof(buf), "%.2fG", th / 1000000000.0);
    } else if (th >= 1000000ULL) {
        snprintf(buf, sizeof(buf), "%.1fM", th / 1000000.0);
    } else if (th >= 1000ULL) {
        snprintf(buf, sizeof(buf), "%.1fK", th / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)th);
    }
    if (uiHashesLabel) lv_label_set_text(uiHashesLabel, buf);

    // Mine button - PoPMobile coral when mining, turquoise when idle
    if (uiMineBtnLabel) {
        if (miningEnabled) {
            lv_label_set_text(uiMineBtnLabel, LV_SYMBOL_PAUSE "  STOP MINING");
            lv_obj_set_style_bg_color(uiMineBtn, lv_color_hex(COLOR_STOP), 0);
            lv_obj_set_style_text_color(uiMineBtnLabel, lv_color_hex(COLOR_TEXT), 0);
        } else {
            lv_label_set_text(uiMineBtnLabel, LV_SYMBOL_PLAY "  START MINING");
            lv_obj_set_style_bg_color(uiMineBtn, lv_color_hex(COLOR_TURQUOISE), 0);
            lv_obj_set_style_text_color(uiMineBtnLabel, lv_color_hex(COLOR_BG), 0);
        }
    }

    // Auto-clear status to default after 5s
    if (statusClearAt && now >= statusClearAt) {
        statusClearAt = 0;
        if (uiStatusLabel) {
            if (miningEnabled && poolConnected) {
                lv_label_set_text(uiStatusLabel, "Mining...");
                lv_obj_set_style_text_color(uiStatusLabel, lv_color_hex(COLOR_GREEN), 0);
            } else if (poolConnected) {
                lv_label_set_text(uiStatusLabel, "Idle - pool connected");
                lv_obj_set_style_text_color(uiStatusLabel, lv_color_hex(COLOR_DIM), 0);
            } else if (wifiReady) {
                lv_label_set_text(uiStatusLabel, "WiFi OK - pool down");
                lv_obj_set_style_text_color(uiStatusLabel, lv_color_hex(COLOR_YELLOW), 0);
            } else {
                lv_label_set_text(uiStatusLabel, "WiFi disconnected");
                lv_obj_set_style_text_color(uiStatusLabel, lv_color_hex(COLOR_RED), 0);
            }
        }
    }
}

// ============================================================================
// EVENT CALLBACKS
// ============================================================================

void mine_btn_event_cb(lv_event_t *e) {
    if (!wifiReady) {
        setStatus("No WiFi - cannot mine", COLOR_RED);
        return;
    }
    if (strlen(cfgWallet) == 0) {
        setStatus("Set wallet in config first", COLOR_RED);
        return;
    }

    miningEnabled = !miningEnabled;
    Serial.printf("[UI] Mining %s\n", miningEnabled ? "STARTED" : "STOPPED");

    if (miningEnabled) {
        if (!stratumClient.connected()) connectToPool();
        startMiningTask();
        setStatus("Mining started", COLOR_GREEN);
    } else {
        stopMiningTask();
        setStatus("Mining stopped", COLOR_DIM);
    }

    prefs.begin("popminer", false);
    prefs.putBool("mining", miningEnabled);
    prefs.end();
}

static void reset_confirm_cb(lv_event_t *e) {
    factory_reset_now();
}

static void reset_cancel_cb(lv_event_t *e) {
    lv_obj_t *modal = (lv_obj_t*)lv_event_get_user_data(e);
    lv_obj_del(modal);
}

// Mask wallet so the modal doesn't shoulder-surf the full address.
static String maskWallet(const String& wallet) {
    if (wallet.length() < 16) return wallet;
    return wallet.substring(0, 10) + "..." + wallet.substring(wallet.length() - 4);
}

void gear_btn_event_cb(lv_event_t *e) {
    // Full-screen modal background (taps outside the box do nothing)
    lv_obj_t *modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modal, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_90, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    // Settings panel
    lv_obj_t *box = lv_obj_create(modal);
    lv_obj_set_size(box, SCREEN_W - 16, SCREEN_H - 16);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(COLOR_TURQUOISE), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Title bar
    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TURQUOISE), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Close (X) button - top right
    lv_obj_t *btnClose = lv_btn_create(box);
    lv_obj_set_size(btnClose, 28, 24);
    lv_obj_align(btnClose, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_set_style_bg_color(btnClose, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_color(btnClose, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_border_width(btnClose, 1, 0);
    lv_obj_set_style_radius(btnClose, 4, 0);
    lv_obj_t *xLbl = lv_label_create(btnClose);
    lv_label_set_text(xLbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(xLbl, lv_color_hex(COLOR_DIM), 0);
    lv_obj_center(xLbl);
    lv_obj_add_event_cb(btnClose, reset_cancel_cb, LV_EVENT_CLICKED, modal);

    // Info rows - "LABEL: value" stacked
    auto addInfoRow = [&](int yPos, const char* label, const String& value, uint32_t valueColor) {
        lv_obj_t *row = lv_obj_create(box);
        lv_obj_set_size(row, SCREEN_W - 32, 22);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, yPos);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_DIM), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *val = lv_label_create(row);
        lv_label_set_text(val, value.c_str());
        lv_obj_set_style_text_color(val, lv_color_hex(valueColor), 0);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
        lv_obj_align(val, LV_ALIGN_LEFT_MID, 90, 0);
    };

    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "(disconnected)";
    String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "-";

    int yRow = 32;
    addInfoRow(yRow,        "IP:",     ip,                       COLOR_TURQUOISE); yRow += 22;
    addInfoRow(yRow,        "WIFI:",   ssid,                     COLOR_TEXT);      yRow += 22;
    addInfoRow(yRow,        "POOL:",   String(cfgPool),          COLOR_TEXT);      yRow += 22;
    addInfoRow(yRow,        "WORKER:", String(cfgWorker),        COLOR_TEXT);      yRow += 22;
    addInfoRow(yRow,        "WALLET:", maskWallet(cfgWallet),    COLOR_TEXT);      yRow += 22;
    addInfoRow(yRow,        "FW:",     String(FW_VERSION),       COLOR_DIM);

    // Hint about web GUI (placeholder until we add it)
    lv_obj_t *hint = lv_label_create(box);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, SCREEN_W - 40);
    lv_label_set_text(hint, "Open http://" + ip + " in a browser on the same WiFi to edit pool / wallet (web GUI coming soon).");
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, -44);

    // Factory reset button - bottom
    lv_obj_t *btnReset = lv_btn_create(box);
    lv_obj_set_size(btnReset, SCREEN_W - 32, 32);
    lv_obj_align(btnReset, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btnReset, lv_color_hex(COLOR_STOP), 0);
    lv_obj_set_style_border_width(btnReset, 0, 0);
    lv_obj_set_style_radius(btnReset, 6, 0);
    lv_obj_t *brl = lv_label_create(btnReset);
    lv_label_set_text(brl, LV_SYMBOL_TRASH "  FACTORY RESET");
    lv_obj_set_style_text_color(brl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(brl, &lv_font_montserrat_14, 0);
    lv_obj_center(brl);
    lv_obj_add_event_cb(btnReset, reset_confirm_cb, LV_EVENT_CLICKED, NULL);
}

void factory_reset_now() {
    Serial.println("[CFG] Factory reset");
    stopMiningTask();
    if (stratumClient.connected()) stratumClient.stop();

    prefs.begin("popminer", false);
    prefs.clear();
    prefs.end();

    WiFiManager wm;
    wm.resetSettings();

    delay(500);
    ESP.restart();
}

// ============================================================================
// CONFIG (NVS)
// ============================================================================

void loadConfig() {
    prefs.begin("popminer", true);
    String w = prefs.getString("wallet", "");
    String p = prefs.getString("pool", DEFAULT_POOL);
    String n = prefs.getString("worker", DEFAULT_WORKER);
    prefs.end();

    strncpy(cfgWallet, w.c_str(), sizeof(cfgWallet) - 1); cfgWallet[sizeof(cfgWallet)-1] = '\0';
    strncpy(cfgPool,   p.c_str(), sizeof(cfgPool)   - 1); cfgPool[sizeof(cfgPool)-1]     = '\0';
    strncpy(cfgWorker, n.c_str(), sizeof(cfgWorker) - 1); cfgWorker[sizeof(cfgWorker)-1] = '\0';

    Serial.printf("[CFG] wallet len=%d pool=%s worker=%s\n",
                  (int)strlen(cfgWallet), cfgPool, cfgWorker);
}

void saveConfig() {
    prefs.begin("popminer", false);
    prefs.putString("wallet", cfgWallet);
    prefs.putString("pool",   cfgPool);
    prefs.putString("worker", cfgWorker);
    prefs.end();
    Serial.println("[CFG] Saved");
}

// ============================================================================
// WIFI SETUP (WiFiManager captive portal with custom params)
// ============================================================================

WiFiManager wm;
WiFiManagerParameter *paramWallet = nullptr;
WiFiManagerParameter *paramPool   = nullptr;
WiFiManagerParameter *paramWorker = nullptr;

static void onSaveParams() {
    Serial.println("[WM] Save params callback fired");
    if (paramWallet) strncpy(cfgWallet, paramWallet->getValue(), sizeof(cfgWallet) - 1);
    if (paramPool)   strncpy(cfgPool,   paramPool->getValue(),   sizeof(cfgPool)   - 1);
    if (paramWorker) strncpy(cfgWorker, paramWorker->getValue(), sizeof(cfgWorker) - 1);
    cfgWallet[sizeof(cfgWallet)-1] = '\0';
    cfgPool[sizeof(cfgPool)-1]     = '\0';
    cfgWorker[sizeof(cfgWorker)-1] = '\0';
    saveConfig();
}

void wifiSetupBlocking() {
    paramWallet = new WiFiManagerParameter("wallet", "Kaspa Wallet (kaspa:...)", cfgWallet, 100);
    paramPool   = new WiFiManagerParameter("pool",   "Pool URL (host:port)",      cfgPool,   80);
    paramWorker = new WiFiManagerParameter("worker", "Worker Name",               cfgWorker, 32);

    wm.addParameter(paramWallet);
    wm.addParameter(paramPool);
    wm.addParameter(paramWorker);
    wm.setSaveParamsCallback(onSaveParams);
    wm.setConfigPortalTimeout(0);   // No timeout - wait forever for user
    wm.setConnectTimeout(20);

    setStatus("Connect phone to AP", COLOR_YELLOW);
    Serial.printf("[WM] AP: %s  pwd: %s\n", AP_NAME, AP_PASSWORD);

    bool ok = wm.autoConnect(AP_NAME, AP_PASSWORD);
    if (ok) {
        wifiReady = true;
        Serial.printf("[WM] WiFi connected: %s  IP: %s\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        setStatus("WiFi connected", COLOR_GREEN);
    } else {
        Serial.println("[WM] WiFi failed - rebooting");
        ESP.restart();
    }
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n=== PoPMiner Nano %s ===\n", FW_VERSION);

    miningStateMutex = xSemaphoreCreateMutex();

    // Display init - rotation 1 = landscape, USB on left
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // Touch init (separate VSPI bus) - match display rotation
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSPI);
    ts.setRotation(1);

    // LVGL init
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, lv_buf1, NULL, SCREEN_W * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    createUI();
    lv_timer_handler();

    // Load saved config
    loadConfig();

    // WiFi (blocks on first boot until user finishes captive portal)
    wifiSetupBlocking();

    // Try pool now if wallet is configured
    if (strlen(cfgWallet) > 0) {
        connectToPool();
    } else {
        setStatus("Set wallet in config", COLOR_YELLOW);
    }

    // Mining is OFF by default - user taps MINE to start
    miningEnabled = false;

    Serial.printf("[SYS] Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.println("[SYS] Setup done. Tap MINE to start hashing.");
}

void loop() {
    lv_timer_handler();

    if (WiFi.status() == WL_CONNECTED) {
        wifiReady = true;
        if (stratumClient.connected()) {
            handleStratumMessages();
        } else if (miningEnabled && strlen(cfgWallet) > 0) {
            // Auto-reconnect to pool while mining
            static unsigned long lastReconnectAttempt = 0;
            if (millis() - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = millis();
                connectToPool();
            }
        }
    } else {
        wifiReady = false;
        poolConnected = false;
    }

    // UI refresh (4 Hz - keeps numbers lively without thrashing LVGL)
    if (millis() - lastUiUpdate >= 250) {
        lastUiUpdate = millis();
        updateUI();
    }

    // Mining state heartbeat once every 10 s. Use this to diagnose share-find:
    //   target_msb=00..00 -> pool never sent mining.set_difficulty (no shares possible)
    //   diff >= 1         -> at ~1 KH/s, shares take days/weeks (pool diff too high)
    //   diff < 0.01       -> shares should appear within a few minutes
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 10000) {
        lastHeartbeat = millis();
        Serial.printf("[HEARTBEAT] mining=%d hasJob=%d pool=%d auth=%d hashrate=%.0f H/s "
                      "diff=%.6f jobs=%u sub=%u acc=%u rej=%u target_msb=%02x%02x%02x%02x\n",
                      (int)miningEnabled, (int)hasJob, (int)poolConnected, (int)isAuthorized,
                      currentHashrate, currentDifficulty,
                      (unsigned)jobsReceived, (unsigned)sharesSubmitted,
                      (unsigned)sharesAccepted, (unsigned)sharesRejected,
                      currentTarget[31], currentTarget[30], currentTarget[29], currentTarget[28]);
    }

    delay(5);
}
