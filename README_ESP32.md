# 🌡️ ThermoWatch ESP32 - DHT11 Temperature Monitor

## 📋 Visão Geral
Sistema de monitoramento de temperatura e umidade usando ESP32 + DHT11 com deep sleep para economia de bateria e integração com Supabase.

## 🎯 Características Principais
- **Sensor DHT11** - Leituras precisas de temperatura e umidade
- **WiFiManager** - Configuração fácil via portal web
- **Deep Sleep** - Até 4 anos de bateria (4 horas de sono)
- **Supabase Integration** - Dados salvos na nuvem
- **Debug Mode** - Sistema completo de diagnóstico por LED
- **Battery Monitor** - Monitoramento de tensão e percentual

## 🔧 Hardware
- **ESP32 DevKit V1**
- **DHT11** sensor (GPIO 4)
- **Resistor 10kΩ** (pull-up DHT11)
- **LED** built-in (GPIO 2)
- **Botão BOOT** (GPIO 0)
- **Monitor bateria** (GPIO 34)

## 📱 Device ID Atual
```
ESP32_A45888
```

## 🚀 Quick Start
```bash
# Instalar dependências
pio lib install

# Compilar
pio run

# Upload
pio run --target upload

# Monitor
pio device monitor --port COM4 --baud 115200
```

## 📊 API Endpoints
- **Sensor Readings:** `POST /rest/v1/sensor_readings`
- **Device Registration:** `POST /rest/v1/devices`
- **Heartbeat:** `PATCH /rest/v1/devices`

## 🌐 WiFi Configuration
- **AP Name:** ThermoWatch_XXXXXX
- **Password:** 12345678
- **Portal:** http://192.168.4.1

## 🔋 Power Management
- **Config Mode:** Operação contínua (para configuração)
- **Battery Mode:** Deep sleep 4h + wake 60s
- **Debug Mode:** Sleep desabilitado para desenvolvimento

## 📈 Dados Enviados
```json
{
  "device_id": "ESP32_A45888",
  "device_name": "Sensor bl17",
  "temperature": 26.0,
  "humidity": 16.0,
  "sensor_type": "DHT11",
  "raw_data": {
    "wifi_rssi": -37,
    "battery_voltage": 3.7,
    "battery_percentage": 85.0,
    "debug_mode": false
  }
}
```

## 🚨 LED Diagnostic Codes
- **1 piscada LONGA** = ✅ Sucesso total
- **2 piscadas RÁPIDAS** = ✅ DHT11 OK  
- **3 piscadas MÉDIAS** = ❌ Erro WiFi
- **4 piscadas MÉDIAS** = ❌ Sem internet
- **5 piscadas LENTAS** = ❌ Erro DHT11
- **6 piscadas RÁPIDAS** = ❌ Erro Supabase

## 📅 Última Atualização
- **Data:** 12/11/2025
- **Status:** DHT11 funcionando perfeitamente
- **Versão:** v2.0 - Debug optimized