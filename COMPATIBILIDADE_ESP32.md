# Compatibilidade de Hardware - ThermoWatch ESP32

## ⚠️ PROBLEMA IDENTIFICADO: ESP32 Single-Core Incompatível

### Situação Atual

Durante os testes de produção, foram identificados **dois tipos diferentes de ESP32**:

1. **ESP32 #1** (MAC: `f8:b3:b7:a4:58:88`)
   - Chip: ESP32-D0WDQ6 revision v1.1
   - Características: **Dual Core** (2 núcleos), WiFi, BT, 240MHz
   - Status: ✅ **FUNCIONA PERFEITAMENTE**

2. **ESP32 #2** (MAC: `7c:c2:94:e8:17:72`)
   - Chip: unknown ESP32 revision v1.0
   - Características: **Single Core** (1 núcleo apenas), WiFi, BT, 240MHz
   - Status: ❌ **INCOMPATÍVEL COM FIRMWARE ATUAL**

### Motivo da Incompatibilidade

O **Arduino-ESP32 framework moderno** (versão 3.x) foi compilado com suporte **obrigatório para dual-core (FreeRTOS SMP)**. Quando o firmware é carregado no ESP32 single-core, o sistema falha durante o boot com o erro:

Entendi! Após a configuração do WiFi ser salva, o ESP32 fica aguardando 3 segundos antes de resetar.
E (454) cpu_start: Running on single core variant of a chip, but app is built with multi-core support.
E (454) cpu_start: Check that CONFIG_FREERTOS_UNICORE is enabled in menuconfig
```

Este é um **chip ESP32 de primeira geração single-core extremamente raro**. A maioria dos ESP32 no mercado são dual-core.

### Soluções Possíveis

#### ✅ Solução 1: Usar Apenas ESP32 Dual-Core (RECOMENDADO)

**Ação:** Substituir o ESP32 single-core por um ESP32 dual-core padrão.

**Prós:**
- Funciona imediatamente sem modificações
- ESP32 dual-core é o padrão do mercado (>95% dos chips)
- Melhor performance e suporte

**Contras:**
- Custo adicional de ~R$ 15-30 por placa

**Status:** ✅ **RECOMENDADO** - Esta é a solução mais prática

---

#### ⚠️ Solução 2: Downgrade do Framework Arduino-ESP32

**Ação:** Modificar `platformio.ini` para usar versão antiga do framework:

```ini
[env:esp32dev]
platform = espressif32@5.4.0  ; Versão antiga com suporte single-core
board = esp32dev
framework = arduino
```

**Prós:**
- Pode funcionar com o chip single-core atual
- Sem custo adicional de hardware

**Contras:**
- Perde recursos e correções de segurança recentes
- Bibliotecas podem ter incompatibilidades
- Suporte limitado da comunidade
- Pode introduzir novos bugs

**Status:** ⚠️ **NÃO TESTADO** - Risco médio/alto

---

#### ❌ Solução 3: Migrar para ESP-IDF Puro

**Ação:** Reescrever todo o projeto usando ESP-IDF nativo com `CONFIG_FREERTOS_UNICORE=1`.

**Prós:**
- Controle total sobre configuração single-core/dual-core
- Máximo desempenho possível

**Contras:**
- Requer reescrever **100% do código** (1880 linhas)
- WiFiManager precisa ser refeito do zero
- Curva de aprendizado muito alta
- Tempo estimado: 2-3 semanas de desenvolvimento

**Status:** ❌ **NÃO RECOMENDADO** - Esforço não justificado

---

## 📊 Recomendação Final

### 🎯 USE APENAS ESP32 DUAL-CORE

O ESP32 dual-core é o chip padrão e amplamente disponível. O firmware atual foi testado e validado nele com sucesso.

### 🛒 Onde Comprar ESP32 Dual-Core

Procure por:
- **ESP32-WROOM-32** (módulo mais comum)
- **ESP32 DevKit v1** (placa de desenvolvimento)
- **NodeMCU-32S**
- Verifique especificações: "Dual Core" ou "240MHz 2-core"

### ✅ Validação Antes da Compra

Ao testar um novo ESP32, use o comando:
```bash
pio run --target upload
```

Verifique a saída do upload. Deve mostrar:
```
Chip is ESP32-D0WDQ6 (revision vX.X)
Features: WiFi, BT, Dual Core, 240MHz
```

Se aparecer "Single Core", **NÃO use este chip**.

---

## 📝 Notas Técnicas

### Identificação do Chip

Para ver o tipo de chip conectado, execute:
```bash
pio run --target upload --verbose
```

Procure por:
- `Features: WiFi, BT, Dual Core` ✅ (compatível)
- `Features: WiFi, BT, Single Core` ❌ (incompatível)

### MAC Address dos Chips Testados

| MAC Address | Tipo | Status |
|-------------|------|--------|
| f8:b3:b7:a4:58:88 | Dual Core v1.1 | ✅ Funciona |
| 7c:c2:94:e8:17:72 | Single Core v1.0 | ❌ Incompatível |

### Código Atual

O firmware atual (`main.cpp`) está **100% funcional** em ESP32 dual-core e inclui todas as otimizações:

- ✅ Deep sleep com intervalos configuráveis (5min - 16h)
- ✅ WiFiManager para configuração via portal captivo
- ✅ Integração com Supabase (sensor_readings, devices, alerts)
- ✅ Monitoramento de bateria com alertas
- ✅ Reset counter que ignora wake from deep sleep
- ✅ Retry logic para posts de dados
- ✅ Heartbeat com metadata de sistema

**Não há necessidade de modificar o código** - apenas use hardware compatível.

---

## 🔧 Troubleshooting

### Problema: "Running on single core variant of a chip"

**Solução:** Troque o ESP32 por um dual-core.

### Problema: Upload funciona mas o dispositivo reinicia infinitamente

**Solução:** Verifique se o chip é single-core. Se sim, troque-o.

### Problema: Como identificar se meu ESP32 é dual-core antes de comprar?

**Solução:** 
1. Pergunte ao vendedor explicitamente: "Este ESP32 é dual-core?"
2. Verifique datasheet do módulo (ESP32-WROOM-32 = dual-core)
3. ESP32-SOLO-1 = single-core (EVITE)
4. ESP32-S2/S3/C3 = single-core (são modelos diferentes, não compatíveis)

---

**Última atualização:** 17/11/2025  
**Firmware version:** Production ready (commit e3181a2)
