# ThermoWatch ESP32 - PlatformIO

## 📦 Dependências Instaladas

Este projeto usa PlatformIO com as seguintes dependências configuradas:

### 🔧 **Bibliotecas Principais:**
- **WiFiManager** v2.0.17+ - Portal captivo para configuração WiFi
- **ArduinoJson** v6.21.3+ - Manipulação de dados JSON
- **DHT sensor library** v1.4.4+ - Leitura do sensor DHT22
- **Adafruit Unified Sensor** v1.1.9+ - Dependência da biblioteca DHT

### 📋 **Bibliotecas Incluídas no Framework:**
- **WiFi** - Conectividade WiFi (built-in ESP32)
- **HTTPClient** - Requisições HTTP (built-in ESP32)
- **Preferences** - Armazenamento EEPROM (built-in ESP32)

## 🚀 **Como Compilar:**

### **Método 1 - Via VS Code (Recomendado):**
1. Abra o VS Code
2. Instale a extensão "PlatformIO IDE"
3. Abra esta pasta do projeto ESP32
4. Clique no ícone do PlatformIO na barra lateral
5. Clique em "Build" ou "Upload"

### **Método 2 - Via Terminal:**
```bash
# Instalar dependências
pio lib install

# Compilar
pio run

# Upload para ESP32
pio run --target upload

# Monitor serial
pio device monitor
```

## 🔌 **Configuração de Hardware:**

```
DHT22 Sensor:
- VCC     → 3.3V (ESP32)
- GND     → GND (ESP32) 
- DATA    → GPIO 4 (ESP32)

LED Status:
- GPIO 2 (LED built-in do ESP32)

Botão Config:
- GPIO 0 (Botão BOOT do ESP32)

Bateria (opcional):
- GPIO 34 (ADC1_CH6)
```

## 📊 **Monitor Serial:**
- Baud Rate: 115200
- Use o monitor do PlatformIO: `pio device monitor`

## ⚠️ **Configuração Inicial:**
1. **Primeira conexão:** ESP32 cria rede `ThermoWatch_XXXXXX`
2. **Senha:** `thermowatch123`
3. **Portal:** http://192.168.4.1
4. **Reconfigurar:** 3x reset em 10s ou botão BOOT por 5s

## 🔋 **Modo Bateria:**
- Ativação: Automática após configurar WiFi
- Sleep: 4 horas entre leituras  
- Consumo: ~10µA dormindo
- Duração: ~4 anos com bateria 3000mAh