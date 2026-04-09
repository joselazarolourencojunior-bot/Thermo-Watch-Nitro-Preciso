# Instruções Copilot - Projeto ThermoWatch ESP32 PT100

## Visão Geral do Projeto
Sistema de monitoramento de temperatura otimizado para bateria usando **ESP32 + sensor PT100** via ADC. Opera em ciclos de deep sleep (4 horas) para vida útil de ~4 anos. Configuração WiFi via portal captivo WiFiManager.

## Arquitetura Principal
- **Código único**: [../src/main.cpp](../src/main.cpp) - ~2500 linhas contendo toda lógica (WiFiManager, leitura PT100, integração Supabase, deep sleep)
- **Build**: [../platformio.ini](../platformio.ini) - Target ESP32, dependências, flags de compilação
- **Sensor**: PT100 via divisor de tensão (R_REF 1kΩ) lido por ADC - **não usa DHT22**

## Comandos Essenciais PlatformIO
```bash
# Instalar dependências e compilar
pio lib install && pio run

# Upload para ESP32
pio run --target upload

# Monitor serial (115200 baud)
pio device monitor

# Limpar e recompilar
pio run --target clean && pio run
```

## Hardware - Configuração Crítica
```cpp
#define PT100_ADC_PIN 4      // GPIO4 (ADC2) - Leitura PT100 via ADC
#define LED_PIN 2            // LED built-in (feedback visual)
#define CONFIG_BUTTON_PIN 0  // Botão BOOT (entra em modo config)
#define BATTERY_PIN 34       // ADC1_CH6 (monitoramento bateria ANTES do regulador)
#define R_REF 1000.0         // Resistor referência 1kΩ (melhor resolução ADC)
#define ADC_VREF 3.08        // Tensão referência ADC (medida: alimentação 3.08V real)
#define TEMP_OFFSET 42.0     // Offset calibração temperatura (°C)
```

**Arquitetura de Alimentação**:
```
Bateria Li-Ion 3.74V → KF33 (reg. 3.3V) → ESP32 @ 3.08V medido
                 ↓
              GPIO34 (monitora bateria ANTES do regulador)
```

**Circuito obrigatório PT100**:
```
3.08V (VDD ESP32) ----[PT100 ~100Ω]----+----[R_REF 1kΩ]---- GND
                                       |
                                    GPIO 4 (ADC2)
```

**⚠️ CRÍTICO**: 
- GPIO4 é ADC2 - **DEVE SER LIDO ANTES DE LIGAR WIFI!** 
- ADC_VREF = 3.1V (saída real do regulador KF33, não 3.3V nominal)
- R_REF = 1kΩ para melhor resolução ADC (~0.8°C/step vs 3.8°C/step com 7.6kΩ)
- PT100 conectado em VDD, resistor em GND (GPIO4 lê ~2.8V a 25°C)
- Bateria monitorada em GPIO34 ANTES do regulador (lê 3.74V direto)
- Sequência correta: ler PT100 → ler bateria → conectar WiFi → enviar dados

## Dois Modos de Operação Distintos

### 1. Modo Configuração (Contínuo)
- **Trigger**: WiFi não configurado OU botão BOOT pressionado durante boot
- **Comportamento**: `loop()` executa continuamente, LED piscando
- **Detecção**: `isConfigMode()` verifica flags EEPROM e status WiFi
- **Uso**: Primeira configuração, testes, debugging

### 2. Modo Bateria (Deep Sleep)
- **Trigger**: Automático após WiFi configurado E não pressionou BOOT
- **Ciclo**: Acorda → lê sensor → conecta WiFi → envia dados → dorme 4h
- **Consumo**: ~10µA dormindo, ~150mA por 60s acordado
- **Função chave**: `performQuickReading()` executa ciclo completo sem entrar em `loop()`

**⚠️ CRÍTICO**: Em modo bateria, `loop()` nunca é executado - tudo acontece em `setup()` via `performQuickReading()` → `enterDeepSleep()`

## Padrões de Reconexão WiFi Robusta
Sistema multi-camadas contra desconexões (linhas 1900-2300):

```cpp
// Estratégias tentadas em sequência:
1. reconnectWiFiSilent()     // Usa credenciais salvas NVS
2. WiFi.begin()              // Conexão direta ESP32
3. scanForSavedNetwork()     // Verifica se rede existe
4. forceWiFiReconnect()      // Reset módulo + retry
```

**Backoff exponencial**: Delay inicial 5s → máximo 2min  
**Watchdog**: 5min sem WiFi = `ESP.restart()`  
**Persistência**: Até 20 tentativas antes de reset total

## Leitura PT100 - Implementação ADC
```cpp
// Fórmula divisor de tensão com PT100 em VDD (R_REF = 1kΩ)
float readPT100Resistance() {
    voltage = (avgADC / 4095.0) * 3.1;  // Tensão no GPIO4
    R_PT100 = R_REF * (3.1 - voltage) / voltage;  // PT100 em cima, resistor embaixo
}

// Equação Callendar-Van Dusen simplificada
float pt100ToTemperature(float resistance) {
    T = (R - 100.0) / (100.0 * 0.00385);  // Linear -50°C a +150°C
}
```

**Resolução ADC**: Com 1kΩ, range ADC ~253 steps (PT100 0-200°C) = **0.8°C/step**  
**Limitações conhecidas**: Precisão ±2-3°C (ADC + linearização simples). MAX31865 recomendado para precisão industrial.

## Integração Supabase - Fluxo de Dados
```cpp
// Credenciais fixas no código (linhas 105-106)
const char *supabaseUrl = "https://qanyszslnactgtzpmtyj.supabase.co";
const char *supabaseKey = "eyJhbGc...";

// Sequência de comunicação:
1. registerDevice()           // POST /rest/v1/devices (primeira vez)
2. sendSensorData()          // POST /rest/v1/sensor_readings (sempre)
3. checkBatteryAlert()       // POST /rest/v1/alerts (se bateria baixa)
// Heartbeat desabilitado para economia (linha 1825)
```

**Headers obrigatórios**: `apikey`, `Authorization: Bearer`, `Content-Type: application/json`

## Portal WiFiManager - Configuração
```cpp
// Parâmetros customizados (linhas 185-187)
WiFiManagerParameter custom_device_name("device_name", "Nome do Dispositivo", "Thermo Watch - Nitro", 40);
WiFiManagerParameter custom_device_location("device_location", "Localização", "Sala Principal", 40);
WiFiManagerParameter custom_reading_interval("reading_interval", "Intervalo Leitura (min)", "60", 4);
```

**Portal**: http://192.168.4.1 quando AP ativo  
**AP name**: "ThermoWatch_v2.0_WiFiManager" / senha "12345678"  
**Reset forçado**: 3x RESET em 10s OU BOOT 5s durante inicialização

## Monitoramento de Bateria
```cpp
// Calibração (linhas 176-180)
const float BATTERY_MAX_VOLTAGE = 3.7;        // 100%bateria
```

**Circuito**: GPIO34 monitora bateria 3.74V **ANTES** do regulador KF33 via divisor 10kΩ+1kΩ (fatorco
const float VOLTAGE_DIVIDER_RATIO = 15.2;     // Calibrado: 3.74V real / 2.71V lido
```

**Circuito divisor bateria**: 10kΩ + 1kΩ (fator teórico 11x, calibrado 15.2x)  
**Multi-sample**: 10 leituras ADC com média para estabilidade  
**Alertas**: Cria registros em `alerts` table quando baixa/crítica  
**Smart resolution**: Resolve alertas antigos antes de criar novos

## Padrões de Feedback LED
```cpp
// Implementação via hardware timer (linhas 121-125)
ledConfigMode()      // Piscando contínuo (modo configuração)
ledSuccess()         // 2s ligado fixo (operação OK)
ledFailure()         // 2 piscadas rápidas (erro)
blinkLED(n, delay)   // N piscadas com delay customizado

// Códigos específicos sensor PT100:
// 5 piscadas LENTAS (800ms) = Erro PT100
// 2 piscadas RÁPIDAS (200ms) = PT100 OK
// 3 piscadas MÉDIAS (600ms) = WiFi falhou
```

## Debugging - Monitor Serial 115200
```bash
# Padrões importantes nas mensagens:
"💤 ACORDOU DE DEEP SLEEP"           # Wakeup do modo bateria
"🔋 Status da Bateria: X.XXV (XX%)"  # Voltage + percentual
"✅ PT100 FUNCIONANDO: XX.X°C"       # Leitura sensor válida
"📤 JSON enviado: {...}"              # Payload Supabase
"🔄 TENTATIVA #X/5"                   # Retry loops
"⚠️ WATCHDOG: 5 minutos sem WiFi"    # Antes de restart

# Flags de erro:
"❌ === ERRO PT100 DETECTADO ==="
"⚠️ Leitura PT100 fora da faixa normal" # < -50 ou > 200°C
```

## Modificações Comuns

### Mudar intervalo sleep
```cpp
// Linha ~165: readingIntervalSec configurável 60-59940s (1-999 min)
// Calculado no enterDeepSleep(): esp_sleep_enable_timer_wakeup()
```

### Trocar sensor para MAX31865
1. Adicionar biblioteca Adafruit_MAX31865 em `platformio.ini`
2. Substituir `readPT100Temperature()` por API MAX31865
3. Remover divisor de tensão e conectar via SPI

### Adicionar novo alerta customizado
Seguir padrão `checkBatteryAlert()` (linhas 1395-1480):
1. Resolver alertas antigos: `PATCH /alerts?...&is_resolved=eq.false`
2. Criar novo: `POST /alerts` com `device_id`, `alert_type`, `severity`, `value`

## Debugging Avançado
```bash
# Verificar wakeup cause
showWakeupReason()  # Diferencia timer vs BOOT button vs primeiro boot

# Estatísticas acumuladas EEPROM
preferences.getInt("reset_counter")      # Conta resets manuais
preferences.getInt("wifi_failures")      # Histórico falhas WiFi
preferences.getInt("last_fail_reason")   # WiFi.status() última falha
```