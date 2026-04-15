/*
 * ThermoWatch ESP32 - Código com WiFiManager para configuração via Portal Captivo
 * VERSÃO PT100 COM ADC (SOLUÇÃO IMPROVISADA - PRECISÃO LIMITADA)
 *
 * ⚠️ CIRCUITO NECESSÁRIO:
 *    3.3V ----[Rref 10kΩ]----+----[PT100]---- GND
 *                            |
 *                         GPIO 4 (ADC2 - LER ANTES WiFi!)
 *
 * COMO CONFIGURAR:
 * 1. Instale as bibliotecas necessárias no Arduino IDE:
 *    - WiFiManager by tzapu (versão 2.0.17 ou superior)
 *    - ArduinoJson by Benoit Blanchon (versão 6.21.3 ou superior)
 *
 * 2. Primeira vez que ligar o ESP32:
 *    - Ele criará um Access Point chamado "Thermo Watch Nitro"
 *    - Conecte seu celular nesta rede
 *    - Senha: "12345678"
 *    - Abra o navegador, será redirecionado para página de configuração
 *    - Configure WiFi, nome do dispositivo e localização
 *    - Dados ficam salvos na memória EEPROM do ESP32
 *
 * 3. Para reconfigurar:
 *    - Pressione o botão RESET 3 vezes em 10 segundos
 *    - Ou segure o botão BOOT por 5 segundos durante a inicialização
 *
 * ⚠️ LIMITAÇÕES DESTA SOLUÇÃO:
 *    - Precisão baixa (±3-5°C)
 *    - Sensível a variações de tensão
 *    - Recomendado usar módulo MAX31865 para aplicações críticas
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <EEPROM.h>
#include <time.h>
#include <Update.h>
#include <HTTPUpdate.h>
#include <SPI.h>

// Configurações de hardware
#define LED_PIN 2
#define CONFIG_BUTTON_PIN 0 // Botão BOOT do ESP32
#define BATTERY_PIN 34      // GPIO34 (ADC1_CH6) para leitura da bateria

// MAX31865 (PT100 com precisão via SPI)
// Ligações confirmadas na sua placa:
// - SDI (MOSI) -> GPIO23
// - SDO (MISO) -> GPIO19
// - SCLK       -> GPIO18
// - CS         -> GPIO5
#define MAX31865_MOSI_PIN 23
#define MAX31865_MISO_PIN 19
#define MAX31865_SCK_PIN 18
#define MAX31865_CS_PIN 5

// Ajuste conforme a sua placa do MAX31865 (a maioria usa 430Ω)
#define MAX31865_RREF 430.0
#define MAX31865_RNOMINAL 100.0

// 2, 3 ou 4 (defina conforme sua ligação do PT100)
#define MAX31865_WIRES 2

// Sistema de alertas de temperatura - 3 ZONAS APENAS
// ZONA 1: T < -150°C = NORMAL (pode dormir, sem alertas)
// ZONA 2: -150°C ≤ T ≤ -120°C = ALERTA FRIO (LED1 pisca, não dorme)
// ZONA 3: T > -120°C = ALERTA QUENTE (LED2+Buzzer, não dorme)
#define TEMP_ALERT_LED1 15   // GPIO15 - LED de alerta frio (-150 a -120°C)
#define TEMP_ALERT_LED2 16   // GPIO16 - LED de alerta quente (>-120°C)
#define TEMP_ALERT_BUZZER 12 // GPIO12 - Buzzer (ativa junto com LED2 se T>-120°C)
#define TEMP_LOW_THRESHOLD -150.0      // Início zona alerta frio (°C)
#define TEMP_LOW_ALERT_THRESHOLD -120.0 // Fim zona alerta frio / início alerta quente (°C)
#define TEMP_ALERT_INTERVAL 500        // Intervalo piscada (ms)

// PT100
#define TEMP_OFFSET 0.0      // Offset calibração (°C) - padrão: 0.0

// ========================================
// ESTRUTURAS DE DADOS
// ========================================

// Estrutura para informações do firmware
struct FirmwareInfo {
    String version;
    String url;
    String md5_hash;
    int file_size;
    bool mandatory;
    int min_battery;
    int min_rssi;
    bool available;
};

// ========================================
// DECLARAÇÕES DE FUNÇÕES (Forward declarations)
// ========================================
void saveConfigCallback();
void loadSavedConfig();
void checkConfigMode();
void setupWiFiManager();
void connectWiFi();
void saveCustomConfig();
void syncNTP();
bool testInternetConnection();
bool testSupabaseConnection();
void performConnectivityTests();
void registerDevice();
void updateDeviceBatteryStatus(float batteryVoltage, float batteryPercentage, String batteryStatus);
bool checkDeviceEnabled();
void syncReadingInterval();
bool ensureDeviceRegistered();
void readAndSendSensorData();
bool syncOffsetWithServer();
bool syncDeviceLocationWithServer();
bool reportOffsetToServer();
void sendHeartbeat();
String formatUptime(unsigned long ms);
void blinkLED(int times, int delayMs);
void setLED(bool state);
void ledConfigMode();
void ledOperationMode();
void ledSuccess();
void ledFailure();
void ledOff();
void initLedTimer();
void startLedBlink();
void stopLedBlink();
void setStatusLedAllowed(bool allowed);
void IRAM_ATTR onLedTimer();
String getISOTimestamp();
float readBatteryVoltage();
float getBatteryPercentage(float voltage);
String getBatteryStatus(float voltage, float percentage);
bool checkBatteryAlert(float voltage, float percentage);
bool checkWiFiSignalAlert(int rssi);
String getWiFiSignalQuality(int rssi);
void displayBatteryInfo();
bool sendSensorData(float temperature, float humidity);
void showWakeupReason();
void showBatteryStats();
bool isConfigMode();
float performQuickReading();
void monitorTemperatureUntilSafe(float initialTemp);
bool connectWiFiQuick();
void enterDeepSleep();
bool reconnectWiFiSilent();
bool scanForSavedNetwork();
void handleWiFiDisconnection();
void resetWiFiReconnectCounters();
bool forceWiFiReconnect();
bool sendFinalBatteryNotification(float temperature, float humidity, float batteryVoltage);
bool getWiFiGeolocation();
bool syncDeviceGeolocation();
bool shouldUpdateGeolocation();
// OTA Update functions
bool shouldCheckForUpdate();
FirmwareInfo checkFirmwareUpdate();
bool canPerformUpdate(FirmwareInfo &info, float batteryPercentage, int rssi);
String logUpdateAttempt(String fromVersion, String toVersion, String status, String error);
void updateLogStatus(String updateId, String status, String error, int duration, int bytes);
bool performOTAUpdate(FirmwareInfo &info);
void validateOTAUpdate();

// Configurações Supabase (FIXAS - não precisam ser configuradas)
#include "../include/secrets.h"

#ifndef SUPABASE_ANON_KEY
#define SUPABASE_ANON_KEY "YOUR_SUPABASE_ANON_KEY"
#endif
#ifndef GOOGLE_GEOLOCATION_API_KEY
#define GOOGLE_GEOLOCATION_API_KEY "YOUR_GOOGLE_GEOLOCATION_API_KEY"
#endif

const char *supabaseUrl = "https://qanyszslnactgtzpmtyj.supabase.co";
const char *supabaseKey = SUPABASE_ANON_KEY;

bool beginSupabaseRequest(HTTPClient &http, WiFiClientSecure &client, const String &url)
{
    client.setInsecure();
    client.setTimeout(15000);
    bool ok = http.begin(client, url);
    if (!ok) return false;
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    return true;
}

// Configurações Google Geolocation API
// Obtenha sua chave em: https://console.cloud.google.com/apis/credentials
const char *googleGeoApiKey = GOOGLE_GEOLOCATION_API_KEY;
const char *googleGeoApiUrl = "https://www.googleapis.com/geolocation/v1/geolocate";
const bool useGeolocation = true; // ✅ Executa apenas 1x no primeiro boot, depois desabilita automaticamente

// Instâncias
WiFiManager wm;
Preferences preferences;

// Variável global de offset (sincronizada com servidor)
float tempOffset = TEMP_OFFSET;

// Variáveis de geolocalização
double latitude = 0.0;
double longitude = 0.0;
float geoAccuracy = 0.0;
bool geoLocationValid = false;

// Controle de frequência de atualização de geolocalização
const unsigned long GEO_UPDATE_INTERVAL = 86400000; // 24 horas em ms (1x por dia)
unsigned long lastGeoUpdate = 0;

// ========================================
// MAX31865 + PT100
// ========================================

static SPISettings max31865SpiSettings(500000, MSBFIRST, SPI_MODE1);

static void max31865Select() { digitalWrite(MAX31865_CS_PIN, LOW); }
static void max31865Deselect() { digitalWrite(MAX31865_CS_PIN, HIGH); }

static void max31865Write8(uint8_t reg, uint8_t value)
{
    SPI.beginTransaction(max31865SpiSettings);
    max31865Select();
    SPI.transfer(reg | 0x80);
    SPI.transfer(value);
    max31865Deselect();
    SPI.endTransaction();
}

static void max31865ReadN(uint8_t reg, uint8_t *buf, size_t n)
{
    SPI.beginTransaction(max31865SpiSettings);
    max31865Select();
    SPI.transfer(reg & 0x7F);
    for (size_t i = 0; i < n; i++) {
        buf[i] = SPI.transfer(0xFF);
    }
    max31865Deselect();
    SPI.endTransaction();
}

static uint8_t max31865Read8(uint8_t reg)
{
    uint8_t v = 0;
    max31865ReadN(reg, &v, 1);
    return v;
}

static void max31865ClearFault()
{
    uint8_t config = max31865Read8(0x00);
    max31865Write8(0x00, config | 0x02);
    delay(10);
}

static bool initMAX31865()
{
    pinMode(MAX31865_CS_PIN, OUTPUT);
    max31865Deselect();

    SPI.begin(MAX31865_SCK_PIN, MAX31865_MISO_PIN, MAX31865_MOSI_PIN, MAX31865_CS_PIN);

    uint8_t modes[2] = {SPI_MODE1, SPI_MODE3};
    for (int i = 0; i < 2; i++) {
        max31865SpiSettings = SPISettings(500000, MSBFIRST, modes[i]);

        uint8_t config = 0;
        config |= 0x80;
        config |= 0x40;
        if (MAX31865_WIRES == 3) config |= 0x10;
        config |= 0x00;

        max31865Write8(0x00, config);
        delay(30);
        max31865ClearFault();
        delay(10);

        uint8_t readback = max31865Read8(0x00);
        if (readback != 0x00 && readback != 0xFF) {
            Serial.printf("✅ MAX31865 inicializado | SPI_MODE=%d | Config=0x%02X | Pinos: MOSI=%d MISO=%d SCK=%d CS=%d\n",
                          modes[i], readback, MAX31865_MOSI_PIN, MAX31865_MISO_PIN, MAX31865_SCK_PIN, MAX31865_CS_PIN);
            return true;
        }

        Serial.printf("⚠️ MAX31865 sem resposta | SPI_MODE=%d | Config=0x%02X\n", modes[i], readback);
    }

    Serial.println("❌ MAX31865 não respondeu. Verifique alimentação, GND e fios SPI (MISO/MOSI/SCK/CS).");
    return false;
}

static uint16_t max31865ReadRTD()
{
    uint8_t buf[2] = {0, 0};
    max31865ReadN(0x01, buf, 2);
    uint16_t rtd = (uint16_t(buf[0]) << 8) | uint16_t(buf[1]);
    rtd >>= 1;
    return rtd;
}

static float max31865RTDToResistance(uint16_t rtd)
{
    return (float(rtd) * MAX31865_RREF) / 32768.0f;
}

static float max31865ResistanceToTemp(float resistance)
{
    const float A = 3.9083e-3f;
    const float B = -5.775e-7f;

    float ratio = resistance / MAX31865_RNOMINAL;
    float temperature = -999.0f;

    if (ratio >= 1.0f) {
        float Z1 = -A;
        float Z2 = A * A - (4.0f * B);
        float Z3 = (4.0f * B) / MAX31865_RNOMINAL;
        float Z4 = 2.0f * B;
        temperature = (Z1 + sqrtf(Z2 + (Z3 * resistance))) / Z4;
    } else {
        float Rt = resistance / MAX31865_RNOMINAL * 100.0f;
        float rpoly = Rt;

        temperature = -242.02f;
        temperature += 2.2228f * rpoly;
        rpoly *= Rt;
        temperature += 2.5859e-3f * rpoly;
        rpoly *= Rt;
        temperature -= 4.8260e-6f * rpoly;
        rpoly *= Rt;
        temperature -= 2.8183e-8f * rpoly;
        rpoly *= Rt;
        temperature += 1.5243e-10f * rpoly;
    }

    temperature += tempOffset;
    return temperature;
}

float readPT100Resistance()
{
    uint16_t rtd = max31865ReadRTD();
    return max31865RTDToResistance(rtd);
}

float readPT100Temperature()
{
    uint8_t fault = max31865Read8(0x07);
    uint16_t rtd = max31865ReadRTD();

    if (fault || rtd == 0 || rtd >= 32760) {
        if (fault) {
            Serial.printf("⚠️ MAX31865 fault=0x%02X (limpando)\n", fault);
            max31865ClearFault();
        }
        Serial.printf("❌ PT100 desconectado/erro (fault=0x%02X, rtd=%u)\n", fault, rtd);
        return NAN;
    }

    float resistance = max31865RTDToResistance(rtd);
    float temperature = max31865ResistanceToTemp(resistance);

    if (!isfinite(temperature)) {
        Serial.printf("❌ PT100 leitura inválida (T não finita) | RTD=%u, R=%.3fΩ\n", rtd, resistance);
        return NAN;
    }

    Serial.printf("PT100/MAX31865 Debug: RTD=%u, R=%.3fΩ, T=%.2f°C, fault=0x%02X\n",
                  rtd, resistance, temperature, fault);

    return temperature;
}

// Variáveis configuráveis (salvas na EEPROM)
String deviceId;
String deviceName;
String deviceLocation;
String deviceDescription;
int readingIntervalSec = 3600; // Intervalo padrão: 60 minutos (em segundos)

// ===== CONFIGURAÇÕES DE ECONOMIA DE ENERGIA =====
// Modo bateria: intervalo configurável via WiFiManager (1-999 minutos)
// readingIntervalSec armazena o intervalo em segundos (60 a 59940)
const int MAX_RETRY_ATTEMPTS = 5; // Máximo 5 tentativas para qualquer conexão
const int RETRY_INTERVAL_SECONDS = 15; // Intervalo de 15 segundos entre tentativas
const int MAX_WIFI_ATTEMPTS = MAX_RETRY_ATTEMPTS;
const int WIFI_TIMEOUT_SECONDS = 30; // Timeout WiFi para economia

// ===== CONFIGURAÇÕES DE RECONEXÃO ROBUSTA =====
const int WIFI_RECONNECT_MAX_ATTEMPTS = 20;        // Máximo de tentativas antes de reiniciar
const int WIFI_RECONNECT_INITIAL_DELAY = 5000;    // Delay inicial 5 segundos
const int WIFI_RECONNECT_MAX_DELAY = 120000;      // Delay máximo 2 minutos (backoff)
const int WIFI_SCAN_ATTEMPTS = 3;                  // Tentativas de scan antes de cada reconexão
const unsigned long WIFI_WATCHDOG_TIMEOUT = 300000; // 5 minutos sem WiFi = restart
const int MAX_CONSECUTIVE_FAILURES = 10;          // Falhas consecutivas antes de reset total

// ===== CONFIGURAÇÕES DE QUALIDADE DO SINAL WIFI (RSSI) =====
// RSSI (Received Signal Strength Indicator) em dBm - quanto mais próximo de 0, melhor
const int WIFI_RSSI_EXCELLENT = -50;   // Excelente: > -50 dBm
const int WIFI_RSSI_GOOD = -60;       // Bom: -50 a -60 dBm  
const int WIFI_RSSI_FAIR = -70;       // Razoável: -60 a -70 dBm
const int WIFI_RSSI_WEAK = -80;       // Fraco: -70 a -80 dBm (pode ter problemas)
const int WIFI_RSSI_VERY_WEAK = -90;  // Muito fraco: -80 a -90 dBm (problemas frequentes)
const int WIFI_RSSI_CRITICAL = -95;   // Crítico: < -90 dBm (desconexões e timeouts)

// ===== CONFIGURAÇÕES DE SUPERVISÃO DE BATERIA =====
const float BATTERY_MAX_VOLTAGE = 3.7; // Tensão máxima da bateria (100%)
const float BATTERY_LOW_VOLTAGE = 3.2; // Tensão de alerta de bateria baixa (~30%)
const float BATTERY_CRITICAL_VOLTAGE = 3.1; // Tensão crítica (~15%)
const float BATTERY_MIN_VOLTAGE = 3.0; // Tensão mínima normal (0%)
const float BATTERY_FINAL_WARNING_VOLTAGE = 2.5; // Tensão para ÚLTIMO AVISO (enviar 4x e hibernar)
// Configuração do divisor de tensão resistivo para leitura da bateria
// Divisor: R1 = 100kΩ (bateria→ADC) | R2 = 10kΩ (ADC→GND)
// Fórmula: V_bat = V_adc * (R1+R2)/R2 = V_adc * 11
const float VOLTAGE_DIVIDER_RATIO = 11.0; // Divisor 100kΩ + 10kΩ
// Calibração fina calculada empiricamente para ThermoWatch_2334B4:
// ADC Raw: 305 | ADC Voltage: 0.246V | Tensão com divisor: 2.706V
// Tensão Real (Multímetro): 3.74V
// Fórmula: CALIBRATION_FACTOR = 3.74 / (0.246 * 11) = 3.74 / 2.706 = 1.382
const float BATTERY_CALIBRATION_FACTOR = 1.382; // CALIBRADO em 27/01/2026
const int BATTERY_SAMPLES = 10; // Número de amostras para média
const int BATTERY_FINAL_SEND_COUNT = 4; // Número de envios antes de hibernar

// ===== CONFIGURAÇÕES DE VERSÃO =====
#define FIRMWARE_VERSION "Thermo Watch Nitro"  // Nome do AP WiFi e identificação
#ifndef CURRENT_FIRMWARE_VERSION
#define CURRENT_FIRMWARE_VERSION "4.2"
#endif
static const char *DEVICE_DB_NAME = "Thermo Watch - Nitro";

// ===== CONFIGURAÇÕES OTA (Over-The-Air Updates) =====
const int OTA_CHECK_INTERVAL_WAKES = 24;       // Checar update a cada 24 wakes (24h)
const int OTA_MIN_BATTERY = 30;                // % mínimo bateria para update
const int OTA_MIN_RSSI = -75;                  // RSSI mínimo WiFi para update
const int OTA_TIMEOUT_MS = 180000;             // 3 min timeout download
const int OTA_MAX_RETRIES = 3;                 // Tentativas de download

// Parâmetros personalizados para WiFiManager
// Nome fixo (readonly) - usado para identificação OTA
const char device_name_readonly[] = "<label for='device_name'>Nome do Dispositivo (fixo para OTA)</label><input id='device_name' name='device_name' value='Thermo Watch - Nitro' readonly style='background-color:#f0f0f0;' maxlength=40>";
WiFiManagerParameter custom_device_name_display(device_name_readonly);
WiFiManagerParameter custom_device_location("device_location", "Localização", "Sala Principal", 40);
WiFiManagerParameter custom_reading_interval("reading_interval", "Intervalo Leitura (min)", "60", 4);

// Variáveis de controle
unsigned long lastReading = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastConfigMessage = 0;
const unsigned long heartbeatInterval = 60000; // 1 minuto
const unsigned long configMessageInterval = 5000; // 5 segundos
bool shouldSaveConfig = false;
bool shouldRestartAfterConfig = false;
int resetCounter = 0;
unsigned long lastResetTime = 0;

// ===== VARIÁVEIS DE RECONEXÃO ROBUSTA =====
unsigned long wifiDisconnectedSince = 0;         // Quando WiFi desconectou
int wifiReconnectAttempts = 0;                   // Contador de tentativas de reconexão
int consecutiveWifiFailures = 0;                 // Falhas consecutivas
unsigned long lastWifiReconnectAttempt = 0;      // Timestamp da última tentativa
int currentReconnectDelay = WIFI_RECONNECT_INITIAL_DELAY; // Delay atual (backoff)
bool wifiWasEverConnected = false;               // Flag se já conectou alguma vez

// ===== CONTROLE DO LED COM HARDWARE TIMER =====
hw_timer_t *ledTimer = NULL;
volatile bool ledState = false;
volatile bool ledBlinkEnabled = false;
bool statusLedAllowed = false;

// Callback para salvar configurações
void saveConfigCallback()
{
    Serial.println("⚙️ Configuração deve ser salva");
    shouldSaveConfig = true;
}

// ===== FUNÇÕES OTA (Over-The-Air Updates) =====

String sanitizeOtaUrl(String url)
{
    url.trim();
    url.replace("`", "");
    url.trim();
    url.replace(" ", "");
    url.trim();
    if (url.startsWith("\"") && url.endsWith("\"") && url.length() >= 2) {
        url = url.substring(1, url.length() - 1);
    }
    url.trim();
    return url;
}

String normalizeVersion(String v)
{
    v.trim();
    if (v.startsWith("v") || v.startsWith("V")) {
        v = v.substring(1);
    }
    v.trim();
    return v;
}

bool isSameVersion(String a, String b)
{
    return normalizeVersion(a) == normalizeVersion(b);
}

int compareVersions(String a, String b)
{
    a = normalizeVersion(a);
    b = normalizeVersion(b);

    int aStart = 0;
    int bStart = 0;

    for (int i = 0; i < 8; i++) {
        int aDot = a.indexOf('.', aStart);
        int bDot = b.indexOf('.', bStart);

        String aPart = (aDot == -1) ? a.substring(aStart) : a.substring(aStart, aDot);
        String bPart = (bDot == -1) ? b.substring(bStart) : b.substring(bStart, bDot);

        int aNum = aPart.toInt();
        int bNum = bPart.toInt();

        if (aNum < bNum) return -1;
        if (aNum > bNum) return 1;

        if (aDot == -1 && bDot == -1) return 0;
        if (aDot == -1) aStart = a.length();
        else aStart = aDot + 1;
        if (bDot == -1) bStart = b.length();
        else bStart = bDot + 1;
    }

    return 0;
}

unsigned long getEpochNow()
{
    time_t now = time(nullptr);
    if (now < 100000) return 0;
    return (unsigned long)now;
}

unsigned long getMillisNow()
{
    return millis();
}

// Verificar se deve checar por atualizações OTA
bool shouldCheckForUpdate() {
    static unsigned long lastCheckMs = 0;
    unsigned long nowMs = millis();
    if (lastCheckMs > 0 && (unsigned long)(nowMs - lastCheckMs) < 20000) {
        return false;
    }
    lastCheckMs = nowMs;

    Serial.println("🔄 OTA: Checando atualização");
    return true;
}

// Buscar versão de firmware disponível no Supabase (NITRO)
FirmwareInfo checkFirmwareUpdate() {
    FirmwareInfo info;
    info.available = false;
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ OTA: WiFi desconectado");
        return info;
    }
    
    HTTPClient http;
    String url = String(supabaseUrl) + "/rest/v1/rpc/check_firmware_update_nitro";
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, url)) {
        Serial.println("❌ OTA: Falha ao iniciar HTTPS (Supabase RPC)");
        http.end();
        return info;
    }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);
    
    // Construir body JSON com p_device_id e versão atual
    DynamicJsonDocument docPayload(512);
    docPayload["p_device_id"] = deviceId;
    docPayload["p_current_version"] = CURRENT_FIRMWARE_VERSION;
    docPayload["p_is_esp32"] = true;
    
    String body;
    serializeJson(docPayload, body);
    
    Serial.println("🔍 OTA: Consultando versão disponível via RPC Nitro...");
    Serial.println("📤 Body: " + body);
    int httpCode = http.POST(body);
    
    if (httpCode == 200) {
        String payload = http.getString();
        Serial.println("📥 Resposta RPC: " + payload);
        
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, payload);
        
        // A RPC retorna um objeto direto ou nulo
        if (!doc.isNull() && doc.containsKey("version")) {
            // Verifica se update_available é true (se a RPC retornar esse campo)
            bool updateAvailable = true;
            if (doc.containsKey("update_available")) {
                updateAvailable = doc["update_available"].as<bool>();
            }

            if (updateAvailable) {
                info.version = doc["version"].as<String>();
                // Verifica campos de URL com fallback
                if (doc.containsKey("url")) info.url = doc["url"].as<String>();
                else if (doc.containsKey("download_url")) info.url = doc["download_url"].as<String>();
                else if (doc.containsKey("firmware_url")) info.url = doc["firmware_url"].as<String>();
                
                info.url = sanitizeOtaUrl(info.url);
                if (!info.url.startsWith("http://") && !info.url.startsWith("https://") && info.url.length() > 0) {
                    info.url = String(supabaseUrl) + "/storage/v1/object/public/firmware-releases/" + info.url;
                }
                
                info.md5_hash = doc["md5_hash"].as<String>();
                if (info.md5_hash.length() == 0 && doc.containsKey("md5")) info.md5_hash = doc["md5"].as<String>();
                
                if (doc.containsKey("file_size")) info.file_size = doc["file_size"];
                else if (doc.containsKey("size")) info.file_size = doc["size"];
                
                info.mandatory = doc["mandatory"] | false; 
                info.min_battery = doc["min_battery"] | OTA_MIN_BATTERY;
                info.min_rssi = doc["min_rssi"] | OTA_MIN_RSSI;
                
                int cmp = compareVersions(info.version, CURRENT_FIRMWARE_VERSION);
                if (cmp > 0) {
                    String lastTarget = preferences.getString("ota_tgt", "");
                    unsigned long lastAttempt = preferences.getULong("ota_ts", 0);
                    int failures = preferences.getInt("ota_failures", 0);
                    unsigned long nowEpoch = getEpochNow();
                    unsigned long lastAttemptMs = preferences.getULong("ota_ms", 0);
                    unsigned long nowMs = getMillisNow();
                    unsigned long minDelayMs = 0;
                    if (failures == 1) minDelayMs = 5UL * 60UL * 1000UL;
                    else if (failures == 2) minDelayMs = 15UL * 60UL * 1000UL;
                    else if (failures >= 3) minDelayMs = 60UL * 60UL * 1000UL;

                    if (failures > 0 && nowEpoch > 0 && lastTarget == info.version && lastAttempt > 0 && (nowEpoch - lastAttempt) < 900) {
                        Serial.println("⚠️  OTA: Tentativa recente para a mesma versão. Aguardando antes de tentar novamente.");
                        http.end();
                        return info;
                    }
                    
                    if (failures > 0 && minDelayMs > 0 && lastTarget == info.version && lastAttemptMs > 0 && (unsigned long)(nowMs - lastAttemptMs) < minDelayMs) {
                        Serial.println("⚠️  OTA: Falhas recentes para a mesma versão. Aguardando antes de tentar novamente.");
                        http.end();
                        return info;
                    }

                    info.available = true;
                    Serial.printf("✨ OTA: Nova versão disponível: %s → %s\n", CURRENT_FIRMWARE_VERSION, info.version.c_str());
                    
                    if (info.mandatory) {
                        Serial.println("⚠️  OTA: Atualização OBRIGATÓRIA!");
                    }
                } else {
                    if (cmp == 0) {
                        Serial.println("✅ OTA: Versão retornada é a mesma da atual");
                    } else {
                        Serial.println("✅ OTA: Versão do servidor é menor/igual a atual");
                    }
                }
            } else {
                Serial.println("✅ OTA: RPC diz que não há atualização disponível");
            }
        } else {
             Serial.println("✅ OTA: Nenhuma atualização retornada pela RPC (objeto nulo ou vazio)");
        }
    } else {
        Serial.printf("❌ OTA: Erro ao consultar: HTTP %d\n", httpCode);
        if (httpCode > 0) Serial.println("   Resposta: " + http.getString());
    }
    
    http.end();
    return info;
}

// Validar se pode executar update
bool canPerformUpdate(FirmwareInfo &info, float batteryPercentage, int rssi) {
    if (compareVersions(info.version, CURRENT_FIRMWARE_VERSION) <= 0) {
        Serial.println("✅ OTA: Versão do servidor não é maior que a atual. Ignorando.");
        return false;
    }

    // Se obrigatória, ignora condições (exceto bateria crítica)
    if (info.mandatory) {
        if (batteryPercentage < 20) {
            Serial.println("⚠️  OTA: Bateria crítica (<20%), aguardando recarga");
            return false;
        }
        Serial.println("🔒 OTA: Atualização obrigatória, ignorando restrições");
        return true;
    }
    
    // Verificar bateria
    if (batteryPercentage < info.min_battery) {
        Serial.printf("⚠️  OTA: Bateria baixa (%.1f%% < %d%%), aguardando\n", 
                      batteryPercentage, info.min_battery);
        return false;
    }
    
    // Verificar sinal WiFi
    if (rssi < info.min_rssi) {
        Serial.printf("⚠️  OTA: Sinal WiFi fraco (%d dBm < %d dBm), aguardando\n", 
                      rssi, info.min_rssi);
        return false;
    }
    
    Serial.println("✅ OTA: Condições adequadas para atualização");
    return true;
}

// Registrar tentativa de update no Supabase (NITRO)
String logUpdateAttempt(String fromVersion, String toVersion, String status, String error = "") {
    HTTPClient http;
    String deviceId = preferences.getString("device_id", "");
    float batteryPercentage = preferences.getFloat("last_battery_pct", 0);
    int rssi = WiFi.RSSI();
    
    String url = String(supabaseUrl) + "/rest/v1/device_updates_nitro";
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, url)) {
        Serial.println("❌ OTA: Erro ao iniciar HTTPS (device_updates_nitro)");
        http.end();
        return "";
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Prefer", "return=representation");
    
    DynamicJsonDocument doc(512);
    doc["device_id"] = deviceId;
    doc["from_version"] = fromVersion;
    doc["to_version"] = toVersion;
    doc["status"] = status;
    doc["battery_level"] = batteryPercentage;
    doc["wifi_rssi"] = rssi;
    
    if (error.length() > 0) {
        doc["error_message"] = error;
    }
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    int httpCode = http.POST(jsonData);
    String updateId = "";
    
    if (httpCode == 201) {
        String response = http.getString();
        DynamicJsonDocument respDoc(512);
        DeserializationError jsonErr = deserializeJson(respDoc, response);
        if (!jsonErr && respDoc.is<JsonArray>() && respDoc.size() > 0) {
            updateId = respDoc[0]["id"].as<String>();
        }
        Serial.printf("📝 OTA: Registrado no banco NITRO (ID: %s)\n", updateId.c_str());
    } else {
        Serial.printf("❌ OTA: Erro ao registrar log: %d\n", httpCode);
        if (httpCode > 0) Serial.println("   Resposta: " + http.getString());
    }
    
    http.end();
    return updateId;
}

// Atualizar status do log no Supabase (NITRO)
void updateLogStatus(String updateId, String status, String error = "", int duration = 0, int bytes = 0) {
    if (updateId.length() == 0) return;
    
    HTTPClient http;
    String url = String(supabaseUrl) + "/rest/v1/device_updates_nitro?id=eq." + updateId;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, url)) {
        http.end();
        return;
    }
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(512);
    doc["status"] = status;
    
    if (error.length() > 0) doc["error_message"] = error;
    if (duration > 0) doc["duration_seconds"] = duration;
    if (bytes > 0) doc["download_bytes"] = bytes;
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    int httpCode = http.PATCH(jsonData);
    if (httpCode != 200 && httpCode != 204) {
         Serial.printf("❌ OTA: Erro ao atualizar status: %d\n", httpCode);
    }
    http.end();
}

// Executar OTA update
bool performOTAUpdate(FirmwareInfo &info) {
    Serial.println("\n🚀 ========== INICIANDO OTA UPDATE ==========");
    
    unsigned long startTime = millis();
    String updateId = logUpdateAttempt(CURRENT_FIRMWARE_VERSION, info.version, "downloading");

    preferences.putString("ota_tgt", info.version);
    unsigned long nowEpoch = getEpochNow();
    if (nowEpoch > 0) {
        preferences.putULong("ota_ts", nowEpoch);
    }
    preferences.putULong("ota_ms", getMillisNow());
    
    // Configurar LED
    digitalWrite(LED_PIN, HIGH);
    
    // Executar update com WiFiClientSecure (HTTPS) e headers customizados
    Serial.printf("📡 Baixando firmware de: %s\n", info.url.c_str());
    updateLogStatus(updateId, "downloading");
    
    // Configurar WiFiClientSecure para HTTPS (Supabase/Cloudflare)
    WiFiClientSecure client;
    client.setInsecure(); // Desabilitar verificação SSL para simplificar
    client.setTimeout(30000);
    
    // Configurar HTTPClient com headers obrigatórios (previne 400 Bad Request do Cloudflare)
    HTTPClient http;
    http.begin(client, info.url);
    http.addHeader("User-Agent", "ESP32-ThermoWatch/3.0.9");
    http.addHeader("Accept", "application/octet-stream, */*");
    http.addHeader("Accept-Encoding", "identity");
    http.addHeader("Cache-Control", "no-cache");
    http.addHeader("Connection", "close");
    http.setTimeout(30000); // 30 segundos timeout
    
    Serial.println("🌐 Iniciando requisição HTTP GET...");
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        String error = "HTTP Code is (" + String(httpCode) + ")";
        Serial.printf("❌ OTA FALHOU: %s (Erro %d)\n", error.c_str(), httpCode);
        updateLogStatus(updateId, "failed", error, (millis() - startTime) / 1000);
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    int contentLength = http.getSize();
    Serial.printf("📦 Tamanho do firmware: %d bytes\n", contentLength);
    
    if (contentLength <= 0) {
        String error = "Invalid content length: " + String(contentLength);
        Serial.printf("❌ OTA FALHOU: %s\n", error.c_str());
        updateLogStatus(updateId, "failed", error, (millis() - startTime) / 1000);
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    if (contentLength < 200000) {
        String error = "Firmware too small: " + String(contentLength);
        Serial.printf("❌ OTA FALHOU: %s\n", error.c_str());
        updateLogStatus(updateId, "failed", error, (millis() - startTime) / 1000, contentLength);
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    if (info.file_size > 0 && contentLength != info.file_size) {
        String error = "Content-Length (" + String(contentLength) + ") != file_size (" + String(info.file_size) + ")";
        Serial.printf("❌ OTA FALHOU: %s\n", error.c_str());
        updateLogStatus(updateId, "failed", error, (millis() - startTime) / 1000, contentLength);
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    if (info.md5_hash.length() == 32) {
        Update.setMD5(info.md5_hash.c_str());
    }
    
    // Iniciar update
    if (!Update.begin(contentLength)) {
        String error = "Update.begin failed";
        Serial.printf("❌ OTA FALHOU: %s\n", error.c_str());
        updateLogStatus(updateId, "failed", error, (millis() - startTime) / 1000);
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    // Baixar e instalar
    Serial.println("⏬ Baixando e instalando firmware...");
    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    uint8_t buff[1024];
    unsigned long lastDataMs = millis();

    while (http.connected() && written < (size_t)contentLength) {
        size_t available = stream->available();
        if (available) {
            size_t toRead = available;
            if (toRead > sizeof(buff)) toRead = sizeof(buff);
            int readBytes = stream->readBytes(buff, toRead);
            if (readBytes > 0) {
                size_t w = Update.write(buff, (size_t)readBytes);
                if (w != (size_t)readBytes) {
                    break;
                }
                written += w;
                lastDataMs = millis();
            }
        } else {
            if (millis() - lastDataMs > 30000) {
                break;
            }
            delay(1);
            yield();
        }
    }
    
    unsigned long duration = (millis() - startTime) / 1000;
    
    if (written != contentLength) {
        String error = "Written (" + String(written) + ") != Content length (" + String(contentLength) + ")";
        Serial.printf("❌ OTA FALHOU: %s\n", error.c_str());
        updateLogStatus(updateId, "failed", error, duration);
        Update.abort();
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    if (!Update.end()) {
        String error = "Update.end failed";
        Serial.printf("❌ OTA FALHOU: %s\n", error.c_str());
        updateLogStatus(updateId, "failed", error, duration);
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    if (!Update.isFinished()) {
        String error = "Update not finished";
        Serial.printf("❌ OTA FALHOU: %s\n", error.c_str());
        updateLogStatus(updateId, "failed", error, duration);
        http.end();
        digitalWrite(LED_PIN, LOW);
        
        int failures = preferences.getInt("ota_failures", 0) + 1;
        preferences.putInt("ota_failures", failures);
        return false;
    }
    
    Serial.println("✅ OTA: Firmware baixado e instalado com sucesso!");
    updateLogStatus(updateId, "success", "", duration, contentLength);
    
    preferences.putInt("ota_failures", 0);
    preferences.putString("pending_version", info.version);
    
    http.end();
    digitalWrite(LED_PIN, LOW);
    
    Serial.println("🔄 Reiniciando em 3 segundos...");
    delay(3000);
    ESP.restart();
    return true;
}

// Validar update após reboot
void validateOTAUpdate() {
    String pendingVersion = preferences.getString("pending_version", "");
    
    if (pendingVersion.length() > 0 && pendingVersion == CURRENT_FIRMWARE_VERSION) {
        Serial.printf("✅ OTA: Update validado! Versão %s operacional\n", CURRENT_FIRMWARE_VERSION);
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("⚠️ OTA: WiFi desconectado - validação no servidor será feita quando conectar");
            return;
        }

        HTTPClient http;
        String deviceId = preferences.getString("device_id", "");
        String url = String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId;
        WiFiClientSecure client;
        if (!beginSupabaseRequest(http, client, url)) {
            http.end();
            return;
        }
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Prefer", "return=minimal");
        
        DynamicJsonDocument doc(256);
        doc["current_firmware"] = CURRENT_FIRMWARE_VERSION;
        
        String jsonData;
        serializeJson(doc, jsonData);
        int httpCode = http.PATCH(jsonData);
        http.end();
        
        if (httpCode == 200 || httpCode == 204) {
            preferences.remove("pending_version");
        }
    }
}

// ============================================================================
// 🚨 SISTEMA DE ALERTAS DE TEMPERATURA
// ============================================================================

/**
 * Verifica temperatura e ativa alertas visuais/sonoros
 * - T > 150°C: LED2 + Buzzer piscam (crítico)
 * - 120°C < T ≤ 150°C: APENAS LED1 pisca (elevado - sem buzzer)
 * - T ≤ 120°C: Todos desligados (normal)
 * NUNCA os dois LEDs piscam juntos - lógica excludente
 */
void checkTemperatureAlerts(float temperature) {
    static unsigned long lastToggle = 0;
    static bool alertState = false;
    unsigned long currentMillis = millis();
    
    // === 3 ZONAS DE TEMPERATURA ===
    // ZONA 1: T < -150°C → NORMAL (sem alertas)
    // ZONA 2: -150°C ≤ T ≤ -120°C → ALERTA FRIO (LED1)
    // ZONA 3: T > -120°C → ALERTA QUENTE (LED2 + Buzzer)
    
    // ZONA 1: Temperatura NORMAL (< -150°C) - desliga IMEDIATAMENTE
    if (temperature < TEMP_LOW_THRESHOLD) {
        digitalWrite(TEMP_ALERT_LED1, LOW);
        digitalWrite(TEMP_ALERT_LED2, LOW);
        digitalWrite(TEMP_ALERT_BUZZER, LOW);
        return;
    }
    
    // Piscar a cada 500ms
    if (currentMillis - lastToggle >= TEMP_ALERT_INTERVAL) {
        lastToggle = currentMillis;
        alertState = !alertState;
        
        // ZONA 2: ALERTA FRIO (-150°C ≤ T ≤ -120°C) - APENAS LED1 pisca
        if (temperature >= TEMP_LOW_THRESHOLD && temperature <= TEMP_LOW_ALERT_THRESHOLD) {
            digitalWrite(TEMP_ALERT_LED2, LOW);  // LED2 sempre desligado
            digitalWrite(TEMP_ALERT_BUZZER, LOW);  // Buzzer sempre desligado
            digitalWrite(TEMP_ALERT_LED1, alertState ? HIGH : LOW);
            
            if (alertState) {
                Serial.printf("❄️ ALERTA FRIO: %.1f°C (%.0f a %.0f°C) - LED1 ATIVO\n", 
                             temperature, TEMP_LOW_THRESHOLD, TEMP_LOW_ALERT_THRESHOLD);
            }
        }
        // ZONA 3: ALERTA QUENTE (T > -120°C) - LED2 + Buzzer piscam juntos
        else if (temperature > TEMP_LOW_ALERT_THRESHOLD) {
            digitalWrite(TEMP_ALERT_LED2, alertState ? HIGH : LOW);
            digitalWrite(TEMP_ALERT_BUZZER, alertState ? HIGH : LOW);
            digitalWrite(TEMP_ALERT_LED1, LOW);  // LED1 sempre desligado
            
            if (alertState) {
                Serial.printf("🚨 ALERTA QUENTE: %.1f°C (>%.0f°C) - LED2+BUZZER ATIVOS\n", 
                             temperature, TEMP_LOW_ALERT_THRESHOLD);
            }
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    
    // ⚠️ VERIFICAÇÃO CRÍTICA: Verificar IMEDIATAMENTE se acordou de deep sleep
    // ANTES de qualquer outra operação que possa consumir tempo
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool isWakeFromSleep = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER || wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
    
    if (isWakeFromSleep) {
        Serial.println("\n💤 ========================================");
        Serial.println("💤 ACORDOU DE DEEP SLEEP - MODO BATERIA");
        Serial.println("💤 ========================================");
        Serial.println("✅ Reset counter será ZERADO (não é reset manual)\n");
        
        // Inicializar apenas o essencial para zerar o contador
        preferences.begin("thermowatch", false);
        preferences.putInt("reset_counter", 0);
        preferences.putULong("last_reset_ts", 0);
        preferences.end();
    }
    
    Serial.println();
    Serial.println("🌡️ ThermoWatch ESP32 - Modo Bateria com Deep Sleep");
    Serial.println("====================================================================");
    
    // Mostrar razão do acordar e estatísticas
    Serial.println("DEBUG: Chamando showWakeupReason()...");
    Serial.flush();
    showWakeupReason();
    
    Serial.println("DEBUG: Chamando showBatteryStats()...");
    Serial.flush();
    showBatteryStats();

    // Configurar pinos
    Serial.println("DEBUG: Configurando pinos...");
    Serial.flush();
    pinMode(LED_PIN, OUTPUT);
    pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BATTERY_PIN, INPUT); // Configurar pino da bateria como entrada analógica
    digitalWrite(LED_PIN, LOW);

    // Configurar sistema de alarme de temperatura
    pinMode(TEMP_ALERT_LED1, OUTPUT);
    pinMode(TEMP_ALERT_LED2, OUTPUT);
    pinMode(TEMP_ALERT_BUZZER, OUTPUT);
    digitalWrite(TEMP_ALERT_LED1, LOW);
    digitalWrite(TEMP_ALERT_LED2, LOW);
    digitalWrite(TEMP_ALERT_BUZZER, LOW);
    Serial.println("🚨 Sistema de alarme de temperatura configurado:");
    Serial.printf("   LED1 (GPIO%d): Frio %.0f a %.0f°C (sem buzzer)\n", 
                  TEMP_ALERT_LED1, TEMP_LOW_THRESHOLD, TEMP_LOW_ALERT_THRESHOLD);
    Serial.printf("   LED2 (GPIO%d) + Buzzer (GPIO%d): Quente >%.0f°C\n", 
                  TEMP_ALERT_LED2, TEMP_ALERT_BUZZER, TEMP_LOW_ALERT_THRESHOLD);
    
    // Configurar ADC (bateria)
    analogSetAttenuation(ADC_11db);  // Faixa completa 0-3.3V
    analogReadResolution(12);         // Resolução 12-bit (0-4095)
    
    // Inicializar timer do LED
    initLedTimer();
    
    // Verificar status da bateria
    Serial.println("DEBUG: Chamando displayBatteryInfo()...");
    Serial.flush();
    displayBatteryInfo();

    // Inicializar MAX31865 (PT100 via SPI)
    Serial.println("DEBUG: Inicializando MAX31865 (PT100 via SPI)...");
    Serial.flush();
    Serial.println("🔌 MAX31865 pinos (ESP32):");
    Serial.printf("   MOSI(SDI)=GPIO%d | MISO(SDO)=GPIO%d | SCK=GPIO%d | CS=GPIO%d\n",
                  MAX31865_MOSI_PIN, MAX31865_MISO_PIN, MAX31865_SCK_PIN, MAX31865_CS_PIN);
    Serial.printf("   RREF=%.0fΩ | RNOMINAL=%.0fΩ | WIRES=%d\n", MAX31865_RREF, MAX31865_RNOMINAL, MAX31865_WIRES);
    initMAX31865();
    
    // Teste de leitura PT100 (MAX31865)
    float testTemp = readPT100Temperature();
    if (testTemp < -270 || testTemp > 850) {
        Serial.println("⚠️ AVISO: Leitura PT100 fora da faixa operacional!");
        Serial.println("   Faixa válida: -270°C (criogênico) a +850°C");
        Serial.println("   Verifique o circuito e conexões.");
    } else {
        Serial.printf("✅ PT100 inicializado: %.2f°C\n", testTemp);
        if (testTemp < -200) {
            Serial.println("❄️ MODO CRIOGÊNICO: Temperatura abaixo de -200°C detectada");
        }
    }

    // Inicializar Preferences (EEPROM)
    Serial.println("DEBUG: Inicializando Preferences...");
    Serial.flush();
    preferences.begin("thermowatch", false);

    // VALIDAR OTA UPDATE (se acabou de reiniciar após atualização)
    Serial.println("DEBUG: Validando OTA update...");
    Serial.flush();
    validateOTAUpdate();

    // Carregar configurações salvas
    Serial.println("DEBUG: Chamando loadSavedConfig()...");
    Serial.flush();
    loadSavedConfig();

    // Verificar se deve entrar em modo configuração
    Serial.println("DEBUG: Chamando checkConfigMode()...");
    Serial.flush();
    checkConfigMode();
    
    // Inicializar WiFiManager para verificar credenciais salvas
    WiFi.mode(WIFI_STA);
    wm.setClass("invert");

    // MODO BATERIA: Fazer leitura rápida e dormir
    if (!isConfigMode()) 
    {
        // Piscar LED para indicar acordar
        blinkLED(1, 100);
        
        // Fazer medição rápida com 5 tentativas em cada passo
        float currentTemp = performQuickReading();
        
        // VERIFICAR SE TEMPERATURA ESTÁ EM ZONA DE ALERTA
        // Zona alerta FRIO: -150°C a -120°C (LED1)
        // Zona alerta QUENTE: > -120°C (LED2+Buzzer)
        bool isInAlertZone = (currentTemp >= TEMP_LOW_THRESHOLD);
        
        if (isInAlertZone) {
            Serial.println("\n🚨 ════════════════════════════════════════════════════");
            if (currentTemp > TEMP_LOW_ALERT_THRESHOLD) {
                Serial.printf("🚨  TEMPERATURA QUENTE EM ALERTA: %.1f°C (>%.0f°C)\n", 
                             currentTemp, TEMP_LOW_ALERT_THRESHOLD);
            } else {
                Serial.printf("❄️  TEMPERATURA FRIA EM ALERTA: %.1f°C (%.0f a %.0f°C)\n", 
                             currentTemp, TEMP_LOW_THRESHOLD, TEMP_LOW_ALERT_THRESHOLD);
            }
            Serial.println("🚨  ESP32 PERMANECERÁ ACORDADO até normalizar!");
            Serial.println("🚨  Sleep CANCELADO - Monitoramento contínuo ativo");
            Serial.println("🚨 ════════════════════════════════════════════════════\n");
            
            // Entrar em loop de monitoramento contínuo até temperatura normalizar
            monitorTemperatureUntilSafe(currentTemp);
        }
        
        // Temperatura normal (< -150°C) - pode dormir
        Serial.println("✅ Temperatura normal - prosseguindo para sleep");
        
        // Configurar timer para acordar em 4 horas
        enterDeepSleep();
    }
    
    // Se chegou aqui, está em modo configuração contínua
    Serial.println("📋 Modo Configuração ativo - funcionamento contínuo (sleep desabilitado)");
    Serial.println("💡 LED: ACESO FIXO durante configuração");
    
    // LED aceso durante todo o processo de configuração
    setStatusLedAllowed(true);
    ledConfigMode();
    
    // Configurar WiFiManager
    setupWiFiManager();

    // Conectar WiFi
    connectWiFi();

    // Executar testes de conectividade sequenciais
    performConnectivityTests();

    // ===== GEOLOCALIZAÇÃO NO MODO CONFIGURAÇÃO =====
    // Executar geolocalização uma vez após configurar WiFi
    if (useGeolocation && shouldUpdateGeolocation()) {
        Serial.println("\n📍 Executando geolocalização inicial...");
        if (getWiFiGeolocation()) {
            Serial.println("📍 Sincronizando coordenadas com banco...");
            syncDeviceGeolocation();
            Serial.println("✅ Geolocalização concluída!");
        } else {
            Serial.println("⚠️ Falha na geolocalização - tentará novamente no próximo boot");
        }
    }

    Serial.println("✅ ThermoWatch iniciado com sucesso!");
    Serial.println("💡 LED: Piscando 3x = sucesso");
    blinkLED(3, 200); // 3 piscadas rápidas = sucesso
    ledConfigMode(); // Volta para LED aceso após as piscadas
}

void loadSavedConfig()
{
    Serial.println("📖 Carregando configurações salvas...");

    deviceName = preferences.getString("device_name", DEVICE_DB_NAME);
    if (deviceName != DEVICE_DB_NAME) {
        deviceName = DEVICE_DB_NAME;
        preferences.putString("device_name", deviceName);
    }
    deviceLocation = preferences.getString("device_location", "Localização não definida");
    readingIntervalSec = preferences.getInt("read_interval", 3600);
    tempOffset = preferences.getFloat("temp_offset", TEMP_OFFSET);

    // Gerar ID único baseado no MAC address se não existir
    deviceId = preferences.getString("device_id", "");
    
    // Migração: Se o ID começa com "ESP32_", atualizar para "ThermoWatch_"
    if (deviceId.startsWith("ESP32_")) {
        Serial.println("🔄 Migrando device_id de ESP32_ para ThermoWatch_...");
        deviceId.replace("ESP32_", "ThermoWatch_");
        preferences.putString("device_id", deviceId);
        Serial.println("✅ Migração concluída: " + deviceId);
    }
    
    // Se não existe ID, criar novo com prefixo ThermoWatch_
    if (deviceId.length() == 0)
    {
        deviceId = "ThermoWatch_" + WiFi.macAddress().substring(9); // Últimos 8 caracteres do MAC
        deviceId.replace(":", "");
        preferences.putString("device_id", deviceId);
    }

    deviceDescription = "Sensor de temperatura Thermo Watch - Nitro em " + deviceLocation;

    Serial.println("📋 Configurações carregadas:");
    Serial.println("   ID: " + deviceId);
    Serial.println("   Nome: " + deviceName);
    Serial.println("   Local: " + deviceLocation);
    Serial.println("   Intervalo: " + String(readingIntervalSec) + "s (" + String(readingIntervalSec / 60) + " minutos)");
}

void checkConfigMode()
{
    Serial.println("🔄 Verificando modo de configuração...");
    
    // Verificar apenas botão BOOT pressionado
    bool buttonPressed = false;
    if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
        Serial.println("🔘 Botão BOOT pressionado - aguardando 5 segundos...");
        blinkLED(3, 100);
        unsigned long buttonStart = millis();
        int seconds = 0;
        
        while (digitalRead(CONFIG_BUTTON_PIN) == LOW && (millis() - buttonStart) < 5000) {
            int newSeconds = (millis() - buttonStart) / 1000;
            if (newSeconds > seconds) {
                seconds = newSeconds;
                Serial.printf("⏳ %d/5 segundos...\n", seconds);
                blinkLED(1, 50);
            }
            delay(50);
        }
        
        if ((millis() - buttonStart) >= 4900) {
            Serial.println("✅ Botão mantido por 5 segundos!");
            buttonPressed = true;
            blinkLED(5, 200);
            
            Serial.println("\n🔧 ========================================");
            Serial.println("🔧  MODO RECONFIGURAÇÃO FORÇADA ATIVADO!");
            Serial.println("🔧 ========================================");
            Serial.println("   Razão: Botão BOOT mantido por 5s");
            
            wm.resetSettings();
            Serial.println("🔧 WiFi limpo - portal será iniciado...\n");
            
            // CRÍTICO: Resetar também a flag de geolocalização para forçar nova leitura
            preferences.putBool("geo_completed", false);
            preferences.putULong("last_geo_update", 0);
            preferences.remove("device_location"); // Limpar localização salva
            
            // Forçar limpeza de qualquer outra variável de localização na flash
            latitude = 0.0;
            longitude = 0.0;
            geoAccuracy = 0.0;
            geoLocationValid = false;
            
            Serial.println("📍 Resetando histórico de geolocalização (forçar nova leitura)");
            
            blinkLED(10, 100);
            delay(1000);
        } else {
            Serial.printf("❌ Botão solto após %lums\n", (millis() - buttonStart));
        }
    }
    
    if (!buttonPressed) {
        Serial.println("✅ Modo normal - configurações preservadas\n");
    }
}

void setupWiFiManager()
{
    Serial.println("🔧 Configurando WiFiManager...");
    Serial.println("💡 LED: ACESO FIXO durante configuração");
    
    // LED aceso durante configuração
    ledConfigMode();

    // Configurar callback
    wm.setSaveConfigCallback(saveConfigCallback);

    // Adicionar parâmetros customizados
    // NOTA: device_name é READONLY - usado para identificação OTA no servidor
    wm.addParameter(&custom_device_name_display);  // Exibido mas não editável
    wm.addParameter(&custom_device_location);
    wm.addParameter(&custom_reading_interval);

    // Configurações do portal
    wm.setConfigPortalTimeout(300); // 5 minutos timeout
    wm.setConnectTimeout(20);       // 20 segundos para conectar
    wm.setDebugOutput(false);       // Desabilitar debug para evitar conflitos

    // CSS + Logo no head element
    const char* customHead = R"rawliteral(
<style>
body{background:#1e3a8a!important;color:#fff!important}
.lc{text-align:center;margin:15px 0;display:flex;flex-direction:column;align-items:center;gap:6px}
.lc img{width:70px;height:auto;border-radius:8px;box-shadow:0 3px 10px rgba(0,0,0,0.4)}
.lt{font-size:15px;font-weight:bold;color:#60a5fa;letter-spacing:1px}
h1{text-align:center!important;color:#fff!important;margin-top:10px!important}
</style>
<div class="lc">
<img src="data:image/jpeg;base64,/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAAuACgDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD3esbxJrNxounefa2L3kzYAUHAX3Pcj2Fad1dQ2VrLc3EgjhiUs7EE4H4VxKeMbTxDeXEdvE0dtagFZZD80hbP8Pbp9a5cbXlQoOrFXat+aNaFNVKih3M7wTr2o674yea+uTIBbSbY14ROV4C9v516bXmkmow6Rqa6va2URnwYnJypkU9c478Dmuz0bxLp2tKFgk8u4xzBJw34eo+lZ4THU8RHs+x018vr0Y87V49/8zYoooruOEyfE/8AyLOof9cf6ivJYrENeR3EZaFw4JdDjcM8g+ua9suLeK6t3gnQPFINrKe4rjtT8HSwv52nMZUzkxOfmH0PevRwdSg6bo1lo++xx4lVYyVSlujB8XC0tdFN7ZM7gSqrW7/eTOec91rlPBd7Pd+PdH8xvlE5wg4A+Rq9A13RtTtdGV7WPzruaVYlhRN+AQck9u30pPCnw8OmX9vq2pSj7XEd0cMPRTgj5m79eg4rzY4XC0Jt0lv+Hoe5l+dYl4aVLFR3TSfXVdUd/wBqKKKZ55xev69qOl+J0868+z6Oht1Jto4Zm3u+0iZWYSKDlQpQHqSawL/4g69a6FraJaRnUobi6ezuPLzELWGVkd37bk27cdy6Hua7NxbT3UN/c6bYS3UUW9Z2hBkTDY+ViMj2q6Qrw3Futra/Z/MkV4mjyr5BY5HTkkZ9eaAOW1Dx21p46jsQ6jSYJY7K7byWP76QZDeZjaAhMakE9ZD6VQ03xRrt/wCI109NQctcX99amN9PVI4Ioi4Ekcp4kdSI8r82cnIGM12nDrLbfYrNrd7krNGU4bJyWIxhjkg5PWpIUV7e0kWztN/nNIfkxsckhnXj7xycnvk80AZXgy41a9h1CbUtU+2CG+uLREFskYAikKbsr1JA57UVqWVw3nxRx29vFFL5kj+WMEvnk++Tkk0UAf/Z" alt="Logo"/>
<div class="lt">SMART SOLUTIONS HUB</div>
</div>
)rawliteral";


    // Aplicar CSS + logo ao portal
    wm.setCustomHeadElement(customHead);

    // Definir nome do AP
    String apName = FIRMWARE_VERSION;
    wm.setHostname(apName.c_str());
}

void connectWiFi()
{
    Serial.println("📡 Conectando ao WiFi...");
    setStatusLedAllowed(true);
    startLedBlink();

    String apName = FIRMWARE_VERSION;

    // Tentar conectar
    if (!wm.autoConnect(apName.c_str(), "12345678"))
    {
        Serial.println("❌ Falha na conexão WiFi");
        Serial.println("🔄 Reiniciando ESP32...");
        delay(3000);
        ESP.restart();
    }

    // Salvar configurações se necessário
    if (shouldSaveConfig)
    {
        saveCustomConfig();
    }

    Serial.println("✅ WiFi conectado!");
    Serial.println("📶 SSID: " + WiFi.SSID());
    Serial.println("🌐 IP: " + WiFi.localIP().toString());
    Serial.println("📡 RSSI: " + String(WiFi.RSSI()) + " dBm");

    validateOTAUpdate();

    // Sincronizar horário via NTP
    syncNTP();

    blinkLED(2, 500); // 2 piscadas lentas = WiFi conectado

    if (shouldRestartAfterConfig) {
        Serial.println("🔄 Reiniciando para aplicar configuração...");
        Serial.flush();
        delay(1000);
        ESP.restart();
    }
}

void saveCustomConfig()
{
    Serial.println("💾 Salvando configurações personalizadas...");

    // DEVICE_NAME é FIXO ("Thermo Watch - Nitro") - não pode ser alterado
    String newDeviceName = DEVICE_DB_NAME;
    String newDeviceLocation = custom_device_location.getValue();
    int newReadingInterval = String(custom_reading_interval.getValue()).toInt();

    Serial.println("📝 Valores recebidos do portal:");
    Serial.println("   Nome: " + newDeviceName + " (FIXO - não editável)");
    Serial.println("   Local: " + newDeviceLocation);
    Serial.println("   Intervalo (min): " + String(newReadingInterval));

    // Validar intervalo (mínimo 1 min, máximo 999 min)
    if (newReadingInterval < 1)
        newReadingInterval = 1;
    if (newReadingInterval > 999)
        newReadingInterval = 999;
    
    // Converter minutos para segundos
    newReadingInterval = newReadingInterval * 60;

    // Salvar na EEPROM (device_name sempre fixo)
    preferences.putString("device_name", newDeviceName);  // Garantir que sempre seja o nome fixo
    preferences.putString("device_location", newDeviceLocation);
    preferences.putInt("read_interval", newReadingInterval);

    // Atualizar variáveis globais
    deviceName = newDeviceName;
    deviceLocation = newDeviceLocation;
    readingIntervalSec = newReadingInterval;
    deviceDescription = "Sensor de temperatura Thermo Watch - Nitro em " + deviceLocation;

    Serial.println("✅ Configurações salvas na memória:");
    Serial.println("   Nome: " + deviceName);
    Serial.println("   Local: " + deviceLocation);
    Serial.println("   Intervalo: " + String(readingIntervalSec) + "s (" + String(readingIntervalSec / 60) + " minutos)");

    // Resetar flag de geolocalização quando WiFi é reconfigurado
    Serial.println("📍 Resetando flag de geolocalização (WiFi reconfigurado)...");
    preferences.remove("geo_completed");
    preferences.remove("last_geo_update");
    Serial.println("✅ Próximo boot irá obter e enviar geolocalização novamente");

    shouldSaveConfig = false;
    shouldRestartAfterConfig = true;
    
    Serial.println("✅ Configurações salvas com sucesso!");
    Serial.println("💡 LED: APAGADO");
    ledOff();
}

bool testInternetConnection()
{
    Serial.println("\n[2/4] Testando conectividade internet (ping Google - 5 tentativas)...");
    
    for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++) {
        Serial.printf("   🔄 [%d/%d] Tentando ping Google...\n", attempt, MAX_RETRY_ATTEMPTS);
        
        HTTPClient http;
        http.begin("http://www.google.com");
        http.setTimeout(5000);
        
        int httpCode = http.GET();
        http.end();
        
        if (httpCode > 0) {
            Serial.printf("   ✅ Internet OK na tentativa %d | HTTP Code: %d\n", attempt, httpCode);
            return true;
        }
        
        Serial.printf("   ❌ Tentativa %d falhou | Erro: %s\n", attempt, http.errorToString(httpCode).c_str());
        
        if (attempt < MAX_RETRY_ATTEMPTS) {
            Serial.printf("   ⏳ Aguardando %ds...\n", RETRY_INTERVAL_SECONDS);
            delay(RETRY_INTERVAL_SECONDS * 1000);
        }
    }
    
    Serial.printf("   ❌ Internet INDISPONÍVEL após %d tentativas\n", MAX_RETRY_ATTEMPTS);
    return false;
}

bool testSupabaseConnection()
{
    Serial.println("\n[3/4] Testando conexão com BD Supabase (5 tentativas)...");
    
    String testUrl = String(supabaseUrl) + "/auth/v1/health";
    
    for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++) {
        Serial.printf("   🔄 [%d/%d] Tentando conectar Supabase...\n", attempt, MAX_RETRY_ATTEMPTS);
        
        HTTPClient http;
        WiFiClientSecure client;
        if (!beginSupabaseRequest(http, client, testUrl)) {
            Serial.println("   ❌ Falha ao iniciar HTTPS");
            http.end();
            continue;
        }
        http.setTimeout(8000);
        
        int httpCode = http.GET();
        String response = "";
        
        if (httpCode == 200) {
            response = http.getString();
            Serial.printf("   ✅ Supabase conectado na tentativa %d | HTTP Code: %d\n", attempt, httpCode);
            Serial.printf("   📡 URL: %s\n", testUrl.c_str());
            Serial.printf("   📄 Resposta (primeiros 100 chars): %s\n", response.substring(0, 100).c_str());
            http.end();
            return true;
        }
        
        Serial.printf("   ❌ Tentativa %d falhou | HTTP Code: %d\n", attempt, httpCode);
        Serial.printf("   ⚠️ Erro: %s\n", http.errorToString(httpCode).c_str());
        if (http.connected()) {
            response = http.getString();
            Serial.printf("   📄 Resposta (primeiros 100 chars): %s\n", response.substring(0, 100).c_str());
        }
        http.end();
        
        if (attempt < MAX_RETRY_ATTEMPTS) {
            Serial.printf("   ⏳ Aguardando %ds...\n", RETRY_INTERVAL_SECONDS);
            delay(RETRY_INTERVAL_SECONDS * 1000);
        }
    }
    
    Serial.printf("   ❌ Supabase INDISPONÍVEL após %d tentativas\n", MAX_RETRY_ATTEMPTS);
    return false;
}

void performConnectivityTests()
{
    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.println("║   INICIANDO TESTES DE CONECTIVIDADE          ║");
    Serial.println("╚════════════════════════════════════════════════╝\n");
    
    // [1/4] WiFi
    Serial.println("[1/4] Verificando conexão WiFi...");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("   ✅ WiFi conectado\n");
        Serial.printf("   📶 SSID: %s\n", WiFi.SSID().c_str());
        Serial.printf("   🌐 IP Local: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("   📡 RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("   🔐 MAC: %s\n", WiFi.macAddress().c_str());
    } else {
        Serial.println("   ❌ WiFi desconectado");
        Serial.println("\n❌ TESTE ABORTADO - Sem WiFi\n");
        return;
    }
    
    delay(500);
    
    // [2/4] Internet
    bool internetOk = testInternetConnection();
    if (!internetOk) {
        Serial.println("\n⚠️ AVISO: Sem acesso à internet\n");
        return;
    }
    
    delay(500);
    
    // [3/4] Supabase
    bool supabaseOk = testSupabaseConnection();
    if (!supabaseOk) {
        Serial.println("\n❌ ERRO: Falha na conexão com Supabase\n");
        return;
    }
    
    delay(500);
    
    // [4/4] Teste de escrita no BD
    Serial.println("\n[4/4] Testando escrita no BD (registro dispositivo)...");
    registerDevice();
    
    Serial.println("\n╔════════════════════════════════════════════════╗");
    Serial.println("║   ✅ TODOS OS TESTES CONCLUÍDOS               ║");
    Serial.println("╚════════════════════════════════════════════════╝\n");
}

void loop()
{
    // ⚠️ ATENÇÃO: Em modo bateria, o ESP32 não deve chegar aqui
    // Se chegou, significa que está em modo configuração
    
    unsigned long currentMillis = millis();
    
    // ===== VERIFICAÇÃO E RECONEXÃO WIFI ROBUSTA =====
    if (WiFi.status() != WL_CONNECTED)
    {
        // Primeira vez que detectou desconexão?
        if (wifiDisconnectedSince == 0) {
            wifiDisconnectedSince = currentMillis;
            Serial.println("\n❌ ═══════════════════════════════════════════");
            Serial.println("❌ WiFi DESCONECTADO - Iniciando protocolo de reconexão");
            Serial.println("❌ ═══════════════════════════════════════════");
        }
        
        // Calcular tempo sem WiFi
        unsigned long disconnectedTime = currentMillis - wifiDisconnectedSince;
        
        // Verificar watchdog (5 minutos sem WiFi = restart)
        if (disconnectedTime > WIFI_WATCHDOG_TIMEOUT) {
            Serial.println("\n🚨 WATCHDOG: 5 minutos sem WiFi - reiniciando ESP32...");
            Serial.printf("📊 Estatísticas: %d tentativas, %d falhas consecutivas\n", 
                         wifiReconnectAttempts, consecutiveWifiFailures);
            preferences.putInt("wifi_failures", preferences.getInt("wifi_failures", 0) + 1);
            delay(1000);
            ESP.restart();
        }
        
        // Verificar se é hora de tentar reconectar (com backoff)
        if (currentMillis - lastWifiReconnectAttempt >= currentReconnectDelay) {
            handleWiFiDisconnection();
            lastWifiReconnectAttempt = currentMillis;
        } else {
            // Mostrar status de espera periodicamente
            static unsigned long lastWaitMsg = 0;
            if (currentMillis - lastWaitMsg > 10000) {
                Serial.printf("⏳ Aguardando %ds para próxima tentativa (%d/%d)...\n",
                             (currentReconnectDelay - (currentMillis - lastWifiReconnectAttempt)) / 1000,
                             wifiReconnectAttempts, WIFI_RECONNECT_MAX_ATTEMPTS);
                lastWaitMsg = currentMillis;
            }
        }
        
        // LED piscando indica problema
        static unsigned long lastLedToggle = 0;
        if (currentMillis - lastLedToggle > 500) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            lastLedToggle = currentMillis;
        }
    }
    else
    {
        // WiFi conectado - resetar contadores se estava desconectado
        if (wifiDisconnectedSince > 0) {
            unsigned long reconnectTime = currentMillis - wifiDisconnectedSince;
            Serial.printf("\n✅ WiFi RECONECTADO após %lu segundos!\n", reconnectTime / 1000);
            Serial.printf("📊 Tentativas necessárias: %d\n", wifiReconnectAttempts);
            resetWiFiReconnectCounters();
        }
        wifiWasEverConnected = true;
        digitalWrite(LED_PIN, LOW); // LED apagado = OK
    }

    // Verificar botão de configuração (pressionar por 5 segundos)
    static unsigned long buttonPressStart = 0;
    if (digitalRead(CONFIG_BUTTON_PIN) == LOW)
    {
        if (buttonPressStart == 0)
        {
            buttonPressStart = millis();
        }
        else if (millis() - buttonPressStart > 5000)
        {
            Serial.println("🔧 Botão configuração pressionado! Entrando em modo setup...");
            wm.resetSettings();
            ESP.restart();
        }
    }
    else
    {
        buttonPressStart = 0;
    }

    // Ler e enviar dados do sensor
    if (currentMillis - lastReading > (readingIntervalSec * 1000))
    {
        readAndSendSensorData();
        lastReading = currentMillis;
    }

    // Enviar heartbeat
    if (currentMillis - lastHeartbeat > heartbeatInterval)
    {
        sendHeartbeat();
        lastHeartbeat = currentMillis;
    }

    static unsigned long lastBlink = 0;
    if (currentMillis - lastBlink > 5000)
    {
        float temp = readPT100Temperature();
        if (isfinite(temp)) {
            Serial.printf("STATUS: WiFi=%s | Temp=%.1f°C | Bat=%.2fV | Uptime=%s\n",
                         (WiFi.status() == WL_CONNECTED ? "OK" : "ERRO"),
                         temp,
                         readBatteryVoltage(),
                         formatUptime(millis()).c_str());
        } else {
            Serial.printf("STATUS: WiFi=%s | Temp=ERRO | Bat=%.2fV | Uptime=%s\n",
                         (WiFi.status() == WL_CONNECTED ? "OK" : "ERRO"),
                         readBatteryVoltage(),
                         formatUptime(millis()).c_str());
        }
        blinkLED(1, 100);
        lastBlink = currentMillis;
    }

    delay(100);
}

bool reportOffsetToServer()
{
    Serial.println("📤 Reportando offset ao servidor...");
    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId)) {
        http.end();
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Prefer", "return=minimal");
    
    String payload = "{\"temp_offset\":" + String(tempOffset, 1) + "}";
    int httpCode = http.PATCH(payload);
    
    if (httpCode == 200 || httpCode == 204) {
        Serial.printf("✅ Offset %.1f°C reportado com sucesso\n", tempOffset);
        http.end();
        return true;
    } else {
        Serial.printf("❌ Erro ao reportar offset: HTTP %d\n", httpCode);
        http.end();
        return false;
    }
}

bool syncOffsetWithServer()
{
    Serial.println("🔄 Sincronizando offset com servidor...");
    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?select=temp_offset&id=eq." + deviceId)) {
        http.end();
        return false;
    }
    
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String response = http.getString();
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error && doc.size() > 0) {
            JsonVariant offsetField = doc[0]["temp_offset"];
            if (offsetField.isNull()) {
                Serial.println("⚠️ Servidor não possui temp_offset (null)");
                http.end();
                return false;
            }

            float serverOffset = offsetField.as<float>();
            if (!isfinite(serverOffset)) {
                Serial.println("⚠️ Servidor retornou temp_offset inválido (NaN/Inf)");
                http.end();
                return false;
            }

            if (abs(serverOffset) > 10.0f) {
                Serial.printf("⚠️ temp_offset no BD fora da faixa (%.2f). Forçando 0.0 e reportando ao servidor.\n", serverOffset);
                tempOffset = 0.0f;
                preferences.putFloat("temp_offset", tempOffset);
                http.end();
                reportOffsetToServer();
                return true;
            }
            if (abs(serverOffset - tempOffset) > 0.01f) {
                Serial.printf("🔄 Atualizando offset: %.2f°C → %.2f°C (servidor)\n", tempOffset, serverOffset);
                tempOffset = serverOffset;
                preferences.putFloat("temp_offset", tempOffset);
            } else {
                Serial.printf("✅ Offset sincronizado: %.2f°C\n", tempOffset);
            }
            http.end();
            return true;
        }
    }
    
    Serial.printf("⚠️ Falha na sincronização - mantendo offset local: %.1f°C\n", tempOffset);
    http.end();
    return false;
}

bool syncDeviceLocationWithServer()
{
    Serial.println("🔄 Sincronizando localização do device com servidor...");

    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?select=location&id=eq." + deviceId)) {
        http.end();
        return false;
    }

    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, response);
        if (!error && doc.size() > 0) {
            JsonVariant locationField = doc[0]["location"];
            if (!locationField.isNull()) {
                String serverLocation = locationField.as<String>();
                serverLocation.trim();
                String localLocation = deviceLocation;
                localLocation.trim();

                bool serverIsDefault =
                    serverLocation.length() == 0 ||
                    serverLocation == "Localização não informada" ||
                    serverLocation == "Localização não definida";

                bool localIsDefault =
                    localLocation.length() == 0 ||
                    localLocation == "Localização não informada" ||
                    localLocation == "Localização não definida";

                if (!serverIsDefault && serverLocation != localLocation) {
                    Serial.println("🔄 Atualizando localização: '" + deviceLocation + "' → '" + serverLocation + "' (servidor)");
                    deviceLocation = serverLocation;
                    preferences.putString("device_location", deviceLocation);
                    deviceDescription = "Sensor de temperatura Thermo Watch - Nitro em " + deviceLocation;
                    http.end();
                    return true;
                }

                if (serverIsDefault && !localIsDefault) {
                    http.end();

                    Serial.println("📤 Servidor sem localização válida - enviando localização local...");
                    HTTPClient httpPatch;
                    WiFiClientSecure clientPatch;
                    if (!beginSupabaseRequest(httpPatch, clientPatch, String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId)) {
                        httpPatch.end();
                        return false;
                    }
                    httpPatch.addHeader("Content-Type", "application/json");
                    httpPatch.addHeader("Prefer", "return=minimal");

                    DynamicJsonDocument patchDoc(256);
                    patchDoc["location"] = deviceLocation;
                    patchDoc["description"] = deviceDescription;
                    String patchJson;
                    serializeJson(patchDoc, patchJson);

                    int patchCode = httpPatch.PATCH(patchJson);
                    httpPatch.end();

                    if (patchCode == 200 || patchCode == 204) {
                        Serial.println("✅ Localização enviada ao servidor");
                        return true;
                    }
                    Serial.printf("⚠️ Falha ao enviar localização (HTTP %d)\n", patchCode);
                    return false;
                }

                Serial.println("✅ Localização já está sincronizada");
                http.end();
                return true;
            }
        }
    }

    http.end();
    Serial.println("⚠️ Não foi possível sincronizar localização (mantendo local)");
    return false;
}

void registerDevice()
{
    Serial.println("📝 Registrando/Atualizando dispositivo no Supabase...");

    // Usa PATCH para sempre atualizar se existir, sem verificar antes
    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId)) {
        Serial.println("❌ Falha ao iniciar conexão HTTPS com Supabase");
        http.end();
        return;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Prefer", "resolution=merge-duplicates,return=representation");
    http.setTimeout(10000);

    // Criar JSON do dispositivo com mais informações
    DynamicJsonDocument doc(1024);
    doc["id"] = deviceId;
    doc["name"] = DEVICE_DB_NAME;
    doc["description"] = deviceDescription;
    doc["location"] = deviceLocation;
    doc["mac_address"] = WiFi.macAddress();
    doc["firmware_version"] = String("ThermoWatch_v") + CURRENT_FIRMWARE_VERSION + "_WiFiManager";
    doc["current_firmware"] = CURRENT_FIRMWARE_VERSION;
    doc["temp_offset"] = tempOffset;
    doc["is_online"] = true;
    doc["last_seen"] = getISOTimestamp();
    doc["signal_strength"] = WiFi.RSSI();
    doc["reading_interval"] = readingIntervalSec;

    // Metadados adicionais
    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["wifi_ssid"] = WiFi.SSID();
    metadata["local_ip"] = WiFi.localIP().toString();
    metadata["reading_interval"] = readingIntervalSec;
    uint64_t chipid = ESP.getEfuseMac();
    metadata["chip_id"] = String((uint32_t)chipid, HEX);
    metadata["flash_size"] = ESP.getFlashChipSize();
    metadata["free_heap"] = ESP.getFreeHeap();

    String jsonString;
    serializeJson(doc, jsonString);

    // Tenta PATCH primeiro (atualizar se existir)
    int httpResponseCode = http.PATCH(jsonString);

    if (httpResponseCode == 200 || httpResponseCode == 201)
    {
        String response = http.getString();
        Serial.println("✅ Dispositivo atualizado com sucesso!");
    }
    else if (httpResponseCode == 406 || httpResponseCode == 404)
    {
        // Dispositivo não existe, fazer POST (insert)
        http.end();
        HTTPClient httpPost;
        WiFiClientSecure clientPost;
        if (!beginSupabaseRequest(httpPost, clientPost, String(supabaseUrl) + "/rest/v1/devices")) {
            Serial.println("❌ Falha ao iniciar conexão HTTPS com Supabase (POST devices)");
            httpPost.end();
            return;
        }
        httpPost.addHeader("Content-Type", "application/json");
        httpPost.addHeader("Prefer", "return=representation");
        httpPost.setTimeout(10000);

        DynamicJsonDocument insertDoc(1024);
        deserializeJson(insertDoc, jsonString);
        insertDoc["name"] = DEVICE_DB_NAME;
        String insertJson;
        serializeJson(insertDoc, insertJson);

        httpResponseCode = httpPost.POST(insertJson);
        
        if (httpResponseCode > 0)
        {
            String response = httpPost.getString();
            Serial.println("✅ Dispositivo registrado com sucesso!");
        }
        else
        {
            Serial.println("❌ Erro ao registrar dispositivo!");
            Serial.println("🔢 Código HTTP: " + String(httpResponseCode));
        }
        httpPost.end();
    }
    else
    {
        Serial.println("❌ Erro ao atualizar dispositivo!");
        Serial.println("🔢 Código HTTP: " + String(httpResponseCode));
        if (http.connected())
        {
            Serial.println("📄 Resposta: " + http.getString());
        }
    }

    http.end();
}

/**
 * Atualiza o status de bateria na tabela devices (PATCH)
 * Chamada após enviar dados com sucesso para sincronizar o status
 */
void updateDeviceBatteryStatus(float batteryVoltage, float batteryPercentage, String batteryStatus)
{
    Serial.println("🔋 Atualizando status de bateria no dispositivo...");

    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId)) {
        http.end();
        return;
    }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    DynamicJsonDocument doc(512);
    doc["name"] = DEVICE_DB_NAME;
    doc["is_online"] = true;
    doc["last_seen"] = getISOTimestamp();
    doc["battery_voltage"] = batteryVoltage;
    doc["battery_percentage"] = (int)batteryPercentage;
    doc["battery_status"] = batteryStatus;
    doc["battery_low_alert"] = (batteryVoltage <= BATTERY_LOW_VOLTAGE);
    doc["battery_critical_alert"] = (batteryVoltage <= BATTERY_CRITICAL_VOLTAGE);
    doc["last_battery_check"] = getISOTimestamp();
    doc["power_mode"] = "BATTERY";
    doc["signal_strength"] = WiFi.RSSI();

    String loc = deviceLocation;
    loc.trim();
    if (loc.length() > 0 && loc != "Localização não informada" && loc != "Localização não definida") {
        doc["location"] = loc;
        doc["description"] = deviceDescription;
    }

    doc["mac_address"] = WiFi.macAddress();
    doc["firmware_version"] = String("ThermoWatch_v") + CURRENT_FIRMWARE_VERSION + "_WiFiManager";
    doc["current_firmware"] = CURRENT_FIRMWARE_VERSION;
    doc["temp_offset"] = tempOffset;

    // Metadados
    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["uptime_ms"] = millis();
    metadata["free_heap"] = ESP.getFreeHeap();
    metadata["wifi_rssi"] = WiFi.RSSI();
    metadata["reading_interval"] = readingIntervalSec;
    metadata["battery_cycles"] = preferences.getULong("sleep_count", 0);

    String jsonString;
    serializeJson(doc, jsonString);

    int httpResponseCode = http.PATCH(jsonString);

    if (httpResponseCode == 200 || httpResponseCode == 204)
    {
        Serial.printf("✅ Status bateria atualizado: %.2fV (%s)\n", batteryVoltage, batteryStatus.c_str());
    }
    else
    {
        Serial.printf("⚠️ Erro ao atualizar status: HTTP %d\n", httpResponseCode);
    }

    http.end();
}

/**
 * Verifica se o dispositivo está habilitado no servidor
 * Coluna: is_enabled (boolean) na tabela devices
 * Retorna: true = habilitado, false = bloqueado
 */
bool checkDeviceEnabled()
{
    Serial.println("🔐 Verificando habilitação do dispositivo...");

    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId + "&select=is_enabled")) {
        http.end();
        return true;
    }
    http.setTimeout(10000);

    int httpCode = http.GET();
    
    if (httpCode == 200)
    {
        String response = http.getString();
        http.end();
        
        // Parse JSON
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error && doc.size() > 0)
        {
            // Verificar campo is_enabled (se não existir, assume habilitado)
            JsonVariant isEnabled = doc[0]["is_enabled"];
            
            if (isEnabled.isNull())
            {
                // Campo não existe ainda - assume habilitado
                Serial.println("✅ Dispositivo HABILITADO (campo não existe - padrão)");
                return true;
            }
            
            bool enabled = isEnabled.as<bool>();
            
            if (enabled)
            {
                Serial.println("✅ Dispositivo HABILITADO");
                return true;
            }
            else
            {
                Serial.println("🚫 ═══════════════════════════════════════════════════=");
                Serial.println("🚫  DISPOSITIVO BLOQUEADO PELO ADMINISTRADOR!");
                Serial.printf("🚫  Entrando em deep sleep por %d minutos...\n", readingIntervalSec / 60);
                Serial.println("🚫 ═══════════════════════════════════════════════════=");
                
                // 3 piscadas demoradas = sinal de bloqueio
                for (int i = 0; i < 3; i++) {
                    digitalWrite(LED_PIN, HIGH);
                    delay(1000);  // 1 segundo aceso
                    digitalWrite(LED_PIN, LOW);
                    delay(500);   // 0.5 segundo apagado
                }
                
                return false;
            }
        }
        else
        {
            // Dispositivo não encontrado - assume habilitado (será criado depois)
            Serial.println("⚠️ Dispositivo não encontrado - assumindo habilitado");
            return true;
        }
    }
    else
    {
        // Erro de conexão - continua normalmente para não bloquear por falha de rede
        Serial.printf("⚠️ Erro ao verificar habilitação (HTTP %d) - continuando\n", httpCode);
        http.end();
        return true;
    }
}

/**
 * Sincroniza o intervalo de leitura com o servidor
 * - Na 1ª transmissão: ESP32 envia seu intervalo local para o servidor
 * - Nas próximas: Consulta o servidor e se diferente, atualiza localmente
 * 
 * Isso permite que o administrador altere o intervalo pelo banco de dados
 * e o ESP32 pegará a nova configuração na próxima leitura
 */
void syncReadingInterval()
{
    Serial.println("🔄 Sincronizando intervalo de leitura...");
    
    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId + "&select=reading_interval")) {
        http.end();
        return;
    }
    http.setTimeout(10000);
    
    int httpCode = http.GET();
    String response = http.getString();
    http.end();
    
    if (httpCode == 200)
    {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error && doc.size() > 0)
        {
            // Verificar se o campo reading_interval existe no servidor
            if (doc[0].containsKey("reading_interval") && !doc[0]["reading_interval"].isNull())
            {
                int serverInterval = doc[0]["reading_interval"].as<int>();
                
                // Validar intervalo (mínimo 60s, máximo 24h)
                if (serverInterval >= 60 && serverInterval <= 86400)
                {
                    if (serverInterval != readingIntervalSec)
                    {
                        Serial.printf("📥 Intervalo alterado pelo servidor: %ds → %ds\n", 
                                      readingIntervalSec, serverInterval);
                        Serial.printf("   (%d min → %d min)\n", 
                                      readingIntervalSec / 60, serverInterval / 60);
                        
                        // Atualizar localmente para próxima vez
                        readingIntervalSec = serverInterval;
                        preferences.putInt("read_interval", readingIntervalSec);
                        
                        Serial.println("✅ Intervalo atualizado localmente");
                    }
                    else
                    {
                        Serial.printf("✅ Intervalo sincronizado: %ds (%d min)\n", 
                                      readingIntervalSec, readingIntervalSec / 60);
                    }
                }
                else
                {
                    Serial.printf("⚠️ Intervalo do servidor inválido (%ds) - mantendo local\n", serverInterval);
                }
            }
            else
            {
                // Campo não existe no servidor - enviar o valor local
                Serial.println("📤 Intervalo não configurado no servidor - enviando valor local...");
                
                HTTPClient httpPatch;
                WiFiClientSecure clientPatch;
                if (!beginSupabaseRequest(httpPatch, clientPatch, String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId)) {
                    httpPatch.end();
                    return;
                }
                httpPatch.addHeader("Content-Type", "application/json");
                httpPatch.setTimeout(10000);
                
                DynamicJsonDocument patchDoc(256);
                patchDoc["reading_interval"] = readingIntervalSec;
                
                String patchJson;
                serializeJson(patchDoc, patchJson);
                
                int patchCode = httpPatch.PATCH(patchJson);
                
                if (patchCode >= 200 && patchCode < 300)
                {
                    Serial.printf("✅ Intervalo enviado ao servidor: %ds (%d min)\n", 
                                  readingIntervalSec, readingIntervalSec / 60);
                }
                else
                {
                    Serial.printf("⚠️ Erro ao enviar intervalo (HTTP %d)\n", patchCode);
                }
                
                httpPatch.end();
            }
        }
        else
        {
            Serial.println("⚠️ Dispositivo não encontrado - intervalo será enviado no cadastro");
        }
    }
    else
    {
        Serial.printf("⚠️ Erro ao sincronizar intervalo (HTTP %d)\n", httpCode);
    }
}

/**
 * Verifica se o dispositivo já está cadastrado no banco de dados
 * Se não estiver, faz o cadastro automaticamente
 */
bool ensureDeviceRegistered()
{
    Serial.println("🔍 Verificando se o dispositivo está cadastrado...");

    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/devices?select=id,name,mac_address,firmware_version,location&id=eq." + deviceId + "&limit=1")) {
        http.end();
        return false;
    }

    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        http.end();

        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, response);
        if (!error && doc.is<JsonArray>() && doc.size() > 0) {
            String serverName = doc[0]["name"] | "";
            String serverMac = doc[0]["mac_address"] | "";
            String serverFw = doc[0]["firmware_version"] | "";
            String serverLoc = doc[0]["location"] | "";
            serverName.trim();
            serverMac.trim();
            serverFw.trim();
            serverLoc.trim();

            String localLoc = deviceLocation;
            localLoc.trim();

            bool serverLocDefault =
                serverLoc.length() == 0 ||
                serverLoc == "Localização não informada" ||
                serverLoc == "Localização não definida";

            bool localLocDefault =
                localLoc.length() == 0 ||
                localLoc == "Localização não informada" ||
                localLoc == "Localização não definida";

            bool needsUpdate =
                serverName != DEVICE_DB_NAME ||
                serverMac.length() == 0 ||
                serverFw.length() == 0 ||
                (serverLocDefault && !localLocDefault);

            if (needsUpdate) {
                Serial.println("📝 Device cadastrado, mas incompleto - atualizando...");
                registerDevice();
            } else {
                Serial.println("✅ Device já cadastrado");
            }
            return true;
        }

        Serial.println("📝 Device não encontrado - registrando...");
        registerDevice();
        return true;
    }

    Serial.printf("⚠️ Erro ao verificar cadastro (HTTP %d)\n", httpCode);
    if (http.connected()) {
        Serial.println("📄 Resposta: " + http.getString());
    }
    http.end();
    return false;
}

void readAndSendSensorData()
{
    Serial.println("🌡️ === INICIANDO LEITURA DO SENSOR PT100 ===");

    // ===== DEBUG: TESTE DE CONECTIVIDADE PT100 =====
    Serial.println("🔍 Verificando conexão PT100...");
    Serial.println("   - Interface: MAX31865 (SPI)");
    Serial.printf("   - MOSI=%d MISO=%d SCK=%d CS=%d\n", MAX31865_MOSI_PIN, MAX31865_MISO_PIN, MAX31865_SCK_PIN, MAX31865_CS_PIN);
    Serial.printf("   - RREF: %.0fΩ | RNOMINAL: %.0fΩ | WIRES: %d\n", MAX31865_RREF, MAX31865_RNOMINAL, MAX31865_WIRES);
    Serial.println("📊 Fazendo leitura do sensor...");
    
    uint8_t fault = max31865Read8(0x07);
    if (fault) {
        Serial.printf("⚠️ MAX31865 fault=0x%02X (limpando)\n", fault);
        max31865ClearFault();
    }

    uint16_t rtd = max31865ReadRTD();
    float resistance = max31865RTDToResistance(rtd);
    float temperature = max31865ResistanceToTemp(resistance);
    float humidity = 0.0; // PT100 não mede umidade

    if (!isfinite(temperature) || rtd == 0 || rtd >= 32760 || max31865Read8(0x07)) {
        Serial.println("❌ PT100 desconectado/erro - leitura descartada (não enviando)");
        blinkLED(5, 200);
        return;
    }
    
    // ===== ALERTAS DE TEMPERATURA =====
    // Não ativa alertas em modo configuração - apenas em monitoramento contínuo
    // (evita comportamento contínuo durante operações de envio)

    // ===== DEBUG DETALHADO DO SENSOR =====
    Serial.println("📋 Resultados da leitura:");
    Serial.printf("   - Resistência: %.2fΩ\n", resistance);
    Serial.printf("   - Temperatura calculada: %.2f°C\n", temperature);
    Serial.println("   - Umidade: N/A (PT100 não mede umidade)");
    
    // ===== SENSOR PT100 - ENVIO SEM VALIDAÇÃO =====
    // Enviando QUALQUER leitura do sensor sem interpretar erros
    Serial.println("✅ === PT100 - LEITURA LIVRE (SEM VALIDAÇÃO) ===");
    Serial.printf("🌡️ Temperatura: %.2f°C (Resistência: %.2fΩ)\n", temperature, resistance);
    Serial.println("💧 Umidade: N/A (sensor apenas de temperatura)");
    Serial.println("📍 Local: " + deviceLocation);
    if (temperature < -200) {
        Serial.println("❄️ MODO CRIOGÊNICO: Medição em temperaturas extremas");
    }
    Serial.println("📤 Enviando leitura para servidor sem interpretação de erro");
    
    // LED PT100 OK: 2 piscadas RÁPIDAS
    blinkLED(2, 200);

    // ===== DEBUG: VERIFICAR CONECTIVIDADE WiFi =====
    Serial.println("📡 === VERIFICANDO CONECTIVIDADE ===");
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi DESCONECTADO!");
        Serial.println("   - Status: " + String(WiFi.status()));
        
        // LED ERRO WIFI: 3 piscadas MÉDIAS
        Serial.println("🚨 LED: 3 piscadas MÉDIAS = ERRO WIFI");
        for(int i = 0; i < 3; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(600);  // 600ms ligado
            digitalWrite(LED_PIN, LOW);
            delay(600);  // 600ms desligado
        }
        return;
    }
    
    Serial.println("✅ WiFi CONECTADO!");
    Serial.println("   - SSID: " + WiFi.SSID());
    Serial.println("   - IP: " + WiFi.localIP().toString());
    Serial.println("   - RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    // Sincronizar horário via NTP
    syncNTP();

    // ============================================================
    // 🔄 SINCRONIZAÇÃO E CONTROLE (NITRO)
    // ============================================================
    
    // 1. Verificar se dispositivo está habilitado
    if (!checkDeviceEnabled()) {
        Serial.println("⛔ Dispositivo DESABILITADO no servidor.");
        // Se estiver desabilitado, não envia dados e retorna
        // O loop principal ou setup cuidará do deep sleep
        return; 
    }

    // 2. Sincronizar intervalo de leitura
    syncReadingInterval();

    // 3. Sincronizar offset de temperatura
    syncOffsetWithServer();

    // 3.1 Sincronizar localização do device
    syncDeviceLocationWithServer();

    // 4. Verificar atualização de firmware (OTA)
    if (shouldCheckForUpdate()) {
         FirmwareInfo fwInfo = checkFirmwareUpdate();
         if (fwInfo.available) {
             float batPct = getBatteryPercentage(readBatteryVoltage());
             if (canPerformUpdate(fwInfo, batPct, WiFi.RSSI())) {
                 performOTAUpdate(fwInfo);
             }
         }
    }
    // ============================================================

    ensureDeviceRegistered();
    float batteryVoltageNow = readBatteryVoltage();
    float batteryPercentageNow = getBatteryPercentage(batteryVoltageNow);
    String batteryStatusNow = getBatteryStatus(batteryVoltageNow, batteryPercentageNow);

    if (sendSensorData(temperature, humidity)) {
        updateDeviceBatteryStatus(batteryVoltageNow, batteryPercentageNow, batteryStatusNow);
    }

    return;
    
    // ===== DEBUG: TESTE DE CONECTIVIDADE INTERNET =====
    Serial.println("🌐 Testando conectividade com internet...");
    HTTPClient testHttp;
    testHttp.begin("http://www.google.com");
    testHttp.setTimeout(5000);
    int testCode = testHttp.GET();
    testHttp.end();
    
    if (testCode <= 0) {
        Serial.println("❌ SEM ACESSO À INTERNET!");
        Serial.println("   - Código de erro: " + String(testCode));
        
        // LED ERRO INTERNET: 4 piscadas MÉDIAS
        Serial.println("🚨 LED: 4 piscadas MÉDIAS = SEM INTERNET");
        for(int i = 0; i < 4; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(500);  // 500ms ligado
            digitalWrite(LED_PIN, LOW);
            delay(500);  // 500ms desligado
        }
        return;
    }
    
    Serial.println("✅ INTERNET OK! (Código: " + String(testCode) + ")");

    // ===== DEBUG: ENVIO PARA SUPABASE =====
    Serial.println("📤 === ENVIANDO DADOS PARA SUPABASE ===");
    Serial.println("🔗 URL: " + String(supabaseUrl) + "/rest/v1/sensor_readings");
    
    HTTPClient http;
    http.begin(String(supabaseUrl) + "/rest/v1/sensor_readings");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    http.setTimeout(10000); // 10 segundos timeout

    // Criar JSON da leitura com informações completas
    DynamicJsonDocument doc(1024);
    doc["device_id"] = deviceId;
    doc["device_name"] = deviceName;
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["timestamp"] = getISOTimestamp();
    doc["sensor_type"] = "PT100";

    // Dados técnicos adicionais incluindo bateria
    JsonObject rawData = doc.createNestedObject("raw_data");
    rawData["wifi_rssi"] = WiFi.RSSI();
    rawData["free_heap"] = ESP.getFreeHeap();
    rawData["uptime_ms"] = millis();
    rawData["reading_interval"] = readingIntervalSec;
    
    // Dados da bateria
    float batteryVoltage = readBatteryVoltage();
    float batteryPercentage = getBatteryPercentage(batteryVoltage);
    String batteryStatus = getBatteryStatus(batteryVoltage, batteryPercentage);
    
    rawData["battery_voltage"] = batteryVoltage;
    rawData["battery_percentage"] = batteryPercentage;
    rawData["battery_status"] = batteryStatus;
    rawData["battery_low_alert"] = (batteryVoltage <= BATTERY_LOW_VOLTAGE);
    rawData["battery_critical_alert"] = (batteryVoltage <= BATTERY_CRITICAL_VOLTAGE);
    rawData["debug_mode"] = true;

    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("� JSON preparado:");
    Serial.println(jsonString);
    Serial.println("�📤 Enviando para Supabase...");

    int httpResponseCode = http.POST(jsonString);
    
    Serial.println("📨 === RESPOSTA DO SUPABASE ===");
    Serial.println("   - Código HTTP: " + String(httpResponseCode));

    if (httpResponseCode > 0)
    {
        String response = http.getString();
        Serial.println("   - Resposta: " + response);
        
        if (httpResponseCode == 200 || httpResponseCode == 201)
        {
            Serial.println("✅ === DADOS ENVIADOS COM SUCESSO! ===");
            Serial.println("📊 Resumo do envio:");
            Serial.println("   - Temperatura: " + String(temperature, 1) + "°C");
            Serial.println("   - Umidade: " + String(humidity, 1) + "%");
            Serial.println("   - Bateria: " + String(batteryVoltage, 2) + "V (" + String(batteryPercentage, 1) + "%)");
            Serial.println("   - WiFi: " + String(WiFi.RSSI()) + " dBm");

            // LED SUCESSO: 1 piscada LONGA
            Serial.println("✅ LED: 1 piscada LONGA = SUCESSO TOTAL");
            digitalWrite(LED_PIN, HIGH);
            delay(1500);  // 1.5 segundos ligado
            digitalWrite(LED_PIN, LOW);
        }
        else
        {
            Serial.println("⚠️ DADOS ENVIADOS COM AVISO:");
            Serial.println("   - Status HTTP: " + String(httpResponseCode));
            
            // LED AVISO: 2 piscadas MÉDIAS
            Serial.println("⚠️ LED: 2 piscadas MÉDIAS = AVISO SUPABASE");
            for(int i = 0; i < 2; i++) {
                digitalWrite(LED_PIN, HIGH);
                delay(600);
                digitalWrite(LED_PIN, LOW);
                delay(600);
            }
        }
    }
    else
    {
        Serial.println("❌ === ERRO AO ENVIAR PARA SUPABASE ===");
        Serial.println("🔢 Código HTTP: " + String(httpResponseCode));
        Serial.println("🌐 WiFi Status: " + String(WiFi.status()));
        Serial.println("📡 RSSI: " + String(WiFi.RSSI()) + " dBm");
        Serial.println("🔧 Possíveis causas:");
        Serial.println("   1. Supabase fora do ar");
        Serial.println("   2. API Key incorreta");
        Serial.println("   3. URL incorreta");
        Serial.println("   4. Firewall bloqueando");

        if (http.connected())
        {
            Serial.println("📄 Erro detalhado: " + http.getString());
        }

        // LED ERRO SUPABASE: 6 piscadas RÁPIDAS
        Serial.println("🚨 LED: 6 piscadas RÁPIDAS = ERRO SUPABASE");
        for(int i = 0; i < 6; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(200);  // 200ms ligado
            digitalWrite(LED_PIN, LOW);
            delay(200);  // 200ms desligado
        }
    }

    http.end();

    // Mostrar próxima leitura
    Serial.println("⏰ Próxima leitura em " + String(readingIntervalSec) + " segundos");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
}

void sendHeartbeat()
{
    Serial.println("💓 Enviando heartbeat...");

    HTTPClient http;
    http.begin(String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    http.setTimeout(10000); // Timeout de 10 segundos

    // Atualizar status do dispositivo com dados de bateria
    DynamicJsonDocument doc(1024);
    doc["is_online"] = true;
    doc["last_seen"] = getISOTimestamp();
    doc["signal_strength"] = WiFi.RSSI();
    doc["current_firmware"] = CURRENT_FIRMWARE_VERSION;
    
    // ===== DADOS DE SUPERVISÃO DE BATERIA =====
    float batteryVoltage = readBatteryVoltage();
    float batteryPercentage = getBatteryPercentage(batteryVoltage);
    String batteryStatus = getBatteryStatus(batteryVoltage, batteryPercentage);
    
    doc["battery_voltage"] = batteryVoltage;
    doc["battery_percentage"] = (int)batteryPercentage;
    doc["battery_status"] = batteryStatus;
    doc["battery_low_alert"] = (batteryVoltage <= BATTERY_LOW_VOLTAGE);
    doc["battery_critical_alert"] = (batteryVoltage <= BATTERY_CRITICAL_VOLTAGE);
    doc["last_battery_check"] = getISOTimestamp();
    doc["power_mode"] = "BATTERY";

    // Metadados do sistema
    JsonObject metadata = doc.createNestedObject("metadata");
    metadata["uptime_ms"] = millis();
    metadata["free_heap"] = ESP.getFreeHeap();
    metadata["wifi_rssi"] = WiFi.RSSI();
    metadata["reading_interval"] = readingIntervalSec;
    metadata["battery_cycles"] = preferences.getULong("sleep_count", 0);

    String jsonString;
    serializeJson(doc, jsonString);

    int httpResponseCode = http.PATCH(jsonString);

    if (httpResponseCode > 0)
    {
        if (httpResponseCode == 200 || httpResponseCode == 204)
        {
            Serial.println("✅ Heartbeat enviado - Sistema online");
        }
        else
        {
            Serial.println("⚠️ Heartbeat com aviso: " + String(httpResponseCode));
        }
    }
    else
    {
        Serial.println("❌ Erro no heartbeat: " + String(httpResponseCode));
        Serial.println("🌐 WiFi: " + String(WiFi.status()));
        Serial.println("📡 RSSI: " + String(WiFi.RSSI()) + " dBm");
    }

    http.end();

    // Mostrar estatísticas do sistema
    Serial.println("📈 Sistema - Uptime: " + formatUptime(millis()) +
                   " | Heap: " + String(ESP.getFreeHeap()) +
                   " | RSSI: " + String(WiFi.RSSI()) + " dBm");
}

// Função auxiliar para formatar uptime
String formatUptime(unsigned long ms)
{
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;

    if (days > 0)
    {
        return String(days) + "d " + String(hours % 24) + "h";
    }
    else if (hours > 0)
    {
        return String(hours) + "h " + String(minutes % 60) + "m";
    }
    else if (minutes > 0)
    {
        return String(minutes) + "m " + String(seconds % 60) + "s";
    }
    else
    {
        return String(seconds) + "s";
    }
}

// Função para piscar LED (indicações visuais)
void blinkLED(int times, int delayMs)
{
    if (!statusLedAllowed) {
        return;
    }
    for (int i = 0; i < times; i++)
    {
        digitalWrite(LED_PIN, HIGH);
        delay(delayMs);
        yield(); // Alimentar watchdog
        digitalWrite(LED_PIN, LOW);
        if (i < times - 1) {
            delay(delayMs);
            yield(); // Alimentar watchdog
        }
    }
}

// ===== FUNÇÕES DE CONTROLE DO LED =====

// Interrupção do timer para piscar LED
void IRAM_ATTR onLedTimer() {
    if (ledBlinkEnabled && statusLedAllowed) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    } else {
        digitalWrite(LED_PIN, LOW);
        ledState = false;
    }
}

// Inicializar timer do LED
void initLedTimer() {
    // Timer 0, prescaler 80 (1MHz), count up
    ledTimer = timerBegin(0, 80, true);
    // Anexar interrupção
    timerAttachInterrupt(ledTimer, &onLedTimer, true);
    // Configurar para 200ms (200000 microsegundos)
    timerAlarmWrite(ledTimer, 200000, true);
}

// Iniciar piscada do LED
void startLedBlink() {
    ledBlinkEnabled = true;
    if (ledTimer != NULL) {
        timerAlarmEnable(ledTimer);
    }
}

// Parar piscada do LED
void stopLedBlink() {
    ledBlinkEnabled = false;
    if (ledTimer != NULL) {
        timerAlarmDisable(ledTimer);
    }
    digitalWrite(LED_PIN, LOW);
}

void setStatusLedAllowed(bool allowed)
{
    statusLedAllowed = allowed;
    if (!allowed) {
        stopLedBlink();
    }
}

// Liga ou desliga o LED
void setLED(bool state)
{
    if (!statusLedAllowed) {
        return;
    }
    stopLedBlink(); // Garantir que não está piscando
    digitalWrite(LED_PIN, state ? HIGH : LOW);
}

// LED MODO CONFIGURAÇÃO: Aceso fixo
void ledConfigMode()
{
    startLedBlink();
}

// LED MODO OPERAÇÃO: Iniciar piscada rápida em background
void ledOperationMode()
{
    ledOff();
}

// LED SUCESSO: Acende 2 segundos e apaga
void ledSuccess()
{
    if (!statusLedAllowed) {
        return;
    }
    stopLedBlink(); // Parar piscada
    digitalWrite(LED_PIN, HIGH);
    delay(2000);
    digitalWrite(LED_PIN, LOW);
}

// LED FALHA: Pisca 2 vezes rápido e apaga
void ledFailure()
{
    if (!statusLedAllowed) {
        return;
    }
    stopLedBlink(); // Parar piscada
    for (int i = 0; i < 2; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(150);
        digitalWrite(LED_PIN, LOW);
        delay(150);
    }
}

// LED OFF: Apaga o LED
void ledOff()
{
    setStatusLedAllowed(false);
}

String getISOTimestamp()
{
    // Usa horário real do NTP (sincronizado automaticamente)
    time_t now;
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        // Se NTP não está disponível, usar tempo baseado em millis()
        unsigned long currentTime = millis();
        unsigned long seconds = currentTime / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;

        // Formato ISO 8601 simulado (começando em 2025-01-01)
        return "2025-01-01T" +
               String((hours % 24), DEC) + ":" +
               String((minutes % 60), DEC) + ":" +
               String((seconds % 60), DEC) + ".000Z";
    }

    // Se NTP está disponível, usar tempo real
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", &timeinfo);
    return String(timestamp);
}

// ============================================================================
// ⏰ SINCRONIZAÇÃO DE HORÁRIO VIA NTP
// ============================================================================

void syncNTP()
{
    Serial.println("⏰ Sincronizando horário com servidor NTP...");
    
    // Configurar NTP para timezone de Brasília (UTC-3)
    // Servidores NTP brasileiros
    configTime(-3 * 3600, 0, "a.st1.ntp.br", "b.st1.ntp.br", "pool.ntp.org");
    
    // Aguardar até 5 segundos para sincronizar
    int attempts = 0;
    struct tm timeinfo;
    
    while (!getLocalTime(&timeinfo) && attempts < 10)
    {
        delay(500);
        attempts++;
    }
    
    if (getLocalTime(&timeinfo))
    {
        char buffer[64];
        strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
        Serial.println("✅ Horário sincronizado: " + String(buffer));
        Serial.println("🕐 RELÓGIO ATUAL: " + String(buffer) + " (Horário de Brasília - UTC-3)");
    }
    else
    {
        Serial.println("⚠️ Falha ao sincronizar NTP - usando horário aproximado");
    }
}

// ============================================================================
// 🔋 FUNÇÕES DE SUPERVISÃO DE BATERIA
// ============================================================================

float readBatteryVoltage()
{
    // Fazer múltiplas leituras para média mais precisa
    float totalVoltage = 0;
    int rawSum = 0;
    
    for (int i = 0; i < BATTERY_SAMPLES; i++) 
    {
        int rawValue = analogRead(BATTERY_PIN);
        rawSum += rawValue;
        float voltage = (rawValue * 3.3) / 4096.0; // ADC de 12 bits, referência 3.3V
        voltage *= VOLTAGE_DIVIDER_RATIO; // Aplicar divisor de tensão (1.0 = sem divisor)
        voltage *= BATTERY_CALIBRATION_FACTOR; // Aplicar calibração fina
        totalVoltage += voltage;
        delay(10); // Pequeno delay entre leituras
    }
    
    float avgRaw = rawSum / (float)BATTERY_SAMPLES;
    float avgADC = (avgRaw * 3.3) / 4096.0;
    float finalVoltage = totalVoltage / BATTERY_SAMPLES;
    
    // Detectar saturação do ADC (bateria acima de 3.3V)
    bool adcSaturated = (avgRaw >= 4090);
    
    // DEBUG DETALHADO: mostrar raw ADC, tensão ADC e tensão calculada
    Serial.printf("🔋 ADC Raw: %.0f | ADC Voltage: %.3fV | Bateria: %.2fV%s\n", 
                  avgRaw, avgADC, finalVoltage,
                  adcSaturated ? " (ADC SATURADO - bateria cheia)" : "");
    Serial.printf("   Calibração: %.4f | Alimentação: %s\n", 
                  BATTERY_CALIBRATION_FACTOR,
                  (avgRaw < 50) ? "USB (sem bateria)" : "Bateria");
    
    return finalVoltage;
}

float getBatteryPercentage(float voltage)
{
    // Calcular porcentagem baseada na curva de descarga Li-Ion
    if (voltage >= BATTERY_MAX_VOLTAGE) return 100.0;
    if (voltage <= BATTERY_MIN_VOLTAGE) return 0.0;
    
    // Interpolação linear (pode ser melhorada com curva real)
    float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
    return constrain(percentage, 0.0, 100.0);
}

String getBatteryStatus(float voltage, float percentage)
{
    if (voltage <= BATTERY_CRITICAL_VOLTAGE) {
        return "CRÍTICA";
    } else if (voltage <= BATTERY_LOW_VOLTAGE) {
        return "BAIXA";
    } else if (percentage >= 80) {
        return "EXCELENTE";
    } else if (percentage >= 50) {
        return "BOA";
    } else if (percentage >= 20) {
        return "MODERADA";
    } else {
        return "BAIXA";
    }
}

// ============================================================================
// 📶 FUNÇÕES DE QUALIDADE DO SINAL WIFI
// ============================================================================

/**
 * Retorna descrição da qualidade do sinal WiFi baseado no RSSI
 */
String getWiFiSignalQuality(int rssi)
{
    if (rssi > WIFI_RSSI_EXCELLENT) return "EXCELENTE";
    if (rssi > WIFI_RSSI_GOOD) return "BOM";
    if (rssi > WIFI_RSSI_FAIR) return "RAZOÁVEL";
    if (rssi > WIFI_RSSI_WEAK) return "FRACO";
    if (rssi > WIFI_RSSI_VERY_WEAK) return "MUITO FRACO";
    return "CRÍTICO";
}

/**
 * Verifica qualidade do sinal WiFi e envia alerta se estiver fraco
 * Retorna true se alerta foi enviado ou não era necessário
 */
bool checkWiFiSignalAlert(int rssi)
{
    String quality = getWiFiSignalQuality(rssi);
    
    // Log sempre a qualidade do sinal
    Serial.printf("📶 Sinal WiFi: %d dBm (%s)\n", rssi, quality.c_str());
    
    // Se sinal está OK (melhor que -80 dBm), não precisa alertar
    if (rssi > WIFI_RSSI_WEAK) {
        return true;
    }
    
    // Determinar severidade do alerta
    bool isCritical = (rssi <= WIFI_RSSI_CRITICAL);
    bool isVeryWeak = (rssi <= WIFI_RSSI_VERY_WEAK);
    
    if (isCritical) {
        Serial.println("🚨 ALERTA CRÍTICO: Sinal WiFi muito fraco!");
        Serial.println("   Isso pode causar falhas de conexão e dados perdidos.");
        Serial.println("   Considere aproximar o dispositivo do roteador.");
    } else if (isVeryWeak) {
        Serial.println("⚠️ ALERTA: Sinal WiFi fraco!");
        Serial.println("   Pode haver instabilidade na conexão.");
    } else {
        Serial.println("ℹ️ AVISO: Sinal WiFi abaixo do ideal.");
    }
    
    // Enviar alerta ao banco de dados
    String alertType = isCritical ? "wifi_critical" : (isVeryWeak ? "wifi_very_weak" : "wifi_weak");
    
    // PASSO 1: Resolver alertas anteriores do mesmo tipo
    HTTPClient httpResolve;
    String resolveUrl = String(supabaseUrl) + "/rest/v1/alerts?device_id=eq." + deviceId + "&alert_type=eq." + alertType + "&is_resolved=eq.false";
    httpResolve.begin(resolveUrl);
    httpResolve.addHeader("Content-Type", "application/json");
    httpResolve.addHeader("apikey", supabaseKey);
    httpResolve.addHeader("Authorization", "Bearer " + String(supabaseKey));
    httpResolve.addHeader("Prefer", "return=minimal");
    httpResolve.setTimeout(10000);
    
    String resolvePayload = "{\"is_resolved\":true}";
    int resolveCode = httpResolve.PATCH(resolvePayload);
    httpResolve.end();
    
    // PASSO 2: Criar novo alerta
    HTTPClient http;
    http.begin(String(supabaseUrl) + "/rest/v1/alerts");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    http.addHeader("Prefer", "return=representation");
    http.setTimeout(10000);
    
    DynamicJsonDocument doc(512);
    doc["device_id"] = deviceId;
    doc["alert_type"] = alertType;
    
    // Mensagem descritiva
    String alertMessage;
    if (isCritical) {
        alertMessage = "🚨 WiFi CRÍTICO: " + String(rssi) + " dBm - Risco de perda de dados!";
    } else if (isVeryWeak) {
        alertMessage = "⚠️ WiFi MUITO FRACO: " + String(rssi) + " dBm - Conexão instável";
    } else {
        alertMessage = "ℹ️ WiFi FRACO: " + String(rssi) + " dBm - Considere reposicionar";
    }
    doc["message"] = alertMessage;
    
    doc["severity"] = isCritical ? "critical" : (isVeryWeak ? "warning" : "info");
    doc["value"] = rssi;
    doc["threshold"] = WIFI_RSSI_WEAK;
    doc["is_resolved"] = false;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("📤 Enviando alerta WiFi: " + jsonString);
    int httpCode = http.POST(jsonString);
    http.end();
    
    if (httpCode == 200 || httpCode == 201) {
        Serial.println("✅ Alerta de WiFi fraco enviado");
        return true;
    } else {
        Serial.printf("❌ Erro ao enviar alerta WiFi: %d\n", httpCode);
        return false;
    }
}

// ============================================================================
// 🔋 FUNÇÕES DE ALERTA DE BATERIA
// ============================================================================

bool checkBatteryAlert(float voltage, float percentage)
{
    // Detectar se está alimentado por USB (sem bateria conectada)
    // ADC raw < 50 (< 0.04V) indica que não há bateria
    int rawTest = analogRead(BATTERY_PIN);
    bool isUSBPowered = (rawTest < 50);
    
    if (isUSBPowered) {
        Serial.println("ℹ️ Alimentação USB detectada - Alertas de bateria DESABILITADOS");
        Serial.printf("   ADC Raw: %d (< 50 = sem bateria)\n", rawTest);
        return true; // Não envia alerta se está em USB
    }
    
    bool isCritical = (voltage <= BATTERY_CRITICAL_VOLTAGE);
    bool isLow = (voltage <= BATTERY_LOW_VOLTAGE);
    
    if (isCritical) 
    {
        Serial.println("🚨 ALERTA CRÍTICO: Bateria em nível crítico!");
        Serial.println("   Tensão: " + String(voltage, 2) + "V (" + String(percentage, 1) + "%)");
        Serial.println("   RECARREGUE IMEDIATAMENTE para evitar danos!");
        
        // LED de alerta crítico (10 piscadas rápidas)
        for (int i = 0; i < 10; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, LOW);
            delay(100);
        }
    }
    else if (isLow) 
    {
        Serial.println("⚠️ ALERTA: Bateria baixa!");
        Serial.println("   Tensão: " + String(voltage, 2) + "V (" + String(percentage, 1) + "%)");
        Serial.println("   Considere recarregar em breve");
        
        // LED de alerta baixo (5 piscadas)
        for (int i = 0; i < 5; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(200);
            digitalWrite(LED_PIN, LOW);
            delay(200);
        }
    }
    
    // Se é crítico ou baixo, enviar alerta ao banco de dados
    if (isCritical || isLow) {
        Serial.println("💾 Enviando alerta de bateria ao banco de dados...");
        
        String alertType = isCritical ? "battery_critical" : "battery_low";
        
        // PASSO 1: Primeiro, resolver (fechar) alertas anteriores do mesmo tipo
        Serial.println("🔍 Verificando alertas anteriores não resolvidos...");
        HTTPClient httpResolve;
        String resolveUrl = String(supabaseUrl) + "/rest/v1/alerts?device_id=eq." + deviceId + "&alert_type=eq." + alertType + "&is_resolved=eq.false";
        httpResolve.begin(resolveUrl);
        httpResolve.addHeader("Content-Type", "application/json");
        httpResolve.addHeader("apikey", supabaseKey);
        httpResolve.addHeader("Authorization", "Bearer " + String(supabaseKey));
        httpResolve.addHeader("Prefer", "return=minimal");
        httpResolve.setTimeout(10000);
        
        String resolvePayload = "{\"is_resolved\":true}";
        int resolveCode = httpResolve.PATCH(resolvePayload);
        httpResolve.end();
        
        if (resolveCode == 200 || resolveCode == 204) {
            Serial.println("✅ Alertas anteriores resolvidos");
        } else {
            Serial.printf("ℹ️ Nenhum alerta anterior encontrado ou já resolvido (código %d)\n", resolveCode);
        }
        
        // PASSO 2: Agora criar novo alerta
        HTTPClient http;
        http.begin(String(supabaseUrl) + "/rest/v1/alerts");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("apikey", supabaseKey);
        http.addHeader("Authorization", "Bearer " + String(supabaseKey));
        http.addHeader("Prefer", "return=representation");
        http.setTimeout(10000);
        
        // Criar JSON do alerta conforme estrutura da tabela alerts
        DynamicJsonDocument doc(512);
        doc["device_id"] = deviceId;
        doc["alert_type"] = alertType;
        
        // Mensagem incluindo tensão e porcentagem da bateria
        String alertMessage = isCritical 
            ? "🚨 Bateria CRÍTICA: " + String(voltage, 2) + "V (" + String(percentage, 0) + "%) - Recarregue imediatamente!"
            : "⚠️ Bateria BAIXA: " + String(voltage, 2) + "V (" + String(percentage, 0) + "%) - Considere recarregar";
        doc["message"] = alertMessage;
        
        doc["severity"] = isCritical ? "critical" : "warning";
        doc["value"] = voltage;
        doc["threshold"] = isCritical ? BATTERY_CRITICAL_VOLTAGE : BATTERY_LOW_VOLTAGE;
        doc["is_resolved"] = false;
        
        String jsonString;
        serializeJson(doc, jsonString);
        
        Serial.println("📤 JSON enviado: " + jsonString);
        int httpCode = http.POST(jsonString);
        
        if (httpCode > 0) {
            String response = http.getString();
            Serial.printf("📥 Resposta HTTP %d: %s\n", httpCode, response.c_str());
        }
        http.end();
        
        if (httpCode == 200 || httpCode == 201) {
            Serial.println("✅ Alerta de bateria enviado com sucesso!");
            return true;
        } else {
            Serial.printf("❌ Erro ao enviar alerta de bateria: %d\n", httpCode);
            return false;
        }
    }
    
    // Bateria normal, sem alerta
    return true;
}

void displayBatteryInfo()
{
    float voltage = readBatteryVoltage();
    float percentage = getBatteryPercentage(voltage);
    String status = getBatteryStatus(voltage, percentage);
    
    // Verificar se está em USB
    int rawTest = analogRead(BATTERY_PIN);
    bool isUSBPowered = (rawTest < 50);
    
    Serial.println("🔋 Status da Bateria:");
    if (isUSBPowered) {
        Serial.println("   ⚡ Alimentação: USB (sem bateria detectada)");
        Serial.printf("   📊 ADC Raw: %d (muito baixo para ter bateria)\n", rawTest);
        Serial.println("   ℹ️  Alertas de bateria DESABILITADOS em modo USB");
    } else {
        Serial.println("   ⚡ Alimentação: Bateria");
        Serial.println("   Tensão: " + String(voltage, 2) + "V");
        Serial.println("   Carga: " + String(percentage, 1) + "%");
        Serial.println("   Status: " + status);
    }
    
    // Não verificar alertas aqui pois WiFi não está conectado ainda
    // checkBatteryAlert será chamado em performQuickReading() após conectar WiFi
}

bool sendSensorData(float temperature, float humidity)
{
    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/sensor_readings")) {
        http.end();
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000); // 15 segundos - evita timeout com WiFi fraco (RSSI < -90dBm)

    // Criar JSON da leitura com dados da bateria
    DynamicJsonDocument doc(1024);
    doc["device_id"] = deviceId;
    doc["device_name"] = deviceName;
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["timestamp"] = getISOTimestamp();
    doc["sensor_type"] = "PT100";

    // Dados da bateria como colunas principais
    float batteryVoltage = readBatteryVoltage();
    float batteryPercentage = getBatteryPercentage(batteryVoltage);
    String batteryStatus = getBatteryStatus(batteryVoltage, batteryPercentage);
    
    doc["battery_voltage"] = batteryVoltage;
    doc["battery_percentage"] = batteryPercentage;
    doc["battery_status"] = batteryStatus;

    // Dados técnicos incluindo bateria e qualidade WiFi
    JsonObject rawData = doc.createNestedObject("raw_data");
    int rssi = WiFi.RSSI();
    rawData["wifi_rssi"] = rssi;
    rawData["wifi_quality"] = getWiFiSignalQuality(rssi);  // EXCELENTE, BOM, FRACO, etc.
    rawData["free_heap"] = ESP.getFreeHeap();
    rawData["uptime_ms"] = millis();
    rawData["reading_interval"] = readingIntervalSec; // Intervalo configurável em modo bateria
    
    // ID único de transação para evitar duplicatas em caso de retry
    // Formato: deviceId_sleepCount_uptimeMs (único por ciclo de acordar)
    unsigned long sleepCount = preferences.getULong("sleep_count", 0);
    String transactionId = deviceId + "_" + String(sleepCount) + "_" + String(millis());
    rawData["transaction_id"] = transactionId;
    
    rawData["battery_low_alert"] = (batteryVoltage <= BATTERY_LOW_VOLTAGE);
    rawData["battery_critical_alert"] = (batteryVoltage <= BATTERY_CRITICAL_VOLTAGE);
    rawData["wifi_weak_alert"] = (rssi <= WIFI_RSSI_WEAK);  // Alerta se sinal fraco
    rawData["power_mode"] = "BATTERY"; // Indicar que está em modo bateria

    String jsonString;
    serializeJson(doc, jsonString);

    int httpResponseCode = http.POST(jsonString);
    bool success = (httpResponseCode == 201);

    if (success) {
        Serial.printf("✅ Dados enviados: %.1f°C, %.1f%%, %.2fV (%.1f%%)\n", 
                      temperature, humidity, batteryVoltage, batteryPercentage);
        Serial.printf("   Transaction ID: %s\n", transactionId.c_str());
    } else {
        // Log detalhado para diagnóstico de problemas
        Serial.printf("❌ Erro HTTP: %d ", httpResponseCode);
        if (httpResponseCode == -1) {
            Serial.println("(Timeout - conexão lenta ou WiFi instável)");
        } else if (httpResponseCode == -11) {
            Serial.println("(Timeout de leitura)");
        } else if (httpResponseCode < 0) {
            Serial.printf("(Erro de conexão: %d)\n", httpResponseCode);
        } else {
            Serial.println("(Erro do servidor)");
        }
        Serial.printf("   RSSI: %d dBm | Heap: %d bytes\n", WiFi.RSSI(), ESP.getFreeHeap());
    }

    http.end();
    return success;
}

// ============================================================================
// � FUNÇÃO DE NOTIFICAÇÃO FINAL DE BATERIA
// ============================================================================

/**
 * Envia notificação final quando a bateria atinge 2.5V
 * Ao invés de enviar apenas a tensão, envia uma mensagem especial
 * indicando que é o último envio antes da recarga
 */
bool sendFinalBatteryNotification(float temperature, float humidity, float batteryVoltage)
{
    Serial.println("\n🚨 ════════════════════════════════════════════════════");
    Serial.println("🚨  ENVIANDO NOTIFICAÇÃO FINAL DE BATERIA");
    Serial.println("🚨 ════════════════════════════════════════════════════\n");
    
    // 1. Enviar leitura final do sensor com flag especial
    HTTPClient http;
    WiFiClientSecure client;
    if (!beginSupabaseRequest(http, client, String(supabaseUrl) + "/rest/v1/sensor_readings")) {
        http.end();
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000); // Timeout maior para garantir envio

    DynamicJsonDocument doc(1024);
    doc["device_id"] = deviceId;
    doc["device_name"] = deviceName;
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["timestamp"] = getISOTimestamp();
    doc["sensor_type"] = "PT100";

    // Dados especiais indicando último envio
    JsonObject rawData = doc.createNestedObject("raw_data");
    rawData["wifi_rssi"] = WiFi.RSSI();
    rawData["free_heap"] = ESP.getFreeHeap();
    rawData["uptime_ms"] = millis();
    rawData["battery_voltage"] = batteryVoltage;
    rawData["battery_percentage"] = getBatteryPercentage(batteryVoltage);
    rawData["battery_status"] = "🚨 ÚLTIMO ENVIO - RECARREGUE A BATERIA!";
    rawData["is_final_warning"] = true;
    rawData["final_message"] = "Dispositivo entrando em hibernação. Recarregue a bateria e pressione BOOT para reativar.";
    rawData["power_mode"] = "HIBERNATION_PENDING";

    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("📤 JSON da leitura final:");
    Serial.println(jsonString);

    int httpCode1 = http.POST(jsonString);
    http.end();
    
    bool sensorOk = (httpCode1 == 201);
    Serial.printf("📥 Leitura final: %s (HTTP %d)\n", sensorOk ? "OK" : "FALHOU", httpCode1);
    
    yield();
    delay(1000);
    
    // 2. Enviar alerta especial para tabela alerts
    HTTPClient httpAlert;
    httpAlert.begin(String(supabaseUrl) + "/rest/v1/alerts");
    httpAlert.addHeader("Content-Type", "application/json");
    httpAlert.addHeader("apikey", supabaseKey);
    httpAlert.addHeader("Authorization", "Bearer " + String(supabaseKey));
    httpAlert.addHeader("Prefer", "return=representation");
    httpAlert.setTimeout(15000);
    
    DynamicJsonDocument alertDoc(512);
    alertDoc["device_id"] = deviceId;
    alertDoc["alert_type"] = "battery_final_warning";
    alertDoc["message"] = "🚨 ÚLTIMO AVISO: Bateria em " + String(batteryVoltage, 2) + "V - Dispositivo entrando em HIBERNAÇÃO! Recarregue imediatamente.";
    alertDoc["severity"] = "critical";
    alertDoc["value"] = batteryVoltage;
    alertDoc["threshold"] = BATTERY_FINAL_WARNING_VOLTAGE;
    alertDoc["is_resolved"] = false;
    
    String alertJson;
    serializeJson(alertDoc, alertJson);
    
    Serial.println("📤 JSON do alerta final:");
    Serial.println(alertJson);
    
    int httpCode2 = httpAlert.POST(alertJson);
    httpAlert.end();
    
    bool alertOk = (httpCode2 == 200 || httpCode2 == 201);
    Serial.printf("📥 Alerta final: %s (HTTP %d)\n", alertOk ? "OK" : "FALHOU", httpCode2);
    
    // LED indica sucesso ou falha
    if (sensorOk && alertOk) {
        Serial.println("\n✅ NOTIFICAÇÃO FINAL ENVIADA COM SUCESSO!");
        // LED aceso por 3 segundos
        digitalWrite(LED_PIN, HIGH);
        delay(3000);
        digitalWrite(LED_PIN, LOW);
        return true;
    } else {
        Serial.println("\n⚠️ Notificação final enviada parcialmente");
        blinkLED(5, 200);
        return sensorOk; // Retorna true se pelo menos a leitura foi enviada
    }
}

// ============================================================================
// 📍 GEOLOCALIZAÇÃO POR WIFI
// ============================================================================

/**
 * Obtém geolocalização usando redes WiFi próximas via Google Geolocation API
 * 
 * Como funciona:
 * 1. Escaneia redes WiFi próximas (até 3)
 * 2. Envia MACs e RSSIs para a API do Google
 * 3. Google retorna lat/long estimada
 * 
 * Precisão: 20-200 metros (depende da densidade de APs)
 * Consumo: ~100ms de scan + 1 requisição HTTP
 * 
 * Retorna: true se conseguiu coordenadas válidas
 */
bool getWiFiGeolocation()
{
    // Verificar se geolocalização está habilitada e API key configurada
    if (!useGeolocation) {
        Serial.println("📍 Geolocalização desabilitada (useGeolocation=false)");
        return false;
    }
    
    if (String(googleGeoApiKey) == "YOUR_API_KEY_HERE") {
        Serial.println("⚠️ Google Geolocation API key não configurada!");
        return false;
    }
    
    Serial.println("📍 Iniciando geolocalização por WiFi...");
    
    // Verificar heap disponível antes de operação crítica SSL
    size_t heapBefore = ESP.getFreeHeap();
    Serial.printf("💾 Heap livre antes da geolocalização: %d bytes\n", heapBefore);
    
    if (heapBefore < 80000) {
        Serial.println("⚠️ Heap insuficiente para geolocalização segura - abortando");
        return false;
    }
    
    // Escanear redes WiFi próximas (REDUZIDO: 200ms por canal)
    Serial.println("📡 Escaneando redes WiFi...");
    int networksFound = WiFi.scanNetworks(false, false, false, 200); // Reduzido para economizar
    
    if (networksFound <= 0) {
        Serial.println("❌ Nenhuma rede WiFi encontrada para geolocalização");
        return false;
    }
    
    Serial.printf("✅ Encontradas %d redes WiFi\n", networksFound);
    
    // Criar JSON com dados das redes WiFi (REDUZIDO: máximo 3 redes)
    DynamicJsonDocument doc(1536); // Reduzido de 2048 para 1536
    JsonArray wifiAccessPoints = doc.createNestedArray("wifiAccessPoints");
    
    int maxNetworks = min(networksFound, 3); // Reduzido de 5 para 3 (economiza memória)
    for (int i = 0; i < maxNetworks; i++) {
        JsonObject ap = wifiAccessPoints.createNestedObject();
        ap["macAddress"] = WiFi.BSSIDstr(i);
        ap["signalStrength"] = WiFi.RSSI(i);
        ap["channel"] = WiFi.channel(i);
        yield(); // Alimentar watchdog
    }
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    doc.clear(); // Liberar memória do documento JSON
    
    Serial.println("📤 Consultando Google Geolocation API...");
    Serial.printf("💾 Heap livre antes do HTTPS: %d bytes\n", ESP.getFreeHeap());
    
    // CRÍTICO: Delay antes de HTTPS para estabilizar memória
    delay(500);
    yield();
    
    // Fazer requisição para Google Geolocation API
    HTTPClient http;
    String apiUrl = String(googleGeoApiUrl) + "?key=" + String(googleGeoApiKey);
    http.begin(apiUrl);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000); // Aumentado timeout para 15s
    http.setReuse(false);   // CRÍTICO: Desabilitar keep-alive
    
    int httpCode = http.POST(jsonPayload);
    String response = http.getString();
    
    // CRÍTICO: Forçar limpeza completa da conexão
    http.end();
    
    // Aguardar liberação total de recursos SSL (CRÍTICO)
    Serial.println("🧹 Limpando recursos SSL...");
    delay(1000); // Delay agressivo
    yield();
    delay(500);
    yield();
    
    Serial.printf("💾 Heap livre após HTTPS: %d bytes\n", ESP.getFreeHeap());
    
    if (httpCode == 200) {
        // Parse da resposta
        DynamicJsonDocument responseDoc(1024);
        DeserializationError error = deserializeJson(responseDoc, response);
        
        if (!error) {
            latitude = responseDoc["location"]["lat"].as<double>();
            longitude = responseDoc["location"]["lng"].as<double>();
            geoAccuracy = responseDoc["accuracy"].as<float>();
            geoLocationValid = true;
            
            responseDoc.clear(); // Liberar memória
            
            Serial.println("✅ Geolocalização obtida:");
            Serial.printf("   Latitude: %.6f\n", latitude);
            Serial.printf("   Longitude: %.6f\n", longitude);
            Serial.printf("   Precisão: %.0f metros\n", geoAccuracy);
            Serial.printf("   Google Maps: https://maps.google.com/?q=%.6f,%.6f\n", latitude, longitude);
            Serial.printf("💾 Heap livre após parse: %d bytes\n", ESP.getFreeHeap());
            
            return true;
        } else {
            Serial.printf("❌ Erro ao parsear resposta: %s\n", error.c_str());
            return false;
        }
    } else {
        Serial.printf("❌ Erro na API Google (HTTP %d)\n", httpCode);
        if (httpCode == 400) {
            Serial.println("   Verifique se a API key está correta");
        } else if (httpCode == 403) {
            Serial.println("   API key inválida ou sem permissão");
        } else if (httpCode == 429) {
            Serial.println("   Limite de requisições excedido");
        }
        Serial.println("   Resposta: " + response);
        return false;
    }
}

/**
 * Verifica se precisa atualizar a geolocalização
 * - Atualiza 1x por dia (ou intervalo configurado)
 * - Força atualização se ainda não tem coordenadas no device
 * 
 * Retorna: true se deve buscar novas coordenadas
 */
bool shouldUpdateGeolocation()
{
    if (!useGeolocation) {
        return false;
    }
    
    // Obter timestamp da última atualização salvo nas Preferences
    unsigned long savedLastUpdate = preferences.getULong("last_geo_update", 0);
    unsigned long currentTime = millis();
    
    // Se nunca atualizou ou passou mais de 24h desde a última atualiz ação
    if (savedLastUpdate == 0) {
        Serial.println("📍 Primeira atualização de geolocalização");
        return true;
    }
    
    // Verificar se passou tempo suficiente (considera overflow do millis)
    unsigned long timeSinceUpdate;
    if (currentTime >= savedLastUpdate) {
        timeSinceUpdate = currentTime - savedLastUpdate;
    } else {
        // Overflow do millis() (a cada ~49 dias)
        timeSinceUpdate = (ULONG_MAX - savedLastUpdate) + currentTime;
    }
    
    if (timeSinceUpdate >= GEO_UPDATE_INTERVAL) {
        Serial.printf("📍 Atualização periódica (última: %lu ms atrás)\n", timeSinceUpdate);
        return true;
    }
    
    Serial.printf("📍 Geolocalização atualizada recentemente (próxima em %lu min)\n", 
                  (GEO_UPDATE_INTERVAL - timeSinceUpdate) / 60000);
    return false;
}

/**
 * Sincroniza as coordenadas obtidas com a tabela devices no Supabase
 * Atualiza os campos lat e lng do dispositivo
 * 
 * Deve ser chamado após getWiFiGeolocation() retornar true
 * 
 * Retorna: true se sincronizou com sucesso
 */
bool syncDeviceGeolocation()
{
    if (!geoLocationValid) {
        Serial.println("⚠️ Coordenadas inválidas - não sincronizando");
        return false;
    }
    
    Serial.println("📍 Sincronizando coordenadas com tabela devices...");
    Serial.printf("   Latitude: %.6f\n", latitude);
    Serial.printf("   Longitude: %.6f\n", longitude);
    Serial.printf("   Precisão: %.0f metros\n", geoAccuracy);
    
    // CRÍTICO: Marcar geolocalização como completa ANTES da chamada HTTP
    // Isso garante que mesmo se travar durante/após o HTTPS, não executará novamente
    preferences.putBool("geo_completed", true);
    preferences.putULong("last_geo_update", millis());
    Serial.println("🔒 Geolocalização registrada (executará novamente em 24h)");
    
    // CRÍTICO: Delay longo antes de SEGUNDA conexão HTTPS
    Serial.println("⏳ Aguardando 3 segundos para estabilizar heap antes do Supabase...");
    for (int i = 0; i < 6; i++) {
        delay(500);
        yield(); // Alimentar watchdog
    }
    
    Serial.printf("💾 Heap livre antes do Supabase: %d bytes\n", ESP.getFreeHeap());
    
    HTTPClient http;
    http.begin(String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    http.setTimeout(15000); // Aumentado para 15s
    http.setReuse(false);   // CRÍTICO: Desabilitar keep-alive
    
    // Criar JSON com campos de geolocalização
    // IMPORTANTE: Não criar nested metadata para não sobrescrever o existente
    DynamicJsonDocument doc(512);
    doc["lat"] = latitude;
    doc["lng"] = longitude;
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    Serial.println("📤 Payload: " + jsonPayload);
    
    int httpCode = http.PATCH(jsonPayload);
    String response = http.getString();
    
    Serial.printf("📥 Resposta HTTP: %d\n", httpCode);
    if (response.length() > 0 && response.length() < 500) {
        Serial.println("   Body: " + response);
    }
    
    // CRÍTICO: Forçar limpeza AGRESSIVA da conexão HTTPS
    http.end();
    
    // Aguardar liberação TOTAL de recursos SSL/TLS (DELAY AGRESSIVO)
    Serial.println("🧹 Limpando recursos SSL após Supabase...");
    for (int i = 0; i < 4; i++) {
        delay(500);
        yield(); // Alimentar watchdog
    }
    
    Serial.printf("💾 Heap livre após limpeza: %d bytes\n", ESP.getFreeHeap());
    
    bool success = (httpCode >= 200 && httpCode < 300);
    
    if (success) {
        Serial.println("✅ Coordenadas sincronizadas com devices!");
    } else {
        Serial.printf("❌ Erro ao sincronizar (HTTP %d)\n", httpCode);
        Serial.println("   Resposta: " + response);
    }
    
    // CRÍTICO: Após geolocalização, FORÇAR RESTART para limpar estado SSL
    // Isso previne travamento permanente e consumo de bateria
    // No próximo boot, a verificação de intervalo impedirá nova execução imediata
    Serial.println("🔄 REINICIANDO ESP32 para limpar SSL após geolocalização...");
    Serial.println("💾 Coordenadas salvas! Próximo boot seguirá fluxo normal.");
    Serial.flush();
    delay(500); // Garantir que Preferences foram gravadas na flash
    ESP.restart();
    
    return success;
}

// ============================================================================
// 🔋 FUNÇÕES DO MODO BATERIA (DEEP SLEEP)
// ============================================================================

void showWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch(wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("🔘 Acordou por sinal externo (RTC_IO)");
            break;  
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("🔘 Acordou por sinal externo (RTC_CNTL)");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.printf("⏰ Acordou pelo timer (%d minutos completados)\n", readingIntervalSec / 60);
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            Serial.println("👆 Acordou pelo touchpad");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            Serial.println("🔬 Acordou pelo ULP program");
            break;
        default:
            Serial.println("🔌 Primeiro boot ou reset");
            break;
    }
}

void showBatteryStats()
{
    // Mostrar estatísticas de economia de energia
    unsigned long sleepCount = preferences.getULong("sleep_count", 0);
    unsigned long lastSleep = preferences.getULong("last_sleep", 0);
    
    if (sleepCount > 0) 
    {
        Serial.println("📊 Estatísticas do Modo Bateria:");
        Serial.println("   Ciclos de sleep: " + String(sleepCount));
        int cyclesPerDay = 1440 / (readingIntervalSec / 60); // Ciclos por dia baseado no intervalo
        Serial.println("   Dias funcionando: " + String(sleepCount / cyclesPerDay));
        Serial.println("   Consumo estimado: " + String(sleepCount * 0.08) + " mAh");
        
        // Calcular próximo sleep
        Serial.println("   Próximo sleep: em ~60 segundos");
        Serial.println("   Duração sleep: " + String(readingIntervalSec / 60) + " minutos (" + String(readingIntervalSec) + "s)");
    }
}

bool isConfigMode()
{
    // Verifica se o WiFi não está configurado
    bool wifiNotConfigured = !wm.getWiFiIsSaved();
    
    if (wifiNotConfigured) {
        Serial.println("📡 WiFi não configurado - Modo Configuração");
        return true;
    }
    
    // Verificar botão com múltiplas leituras (debounce)
    Serial.println("🔘 Verificando botão CONFIG (GPIO0)...");
    int buttonPressCount = 0;
    for (int i = 0; i < 10; i++) {
        if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
            buttonPressCount++;
        }
        delay(50); // 50ms entre leituras = 500ms total de janela
    }
    
    // Se detectou botão pressionado em pelo menos 5 das 10 leituras
    if (buttonPressCount >= 5) {
        Serial.printf("🔧 Botão CONFIG detectado (%d/10 leituras) - Modo Configuração\n", buttonPressCount);
        
        // Aguardar usuário soltar o botão e piscar LED para confirmar
        Serial.println("💡 Aguardando soltar o botão...");
        blinkLED(5, 100);
        while (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
            delay(100);
        }
        
        // Resetar configurações WiFi para abrir portal
        Serial.println("🔧 Resetando WiFi para abrir portal de configuração...");
        wm.resetSettings();
        blinkLED(3, 200);
        
        return true;
    }
    
    Serial.println("🔋 WiFi configurado e botão não pressionado - Modo Bateria (com sleep)");
    return false;
}

float performQuickReading()
{
    Serial.println("\n🔋 === MODO BATERIA: LEITURA RÁPIDA ===");
    
    // ===== VERIFICAÇÃO ADICIONAL DO BOTÃO =====
    // Dar uma última chance para o usuário pressionar o botão antes de iniciar
    Serial.println("🔘 Verificando botão CONFIG antes de iniciar leitura (2 segundos)...");
    unsigned long checkStart = millis();
    while (millis() - checkStart < 2000) {
        if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
            Serial.println("\n🔧 ========================================");
            Serial.println("🔧  BOTÃO PRESSIONADO DURANTE BOOT!");
            Serial.println("🔧  Entrando em modo configuração...");
            Serial.println("🔧 ========================================");
            
            // Aguardar soltar botão
            blinkLED(5, 100);
            while (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
                delay(100);
            }
            
            // Resetar WiFi e reiniciar para entrar no modo configuração
            wm.resetSettings();
            Serial.println("🔄 Reiniciando para modo configuração...");
            delay(1000);
            ESP.restart();
        }
        delay(100);
    }
    Serial.println("✅ Botão não pressionado - continuando leitura rápida");
    
    Serial.println("💡 LED: PISCANDO durante operação");
    
    // Iniciar LED piscando automaticamente
    ledOperationMode();
    
    // ===== LER VARIÁVEIS DE ESTADO (Bateria, etc) =====
    float batteryVoltage = readBatteryVoltage();
    float batteryPercentage = getBatteryPercentage(batteryVoltage);
    String batteryStatus = getBatteryStatus(batteryVoltage, batteryPercentage);
    
    // Flag para indicar bateria crítica (mas continuar enviando alerta)
    bool isCriticalBattery = (batteryVoltage <= BATTERY_CRITICAL_VOLTAGE && batteryVoltage > BATTERY_FINAL_WARNING_VOLTAGE);
    
    // ===== MODO ÚLTIMO AVISO: Bateria em 2.5V =====
    // Envia 4 leituras consecutivas e depois hiberna até recarregar
    bool isFinalWarning = (batteryVoltage <= BATTERY_FINAL_WARNING_VOLTAGE);
    
    // ===== ETAPA 0: LER SENSORES (ADC1 - funciona com WiFi) =====
    Serial.println("\n[0/5] 🌡️ Lendo PT100 (MAX31865 / SPI)...");
    Serial.printf("✅ SPI pinos: MOSI=%d MISO=%d SCK=%d CS=%d\n", MAX31865_MOSI_PIN, MAX31865_MISO_PIN, MAX31865_SCK_PIN, MAX31865_CS_PIN);
    
    // Ler PT100
    float temperature = readPT100Temperature();
    float humidity = 0.0; // PT100 não mede umidade
    
    bool sensorOk = isfinite(temperature);
    if (!sensorOk) {
        Serial.println("❌ PT100 desconectado/erro - ignorando leitura");
    }
    
    if (sensorOk) {
        Serial.printf("🌡️ PT100: %.1f°C (sem validação - enviando direto)\n", temperature);
        if (temperature < -200) {
            Serial.println("❄️ MODO CRIOGÊNICO ativado (T < -200°C)");
        }
    }
    
    if (isFinalWarning) {
        Serial.println("\n🚨 ════════════════════════════════════════════════════");
        Serial.println("🚨  BATERIA MUITO BAIXA (2.5V) - MODO ÚLTIMO AVISO!");
        Serial.println("🚨  Serão enviadas 4 leituras e depois HIBERNAÇÃO");
        Serial.println("🚨 ════════════════════════════════════════════════════\n");
    } else if (isCriticalBattery) {
        Serial.println("⚠️ BATERIA CRÍTICA - Enviando alerta antes de sleep");
    }
    
    // ===== ALERTAS DE TEMPERATURA E LÓGICA DE ENVIO CONTÍNUO =====
    // Se a temperatura estiver ALTA (Zona 3: > -120°C), entrar em modo de alerta contínuo
    if (temperature > TEMP_LOW_ALERT_THRESHOLD) {
        Serial.println("\n🚨 ALERTA DE TEMPERATURA ALTA DETECTADO!");
        Serial.println("🚨 Iniciando ciclo de envio contínuo e alerta sonoro/visual...");
        
        // 1. Conectar WiFi (se não estiver conectado)
        if (WiFi.status() != WL_CONNECTED) {
            connectWiFiQuick();
        }
        
        // 2. Enviar dados iniciais do alerta
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("📤 Enviando alerta inicial ao servidor...");
            ensureDeviceRegistered();
            syncOffsetWithServer();
            syncDeviceLocationWithServer();
            bool sent = sendSensorData(temperature, humidity);
            checkBatteryAlert(batteryVoltage, batteryPercentage);
            if (sent) {
                updateDeviceBatteryStatus(batteryVoltage, batteryPercentage, batteryStatus);
            }

            Serial.println("🔄 OTA: Checando atualização durante alerta...");
            FirmwareInfo fwInfo = checkFirmwareUpdate();
            if (fwInfo.available) {
                float batPct = getBatteryPercentage(readBatteryVoltage());
                if (canPerformUpdate(fwInfo, batPct, WiFi.RSSI())) {
                    performOTAUpdate(fwInfo);
                }
            }
        }
        
        // 3. Entrar em loop de monitoramento contínuo
        // Fica aqui até a temperatura baixar para nível seguro (< -150°C)
        monitorTemperatureUntilSafe(temperature);
        
        // 4. Quando retornar de monitorTemperatureUntilSafe, significa que normalizou
        // O código seguirá para o fluxo normal de sleep abaixo
    } else {
        // Temperatura normal ou zona de alerta frio (apenas LED1)
        // Segue fluxo padrão de envio único e sleep
        Serial.println("✅ Temperatura em nível seguro ou alerta frio (sem envio contínuo)");
    }
    
    // Aguardar sensor estabilizar
    delay(1000);
    
    Serial.println("\n✅ SENSORES LIDOS COM SUCESSO");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    // Se já conectou no loop de alerta (e retornou de lá), pode ser que o WiFi esteja desconectado
    // ou conectado. Verificar antes de prosseguir.
    if (WiFi.status() != WL_CONNECTED) {
        // [1/5] CONECTAR WIFI (5 tentativas, 15s)
        Serial.println("\n[1/5] Conectando WiFi (5 tentativas)...");
        digitalWrite(LED_PIN, HIGH); // LED piscando iniciado
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
        
        if (!connectWiFiQuick()) 
        {
            Serial.println("❌ [1/4] WiFi FALHOU após 5 tentativas - entrando em sleep");
            ledFailure(); // 2 piscadas = falha
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            ledOff();
            return 0.0; // Retorna 0 (falha) - irá para sleep
        }
    }
    
    // WiFi OK - pisca LED
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
    
    // [2/4] TESTAR INTERNET (5 tentativas, 15s)
    Serial.println("\n[2/4] Testando Internet (ping Google - 5 tentativas)...");
    bool internetOk = false;
    for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++) 
    {
        Serial.printf("🔄 [%d/%d] Tentando ping Google...\n", attempt, MAX_RETRY_ATTEMPTS);
        
        HTTPClient http;
        http.begin("http://www.google.com");
        http.setTimeout(5000);
        int httpCode = http.GET();
        http.end();
        
        if (httpCode > 0) {
            Serial.printf("✅ Internet OK na tentativa %d | HTTP Code: %d\n", attempt, httpCode);
            internetOk = true;
            // LED pisca = progresso
            digitalWrite(LED_PIN, HIGH);
            delay(200);
            digitalWrite(LED_PIN, LOW);
            delay(200);
            break;
        }
        
        if (attempt < MAX_RETRY_ATTEMPTS) {
            Serial.printf("⏳ Aguardando %ds...\n", RETRY_INTERVAL_SECONDS);
            delay(RETRY_INTERVAL_SECONDS * 1000);
        }
    }
    
    if (!internetOk) {
        Serial.println("❌ [2/4] Internet FALHOU após 5 tentativas - entrando em sleep");
        ledFailure(); // 2 piscadas = falha
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        ledOff();
        return 0.0; // Retorna 0 (falha internet)
    }
    startLedBlink();
    
    // [3/4] TESTAR SUPABASE (5 tentativas, 15s)
    Serial.println("\n[3/4] Testando Supabase (5 tentativas)...");
    bool supabaseOk = false;
    for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++) 
    {
        Serial.printf("🔄 [%d/%d] Tentando conectar Supabase...\n", attempt, MAX_RETRY_ATTEMPTS);
        
        HTTPClient http;
        http.begin(String(supabaseUrl) + "/auth/v1/health");
        http.setTimeout(5000);
        int httpCode = http.GET();
        http.end();
        
        if (httpCode == 200) {
            Serial.printf("✅ Supabase conectado na tentativa %d\n", attempt);
            supabaseOk = true;
            // LED pisca = progresso
            digitalWrite(LED_PIN, HIGH);
            delay(200);
            digitalWrite(LED_PIN, LOW);
            delay(200);
            break;
        }
        
        if (attempt < MAX_RETRY_ATTEMPTS) {
            Serial.printf("⏳ Aguardando %ds...\n", RETRY_INTERVAL_SECONDS);
            delay(RETRY_INTERVAL_SECONDS * 1000);
        }
    }
    
    if (!supabaseOk) {
        Serial.println("❌ [3/4] Supabase FALHOU após 5 tentativas - entrando em sleep");
        ledFailure(); // 2 piscadas = falha
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        ledOff();
        return 0.0; // Retorna 0 (falha supabase)
    }
    startLedBlink();
    
    // [3B/6] 🔍 VERIFICAR SE DISPOSITIVO ESTÁ CADASTRADO
    Serial.println("\n[3B/6] 🔍 Verificando cadastro do dispositivo...");
    if (!ensureDeviceRegistered()) {
        Serial.println("⚠️ Erro ao verificar/cadastrar dispositivo - continuando");
    }
    yield();
    delay(500);
    
    // [3C/6] 📋 VERIFICAR SE DISPOSITIVO ESTÁ HABILITADO
    Serial.println("\n[3C/6] 📋 Verificando se dispositivo está habilitado...");
    if (!checkDeviceEnabled()) {
        Serial.println("🚫 Dispositivo BLOQUEADO - entrando em sleep");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        ledOff();
        return 0.0; // Retorna 0 (dispositivo bloqueado)
    }
    yield();
    delay(500);
    
    // [3D/6] 🔄 SINCRONIZAR INTERVALO DE LEITURA
    Serial.println("\n[3D/6] 🔄 Sincronizando intervalo de leitura...");
    syncReadingInterval();
    yield();
    delay(500);
    
    // [3E/6] 🔄 SINCRONIZAR OFFSET COM SERVIDOR
    Serial.println("\n[3E/6] 🔄 Sincronizando offset de temperatura...");
    Serial.println("📥 Baixando offset do servidor (se disponível)...");
    
    // SEMPRE sincronizar do servidor primeiro
    if (!syncOffsetWithServer()) {
        // Se servidor não tem offset (retorna 0.0), reportar o offset local
        Serial.println("⚠️ Servidor sem offset configurado - usando offset local");
        Serial.printf("📤 Reportando offset local %.1f°C ao servidor\n", tempOffset);
        reportOffsetToServer();
    }

    Serial.println("\n[3F/6] 🔄 Sincronizando localização...");
    syncDeviceLocationWithServer();
    
    yield();
    delay(500);
    
    // [4A/5] SE BATERIA CRÍTICA: Enviar alerta (NÃO BLOQUEANTE)
    if (isCriticalBattery) {
        Serial.println("\n[4A/5] ⚠️ ENVIANDO ALERTA DE BATERIA CRÍTICA (tentativa única, não bloqueante)...");
        
        // Tenta enviar alerta mas NÃO bloqueia se falhar
        if (checkBatteryAlert(batteryVoltage, batteryPercentage)) {
            Serial.println("✅ Alerta de bateria enviado");
        } else {
            Serial.println("⚠️ Alerta de bateria falhou (será reenviado no próximo ciclo)");
        }
        
        yield(); // Feed watchdog
        delay(500);
    }
    
    // ===== HEARTBEAT DESABILITADO PARA ECONOMIA DE BATERIA =====
    // O heartbeat era redundante pois os dados de bateria já são enviados junto com
    // as leituras do sensor em sendSensorData(). Economiza ~2-3 segundos de WiFi ativo
    // e ~480mAh por envio, aumentando autonomia de 58 para ~70 dias.
    /*
    // [4B/5] SEMPRE ENVIAR HEARTBEAT (independente de alerta)
    Serial.println("\n[4B/5] 💓 Enviando heartbeat + dados de bateria...");
    
    HTTPClient httpHeartbeat;
    httpHeartbeat.begin(String(supabaseUrl) + "/rest/v1/devices?id=eq." + deviceId);
    httpHeartbeat.addHeader("Content-Type", "application/json");
    httpHeartbeat.addHeader("apikey", supabaseKey);
    httpHeartbeat.addHeader("Authorization", "Bearer " + String(supabaseKey));
    httpHeartbeat.addHeader("Prefer", "return=minimal");
    httpHeartbeat.setTimeout(8000);
    
    DynamicJsonDocument docHeartbeat(512);
    docHeartbeat["last_seen"] = getISOTimestamp();
    docHeartbeat["is_online"] = true;
    docHeartbeat["battery_voltage"] = batteryVoltage;
    docHeartbeat["battery_percentage"] = (int)batteryPercentage;
    docHeartbeat["battery_status"] = getBatteryStatus(batteryVoltage, batteryPercentage);
    docHeartbeat["battery_low_alert"] = (batteryVoltage <= BATTERY_LOW_VOLTAGE);
    docHeartbeat["battery_critical_alert"] = (batteryVoltage <= BATTERY_CRITICAL_VOLTAGE);
    docHeartbeat["last_battery_check"] = getISOTimestamp();
    docHeartbeat["power_mode"] = "BATTERY";
    docHeartbeat["signal_strength"] = WiFi.RSSI();
    
    String jsonHeartbeat;
    serializeJson(docHeartbeat, jsonHeartbeat);
    
    int httpCodeHeartbeat = httpHeartbeat.PATCH(jsonHeartbeat);
    httpHeartbeat.end();
    
    if (httpCodeHeartbeat == 200 || httpCodeHeartbeat == 204) {
        Serial.println("✅ Heartbeat enviado - device ONLINE no app");
    } else {
        Serial.printf("⚠️ Heartbeat falhou (code: %d)\n", httpCodeHeartbeat);
    }
    
    yield();
    delay(500);
    */
    
    // [5C/6] ENVIAR DADOS DO SENSOR (JÁ LIDOS NO INÍCIO)
    Serial.println("\n[5/5] 📤 Enviando dados do sensor (lidos antes de conectar WiFi)...");
    Serial.printf("   Temperatura: %.1f°C | Umidade: %.1f%% | Bateria: %.2fV\n", 
                  temperature, humidity, batteryVoltage);
    
    if (sensorOk) {
        Serial.println("✅ Dados válidos - preparando envio...");
        
        // ===== MODO ÚLTIMO AVISO: Enviar 4x e hibernar =====
        if (isFinalWarning) {
            Serial.println("🚨 MODO ÚLTIMO AVISO - 4 envios");
            
            for (int sendNum = 1; sendNum <= BATTERY_FINAL_SEND_COUNT; sendNum++) {
                Serial.printf("📤 Envio %d/4\n", sendNum);
                
                bool isLastSend = (sendNum == BATTERY_FINAL_SEND_COUNT);
                
                if (isLastSend) {
                    sendFinalBatteryNotification(temperature, humidity, batteryVoltage);
                } else {
                    if (sendSensorData(temperature, humidity)) {
                        Serial.println("✅ OK");
                    } else {
                        Serial.println("❌ Falhou");
                    }
                }
                
                if (!isLastSend) {
                    delay(5000);
                    yield();
                }
            }
            
            Serial.println("💤 HIBERNANDO - recarregue bateria");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            ledOff();
            
            for (int i = 0; i < 10; i++) {
                digitalWrite(LED_PIN, HIGH);
                delay(100);
                digitalWrite(LED_PIN, LOW);
                delay(100);
            }
            
            Serial.println("💤 Hibernação ativada");
            Serial.flush();
            
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
            esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
            esp_deep_sleep_start();
            
            return temperature; // Retorna temperatura lida (hibernando)
        }
        
        // ===== MODO NORMAL: Envio único (SEM RETRY para evitar duplicatas) =====
        // NOTA: Removido loop de retry pois causava duplicatas no Supabase
        // quando o POST era bem-sucedido mas a resposta demorava mais que o timeout.
        // Com timeout aumentado para 15s, uma única tentativa deve ser suficiente.
        Serial.printf("📤 Enviando dados (RSSI: %d dBm, timeout: 15s)...\n", WiFi.RSSI());
        
        if (sendSensorData(temperature, humidity)) {
            Serial.println("✅ Dados enviados com sucesso");
            
            // ===== ENVIAR ALERTA DE BATERIA SE NECESSÁRIO =====
            // Envia alerta se bateria estiver baixa ou crítica (e ainda não enviado)
            checkBatteryAlert(batteryVoltage, batteryPercentage);
            
            // ===== ATUALIZAR STATUS DE BATERIA NA TABELA DEVICES =====
            // Isso garante que as tabelas devices fiquem sincronizadas com sensor_readings
            updateDeviceBatteryStatus(batteryVoltage, batteryPercentage, batteryStatus);
            
            ledSuccess();
        } else {
            Serial.println("❌ Envio falhou - dados serão enviados no próximo ciclo");
            Serial.println("⚠️ NOTA: Não reenviando para evitar duplicatas");
            ledFailure();
        }
    } else {
        Serial.println("❌ Erro sensor PT100");
        for(int i = 0; i < 5; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(800);
            digitalWrite(LED_PIN, LOW);
            delay(800);
        }
    }
    
    // ===== [6/7] GEOLOCALIZAÇÃO (POR ÚLTIMO - se travar, dados já foram salvos) =====
    // Executado após todos os dados críticos terem sido enviados
    // Se travar aqui, o sistema já cumpriu sua função principal
    if (useGeolocation && shouldUpdateGeolocation()) {
        Serial.println("📍 Obtendo geolocalização...");
        if (getWiFiGeolocation()) {
            Serial.println("📍 Sincronizando coordenadas...");
            syncDeviceGeolocation();
            delay(1000);
            yield();
        }
    }
    
    // ===== [7/7] VERIFICAR ATUALIZAÇÕES OTA (ANTES DE DESCONECTAR WIFI) =====
    if (shouldCheckForUpdate()) {
        Serial.println("\n🔍 ========== CHECANDO ATUALIZAÇÕES OTA ==========");
        
        FirmwareInfo firmware = checkFirmwareUpdate();
        
        if (firmware.available) {
            float batteryVoltage = readBatteryVoltage();
            float batteryPercentage = getBatteryPercentage(batteryVoltage);
            int rssi = WiFi.RSSI();
            
            if (canPerformUpdate(firmware, batteryPercentage, rssi)) {
                performOTAUpdate(firmware);  // Vai reiniciar se sucesso
            } else {
                Serial.println("⏸️  OTA: Aguardando melhores condições");
            }
        }
    }
    
    // ===== FINALIZAÇÃO: Desconectar WiFi e preparar para sleep =====
    yield();
    Serial.println("\n📴 Desconectando WiFi para economizar energia...");
    Serial.flush();
    yield();
    
    WiFi.disconnect(true);
    delay(1000); // Delay maior para garantir desconexão
    yield();
    
    WiFi.mode(WIFI_OFF);
    ledOff();
    delay(1000);
    yield();
    
    Serial.println("✅ WiFi desconectado com sucesso");
    Serial.flush();
    delay(500);
    yield();
    
    Serial.println("\n✅ Sequência completa - pronto para sleep");
    Serial.flush();
    delay(500);
    yield();
    
    Serial.println("🔄 Retornando temperatura para verificação de sleep...");
    Serial.flush();
    delay(500);
    yield();
    
    return temperature; // Retorna temperatura para decisão de sleep
}

void monitorTemperatureUntilSafe(float initialTemp) {
    Serial.println("🔄 ALERTA ATIVO: Monitoramento contínuo iniciado...");
    Serial.println("✅ Retornará ao sleep APENAS quando T < -150°C");
    
    // Configurações do ciclo de alerta
    const unsigned long RECHECK_INTERVAL = 30000; // 30 segundos entre leituras do sensor
    // USAR INTERVALO CONFIGURADO NO DISPOSITIVO/SERVIDOR PARA REENVIO (readingIntervalSec)
    // Se o intervalo for muito curto (< 5 min), forçar mínimo de 5 min para evitar flood
    unsigned long resendIntervalMs = readingIntervalSec * 1000;
    if (resendIntervalMs < 300000) resendIntervalMs = 300000; // Mínimo 5 minutos em alerta
    
    Serial.printf("⏱️ Intervalo de reenvio configurado para: %lu minutos\n", resendIntervalMs / 60000);
    
    unsigned long lastCheck = 0;
    unsigned long lastSend = millis(); // Já enviou o inicial antes de entrar aqui
    
    // Desconectar WiFi para economizar bateria enquanto apenas monitora/alerta localmente
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("📴 Desconectando WiFi para economia durante alerta local...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        ledOff();
    }

    float currentTemp = initialTemp;

    while (true) {
        if (isfinite(currentTemp)) {
            checkTemperatureAlerts(currentTemp);
        }
        
        unsigned long now = millis();
        
        // 2. A cada 30s: Ler sensor novamente
        if (now - lastCheck >= RECHECK_INTERVAL) {
            lastCheck = now;
            
            // Leitura rápida do sensor
            currentTemp = readPT100Temperature();
            if (!isfinite(currentTemp)) {
                Serial.println("❌ PT100 desconectado/erro - monitoramento pausado");
                continue;
            }
            Serial.printf("🌡️ Monitoramento: %.1f°C\n", currentTemp);
            
            // SE NORMALIZOU (< -150°C): Sair do loop e permitir sleep
            if (currentTemp < TEMP_LOW_THRESHOLD) {
                Serial.println("✅ TEMPERATURA NORMALIZADA! Encerrando alerta.");
                
                // Desligar alertas
                digitalWrite(TEMP_ALERT_LED1, LOW);
                digitalWrite(TEMP_ALERT_LED2, LOW);
                digitalWrite(TEMP_ALERT_BUZZER, LOW);
                
                // Enviar leitura final de normalização
                connectWiFiQuick();
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("📤 Enviando leitura de normalização...");
                    sendSensorData(currentTemp, 0.0);
                }
                return; // Retorna para performQuickReading -> Sleep
            }
        }
        
        // 3. Respeitar intervalo configurado para reenvio
        if (now - lastSend >= resendIntervalMs) {
            lastSend = now;
            
            Serial.printf("🔄 Ciclo de reenvio (%lu min): Conectando WiFi...\n", resendIntervalMs / 60000);
            connectWiFiQuick();
            
            if (WiFi.status() == WL_CONNECTED) {
                // Sincronizar intervalo se possível (caso tenha mudado no servidor)
                syncReadingInterval();
                
                // Atualizar intervalo de reenvio localmente
                unsigned long newInterval = readingIntervalSec * 1000;
                if (newInterval >= 300000) { // Só atualiza se for >= 5 min
                    resendIntervalMs = newInterval;
                    Serial.printf("⏱️ Intervalo atualizado: %lu min\n", resendIntervalMs / 60000);
                }

                Serial.println("📤 Reenviando dados de alerta...");
                if (isfinite(currentTemp)) {
                    sendSensorData(currentTemp, 0.0);
                }
                
                // Checar OTA também durante o alerta (opcional, mas bom para correções)
                if (shouldCheckForUpdate()) {
                    FirmwareInfo fw = checkFirmwareUpdate();
                    if (fw.available && canPerformUpdate(fw, getBatteryPercentage(readBatteryVoltage()), WiFi.RSSI())) {
                        performOTAUpdate(fw);
                    }
                }
                
                Serial.println("📴 Desconectando WiFi novamente...");
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                ledOff();
            }
        }
        
        // Pequeno delay para não travar CPU (watchdog) e permitir o piscar dos LEDs
        delay(100); 
        yield();
    }
}

// ============================================================================
// 🚨 LÓGICA DE ALERTA CONTÍNUO (Loop até normalizar)
// ============================================================================
// (A definição duplicada foi removida daqui)

bool connectWiFiQuick()
{
    Serial.println("📶 Conectando WiFi (modo rápido com reconexão robusta)...");
    
    // Desligar WiFi completamente antes de reconectar
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    ledOff();
    delay(500);
    
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    setStatusLedAllowed(true);
    startLedBlink();
    
    // Primeiro: tentar scan para verificar se a rede está disponível
    String savedSSID = wm.getWiFiSSID();
    if (savedSSID.length() > 0) {
        Serial.println("🔍 Procurando rede salva: " + savedSSID);
        
        if (!scanForSavedNetwork()) {
            Serial.println("⚠️ Rede não encontrada no scan - tentando conexão mesmo assim");
        }
    }
    
    // Tentar conexão com múltiplas estratégias
    for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++) 
    {
        // ===== VERIFICAR BOTÃO A CADA TENTATIVA =====
        if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
            Serial.println("\n🔧 ========================================");
            Serial.println("🔧  BOTÃO PRESSIONADO DURANTE CONEXÃO!");
            Serial.println("🔧  Entrando em modo configuração...");
            Serial.println("🔧 ========================================");
            blinkLED(5, 100);
            while (digitalRead(CONFIG_BUTTON_PIN) == LOW) { delay(100); }
            wm.resetSettings();
            Serial.println("🔄 Reiniciando para modo configuração...");
            delay(1000);
            ESP.restart();
        }
        
        Serial.printf("\n🔄 [%d/%d] Tentativa de conexão WiFi...\n", attempt, MAX_RETRY_ATTEMPTS);
        
        // ESTRATÉGIA 1: Reconexão silenciosa (sem portal)
        if (reconnectWiFiSilent()) {
            Serial.printf("✅ WiFi conectado na tentativa %d (método silencioso)!\n", attempt);
            goto wifi_connected;
        }
        
        // Verificar botão após reconexão silenciosa
        if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
            Serial.println("\n🔧 BOTÃO DETECTADO - Entrando em modo configuração...");
            blinkLED(5, 100);
            while (digitalRead(CONFIG_BUTTON_PIN) == LOW) { delay(100); }
            wm.resetSettings();
            delay(1000);
            ESP.restart();
        }
        
        // ESTRATÉGIA 2: Usar WiFi.begin() direto com credenciais salvas
        if (savedSSID.length() > 0) {
            Serial.println("   Tentando WiFi.begin() com credenciais salvas...");
            WiFi.begin(); // Usa credenciais salvas automaticamente
            
            int waitTime = 0;
            while (WiFi.status() != WL_CONNECTED && waitTime < WIFI_TIMEOUT_SECONDS * 1000) {
                delay(500);
                waitTime += 500;
                Serial.print(".");
                yield(); // Feed watchdog
                
                // Verificar botão durante espera WiFi
                if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
                    Serial.println("\n🔧 BOTÃO DETECTADO - Entrando em modo configuração...");
                    blinkLED(5, 100);
                    while (digitalRead(CONFIG_BUTTON_PIN) == LOW) { delay(100); }
                    wm.resetSettings();
                    delay(1000);
                    ESP.restart();
                }
            }
            Serial.println();
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("✅ WiFi conectado na tentativa %d (método begin)!\n", attempt);
                goto wifi_connected;
            }
        }
        
        Serial.printf("❌ Tentativa %d falhou | Status: %d\n", attempt, WiFi.status());
        
        // Desconectar e limpar antes da próxima tentativa
        WiFi.disconnect(true);
        delay(1000);
        
        if (attempt < MAX_RETRY_ATTEMPTS) {
            // Backoff progressivo: 5s, 10s, 15s, 20s, 25s
            int waitDelay = RETRY_INTERVAL_SECONDS + (attempt * 5);
            Serial.printf("⏳ Aguardando %ds antes da próxima tentativa... (Pressione BOOT para configurar)\n", waitDelay);
            
            // Aguardar com yield para não travar watchdog E verificar botão
            for (int i = 0; i < waitDelay; i++) {
                delay(1000);
                yield();
                
                // Verificar botão durante espera
                if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
                    Serial.println("\n🔧 BOTÃO DETECTADO - Entrando em modo configuração...");
                    blinkLED(5, 100);
                    while (digitalRead(CONFIG_BUTTON_PIN) == LOW) { delay(100); }
                    wm.resetSettings();
                    delay(1000);
                    ESP.restart();
                }
            }
        }
    }
    
    Serial.printf("❌ Falha na conexão WiFi após %d tentativas\n", MAX_RETRY_ATTEMPTS);
    ledOff();
    return false;
    
wifi_connected:
    Serial.println("   SSID: " + WiFi.SSID());
    Serial.println("   IP: " + WiFi.localIP().toString());
    Serial.println("   RSSI: " + String(WiFi.RSSI()) + " dBm");
    wifiWasEverConnected = true;
    consecutiveWifiFailures = 0;

    validateOTAUpdate();
    
    // Sincronizar horário via NTP
    syncNTP();
    
    return true;
}

void enterDeepSleep()
{
    Serial.println("😴 Entrando em Deep Sleep por " + String(readingIntervalSec / 60) + " minutos...");
    Serial.println("⏰ Próximo acordar em: " + String(readingIntervalSec) + " segundos (" + String(readingIntervalSec / 60) + " min)");
    Serial.println("🔋 Consumo: ~10µA durante o sono");
    Serial.println("💡 LED: APAGADO durante sleep");
    
    // Salvar estatísticas antes de dormir
    preferences.putULong("sleep_count", preferences.getULong("sleep_count", 0) + 1);
    preferences.putULong("last_sleep", millis());
    
    // APAGAR LED antes de dormir
    ledOff();
    
    // DESLIGAR ALERTAS DE TEMPERATURA antes de dormir (economia de energia)
    digitalWrite(TEMP_ALERT_LED1, LOW);
    digitalWrite(TEMP_ALERT_LED2, LOW);
    digitalWrite(TEMP_ALERT_BUZZER, LOW);
    Serial.println("🔇 Alertas de temperatura desligados");
    
    // Configurar timer para acordar com intervalo configurável
    esp_sleep_enable_timer_wakeup(readingIntervalSec * 1000000ULL); // microsegundos
    
    // Também permitir acordar com botão (opcional)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0); // GPIO 0 = botão BOOT
    
    Serial.println("💤 Dormindo agora... ZZZ");
    Serial.flush(); // Garantir que mensagem seja enviada
    
    // Entrar em deep sleep
    esp_deep_sleep_start();
}

// ============================================================================
// 📶 FUNÇÕES DE RECONEXÃO WIFI ROBUSTA
// ============================================================================

/**
 * Reseta todos os contadores de reconexão WiFi
 */
void resetWiFiReconnectCounters()
{
    wifiDisconnectedSince = 0;
    wifiReconnectAttempts = 0;
    consecutiveWifiFailures = 0;
    lastWifiReconnectAttempt = 0;
    currentReconnectDelay = WIFI_RECONNECT_INITIAL_DELAY;
    Serial.println("📊 Contadores de reconexão resetados");
}

/**
 * Faz scan para verificar se a rede salva está disponível
 */
bool scanForSavedNetwork()
{
    String savedSSID = wm.getWiFiSSID();
    if (savedSSID.length() == 0) {
        Serial.println("⚠️ Nenhuma rede WiFi salva");
        return false;
    }
    
    Serial.println("🔍 Iniciando scan de redes WiFi...");
    
    for (int scanAttempt = 1; scanAttempt <= WIFI_SCAN_ATTEMPTS; scanAttempt++) {
        Serial.printf("   Scan %d/%d...\n", scanAttempt, WIFI_SCAN_ATTEMPTS);
        
        int networksFound = WiFi.scanNetworks(false, false, false, 300);
        
        if (networksFound > 0) {
            Serial.printf("   Encontradas %d redes:\n", networksFound);
            
            for (int i = 0; i < networksFound; i++) {
                String ssid = WiFi.SSID(i);
                int rssi = WiFi.RSSI(i);
                Serial.printf("     [%d] %s (RSSI: %d dBm)\n", i + 1, ssid.c_str(), rssi);
                
                if (ssid == savedSSID) {
                    Serial.printf("   ✅ Rede salva '%s' encontrada com sinal %d dBm!\n", savedSSID.c_str(), rssi);
                    WiFi.scanDelete();
                    return true;
                }
            }
        } else if (networksFound == 0) {
            Serial.println("   ⚠️ Nenhuma rede encontrada");
        } else {
            Serial.printf("   ❌ Erro no scan: %d\n", networksFound);
        }
        
        WiFi.scanDelete();
        
        if (scanAttempt < WIFI_SCAN_ATTEMPTS) {
            delay(2000);
        }
    }
    
    Serial.printf("   ❌ Rede '%s' não encontrada após %d scans\n", savedSSID.c_str(), WIFI_SCAN_ATTEMPTS);
    return false;
}

/**
 * Tenta reconectar ao WiFi silenciosamente (sem abrir portal captivo)
 * Esta função NUNCA abre o portal de configuração
 */
bool reconnectWiFiSilent()
{
    Serial.println("📡 Tentando reconexão silenciosa (sem portal)...");
    
    // Desligar WiFi completamente
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(500);
    
    // Religar em modo station
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    // Configurar WiFiManager para NÃO abrir portal
    wm.setConfigPortalTimeout(0);    // Timeout 0 = não abre portal
    wm.setConnectTimeout(WIFI_TIMEOUT_SECONDS);
    
    // IMPORTANTE: Usar setEnableConfigPortal(false) para garantir que não abre portal
    wm.setEnableConfigPortal(false);
    
    // Tentar conectar
    Serial.println("   Chamando wm.autoConnect() com portal DESABILITADO...");
    bool connected = wm.autoConnect();
    
    // Reabilitar portal para uso futuro (quando usuário quiser reconfigurar)
    wm.setEnableConfigPortal(true);
    
    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("   ✅ Reconexão silenciosa bem-sucedida!");
        return true;
    }
    
    Serial.printf("   ❌ Reconexão silenciosa falhou (status: %d)\n", WiFi.status());
    return false;
}

/**
 * Força reconexão WiFi usando múltiplas estratégias
 */
bool forceWiFiReconnect()
{
    Serial.println("\n🔧 FORÇANDO RECONEXÃO WIFI (múltiplas estratégias)...");
    
    // Estratégia 1: Reconexão silenciosa
    Serial.println("\n📋 Estratégia 1: Reconexão silenciosa");
    if (reconnectWiFiSilent()) {
        return true;
    }
    
    // Estratégia 2: WiFi.begin() direto
    Serial.println("\n📋 Estratégia 2: WiFi.begin() direto");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_STA);
    
    WiFi.begin(); // Usa credenciais salvas
    
    int waitTime = 0;
    while (WiFi.status() != WL_CONNECTED && waitTime < WIFI_TIMEOUT_SECONDS * 1000) {
        delay(500);
        waitTime += 500;
        Serial.print(".");
        yield();
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("   ✅ Conectado via WiFi.begin()!");
        return true;
    }
    
    // Estratégia 3: Reset do módulo WiFi
    Serial.println("\n📋 Estratégia 3: Reset do módulo WiFi");
    WiFi.disconnect(true, true); // Apaga credenciais da NVS temporariamente
    delay(1000);
    
    // Restaurar e tentar novamente
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
    
    // Usar autoConnect que vai usar credenciais do WiFiManager
    wm.setConfigPortalTimeout(0);
    wm.setEnableConfigPortal(false);
    
    if (wm.autoConnect()) {
        wm.setEnableConfigPortal(true);
        Serial.println("   ✅ Conectado após reset do módulo!");
        return true;
    }
    wm.setEnableConfigPortal(true);
    
    // Estratégia 4: Verificar se rede existe via scan
    Serial.println("\n📋 Estratégia 4: Verificar disponibilidade da rede");
    if (!scanForSavedNetwork()) {
        Serial.println("   ⚠️ Rede não disponível - possível problema no roteador");
        Serial.println("   💡 Dica: Verifique se o roteador está ligado e funcionando");
    }
    
    return false;
}

/**
 * Handler principal para WiFi desconectado
 * Gerencia tentativas com backoff exponencial
 */
void handleWiFiDisconnection()
{
    wifiReconnectAttempts++;
    Serial.printf("\n🔄 ═══════════════════════════════════════════════════════════\n");
    Serial.printf("🔄 TENTATIVA DE RECONEXÃO #%d/%d\n", wifiReconnectAttempts, WIFI_RECONNECT_MAX_ATTEMPTS);
    Serial.printf("🔄 Delay atual: %d ms | Falhas consecutivas: %d\n", currentReconnectDelay, consecutiveWifiFailures);
    Serial.printf("🔄 ═══════════════════════════════════════════════════════════\n");
    
    // Se já passou do máximo de tentativas, tentar reset total
    if (wifiReconnectAttempts >= WIFI_RECONNECT_MAX_ATTEMPTS) {
        Serial.println("\n⚠️ Máximo de tentativas atingido!");
        Serial.println("🔄 Tentando FORCE RECONNECT...");
        
        if (forceWiFiReconnect()) {
            Serial.println("✅ Force reconnect bem-sucedido!");
            resetWiFiReconnectCounters();
            syncNTP();
            return;
        }
        
        consecutiveWifiFailures++;
        Serial.printf("❌ Force reconnect falhou. Falhas consecutivas: %d/%d\n", 
                     consecutiveWifiFailures, MAX_CONSECUTIVE_FAILURES);
        
        // Se muitas falhas consecutivas, reiniciar ESP32
        if (consecutiveWifiFailures >= MAX_CONSECUTIVE_FAILURES) {
            Serial.println("\n🚨 MUITAS FALHAS CONSECUTIVAS!");
            Serial.println("🔄 Salvando estatísticas e reiniciando ESP32...");
            preferences.putInt("wifi_failures", preferences.getInt("wifi_failures", 0) + 1);
            preferences.putInt("last_fail_reason", WiFi.status());
            delay(2000);
            ESP.restart();
        }
        
        // Resetar tentativas mas manter contagem de falhas
        wifiReconnectAttempts = 0;
        currentReconnectDelay = WIFI_RECONNECT_INITIAL_DELAY;
        return;
    }
    
    // Mostrar diagnóstico
    Serial.println("\n📊 DIAGNÓSTICO WiFi:");
    Serial.printf("   Status: %d\n", WiFi.status());
    Serial.printf("   SSID salvo: %s\n", wm.getWiFiSSID().c_str());
    Serial.printf("   WiFi já conectou antes: %s\n", wifiWasEverConnected ? "SIM" : "NÃO");
    
    // Tentar reconectar
    if (reconnectWiFiSilent()) {
        Serial.println("✅ Reconexão bem-sucedida!");
        resetWiFiReconnectCounters();
        syncNTP();
        return;
    }
    
    // Falhou - aplicar backoff exponencial
    Serial.println("❌ Tentativa falhou");
    
    // Backoff exponencial: dobrar delay até máximo de 2 minutos
    currentReconnectDelay = min(currentReconnectDelay * 2, WIFI_RECONNECT_MAX_DELAY);
    
    Serial.printf("⏳ Próxima tentativa em %d segundos\n", currentReconnectDelay / 1000);
    Serial.printf("📶 Tentativas restantes: %d\n", WIFI_RECONNECT_MAX_ATTEMPTS - wifiReconnectAttempts);
}

/*
 * ═══════════════════════════════════════════════════════════════════════════════════
 * 📋 GUIA COMPLETO DE INSTALAÇÃO E CONFIGURAÇÃO - ThermoWatch ESP32 com WiFiManager
 * ═══════════════════════════════════════════════════════════════════════════════════
 *
 * � MODO BATERIA - DEEP SLEEP (NOVO RECURSO):
 *
 * O ThermoWatch agora possui modo de economia de energia otimizado para bateria:
 *
 * ⚡ FUNCIONAMENTO:
 * - ESP32 acorda a cada 4 horas automaticamente
 * - Faz a leitura de temperatura e umidade
 * - Conecta no WiFi rapidamente (30s timeout)
 * - Envia dados para o Supabase
 * - Volta para deep sleep por mais 4 horas
 * - Consumo durante sono: ~10µA (meses de bateria)
 *
 * 🔧 COMO ATIVAR MODO BATERIA:
 * - O modo bateria é AUTOMÁTICO se WiFi já está configurado
 * - Para configurar WiFi: segure botão BOOT durante boot
 * - Após configurar WiFi, ESP32 entra automaticamente em modo bateria
 *
 * 📊 ESTATÍSTICAS DE BATERIA:
 * - Tempo acordado: ~60 segundos a cada 4 horas
 * - Consumo ativo: ~150mA por 60s = 0.0417mAh por ciclo
 * - Consumo sleep: ~0.01mA por 4h = 0.04mAh por ciclo  
 * - Total: ~0.08mAh por ciclo = ~1.9mAh por dia
 * - Bateria 3000mAh = ~4 anos de funcionamento
 *
 * 🔄 COMO SAIR DO MODO BATERIA:
 * - Segure o botão BOOT durante a inicialização
 * - ESP32 entrará em modo configuração contínua
 * - Use para reconfigurar WiFi ou testar sensor
 *
 * 🔧 BIBLIOTECAS NECESSÁRIAS (instalar via Library Manager do Arduino IDE):
 *
 * 1. WiFiManager by tzapu - versão 2.0.17+
 *    - Gerencia conexão WiFi via portal captivo
 *    - Comando: Sketch > Include Library > Manage Libraries > Pesquisar "WiFiManager"
 *
 * 2. ArduinoJson by Benoit Blanchon - versão 6.21.3+
 *    - Manipulação de dados JSON
 *    - Comando: Sketch > Include Library > Manage Libraries > Pesquisar "ArduinoJson"
 *
 * 3. OneWire by Paul Stoffregen - versão 2.3.8+
 *    - Manter compatibilidade (não usado com PT100 via ADC)
 *    - Comando: Sketch > Include Library > Manage Libraries > Pesquisar "OneWire"
 *
 * 4. DallasTemperature by Miles Burton - versão 3.11.0+
 *    - Manter compatibilidade (não usado com PT100 via ADC)
 *    - Comando: Sketch > Include Library > Manage Libraries > Pesquisar "DallasTemperature"
 *
 * 🔌 CONEXÕES FÍSICAS DO ESP32:
 *
 * PT100 (Sensor de Temperatura via ADC):
 *   - 3.3V → Resistor 10kΩ → Junção
 *   - Junção → PT100 → GND
 *   - Junção → GPIO 4 (ADC)
 *   - Resistor de referência 10kΩ entre VCC e PT100 (OBRIGATÓRIO!)
 *
 * NOTA: O PT100 é um sensor de temperatura apenas (não mede umidade).
 *       A umidade será enviada como 0.0 ao banco de dados.
 *
 * LED Interno:
 *   - GPIO 2 (LED_BUILTIN) - usado para indicações visuais
 *
 * Botão de Configuração:
 *   - GPIO 0 (BOOT button) - pressionar 5s para entrar em modo config
 *
 * 🚀 PRIMEIRO USO - CONFIGURAÇÃO INICIAL:
 *
 * 1. Faça o upload deste código para o ESP32
 * 2. O ESP32 irá criar um Access Point chamado "ThermoWatch_XXXXXX"
 * 3. No seu celular/computador:
 *    - Conecte na rede WiFi "ThermoWatch_XXXXXX"
 *    - Senha: "12345678"
 *    - O navegador abrirá automaticamente a página de configuração
 *    - Se não abrir, acesse: http://192.168.4.1
 *
 * 4. Na página de configuração:
 *    - Configure sua rede WiFi (SSID e senha)
 *    - Nome do Dispositivo: ex. "ESP32 Sala Principal"
 *    - Localização: ex. "Sala de Estar"
 *    - Intervalo de Leitura: 30 segundos (padrão)
 *    - Clique em "Save" para salvar
 *
 * 5. O ESP32 irá reiniciar e conectar na sua rede WiFi
 * 6. Verifique o Serial Monitor (115200 baud) para ver o IP atribuído
 *
 * 🔄 RECONFIGURAÇÃO (quando precisar trocar WiFi):
 *
 * Método 1 - Triplo Reset:
 *   - Pressione o botão RESET do ESP32 3 vezes em 10 segundos
 *   - ESP32 entrará em modo configuração automaticamente
 *
 * Método 2 - Botão BOOT:
 *   - Segure o botão BOOT por 5 segundos durante funcionamento
 *   - ESP32 entrará em modo configuração
 *
 * Método 3 - Reset das configurações:
 *   - No código, descomente a linha: wm.resetSettings();
 *   - Faça upload do código
 *   - Comente a linha novamente e faça upload final
 *
 * 🔍 INDICAÇÕES VISUAIS DO LED:
 *
 * - LED piscando lentamente (2s): Funcionamento normal
 * - 2 piscadas lentas: WiFi conectado com sucesso
 * - 3 piscadas rápidas: Sistema iniciado com sucesso
 * - 2 piscadas rápidas: Dados enviados ao Supabase
 * - 4 piscadas médias: Erro ao enviar dados
 * - 5 piscadas lentas: Erro no sensor PT100
 * - LED fixo ligado: Problema de conexão WiFi
 *
 * 📊 MONITORAMENTO PELO SERIAL MONITOR:
 *
 * Configure o Serial Monitor para 115200 baud para ver:
 * - Status da conexão WiFi
 * - Leituras do sensor em tempo real
 * - Confirmação de envio de dados
 * - Estatísticas do sistema (uptime, memória, sinal)
 * - Mensagens de erro detalhadas
 *
 * 🗄️ ESTRUTURA DO BANCO SUPABASE (use o arquivo supabase_structure.sql):
 *
 * Tabela "devices" - Informações dos dispositivos:
 *   - id (text, PK) - ID único gerado automaticamente
 *   - name (text) - Nome configurado pelo usuário
 *   - description (text) - Descrição automática
 *   - location (text) - Localização configurada
 *   - mac_address (text) - MAC do ESP32
 *   - firmware_version (text) - Versão do firmware
 *   - is_online (boolean) - Status online/offline
 *   - last_seen (timestamptz) - Último heartbeat
 *   - signal_strength (integer) - Força do sinal WiFi
 *   - metadata (jsonb) - Dados técnicos adicionais
 *
 * Tabela "sensor_readings" - Leituras dos sensores:
 *   - id (text, PK) - ID único da leitura
 *   - device_id (text, FK) - Referência ao dispositivo
 *   - device_name (text) - Nome do equipamento
 *   - temperature (real) - Temperatura em Celsius
 *   - humidity (real) - Umidade relativa em %
 *   - timestamp (timestamptz) - Data/hora da leitura
 *   - sensor_type (text) - Tipo do sensor (PT100)
 *   - raw_data (jsonb) - Dados técnicos da leitura
 *
 * 🚨 SOLUÇÃO DE PROBLEMAS:
 *
 * Problema: ESP32 não cria o Access Point
 * Solução: Verifique se a biblioteca WiFiManager está instalada corretamente
 *
 * Problema: Sensor PT100 retorna valores fora da faixa (-50 a 200°C)
 * Solução: Verifique conexões físicas e resistor de 10kΩ no divisor de tensão
 *
 * Problema: Não conecta no Supabase
 * Solução: Verifique URL e API key do Supabase no código
 *
 * Problema: WiFi desconecta frequentemente
 * Solução: Verifique qualidade do sinal WiFi e estabilidade da rede
 *
 * 📈 RECURSOS AVANÇADOS:
 *
 * - Configurações salvas na EEPROM (não perde na falta de energia)
 * - Reconexão automática WiFi em caso de falha
 * - Heartbeat para monitorar dispositivos online
 * - Metadados técnicos para diagnóstico
 * - Timestamps precisos com suporte NTP
 * - Portal de configuração responsivo para mobile
 *
 * 🔧 PERSONALIZAÇÃO:
 *
 * Para alterar configurações padrão, modifique estas variáveis:
 * - MAX31865_MOSI_PIN / MAX31865_MISO_PIN / MAX31865_SCK_PIN / MAX31865_CS_PIN: Pinos SPI do MAX31865
 * - LED_PIN: Pino do LED de status
 * - CONFIG_BUTTON_PIN: Pino do botão de configuração
 * - readingIntervalSec: Intervalo padrão entre leituras
 * - MAX31865_RREF / MAX31865_RNOMINAL / MAX31865_WIRES: Parametrização do PT100 no MAX31865
 *
 * 📞 SUPORTE:
 *
 * Para dúvidas ou problemas:
 * - Verifique o Serial Monitor para mensagens de erro
 * - Teste as conexões físicas do sensor
 * - Confirme se as bibliotecas estão atualizadas
 * - Verifique se o Supabase está acessível
 *
 * ═══════════════════════════════════════════════════════════════════════════════════
 * 🌡️ ThermoWatch ESP32 v2.0 - Sistema de Monitoramento Inteligente com WiFiManager
 * ═══════════════════════════════════════════════════════════════════════════════════
 */
