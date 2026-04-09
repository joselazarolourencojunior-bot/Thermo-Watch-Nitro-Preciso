# 🛠️ INSTALAÇÃO COMPLETA - ESP32 no Visual Studio Code

## ⚡ **PASSO 1: Instalar Python**

1. **Baixe Python:** <https://www.python.org/downloads/>
2. **Durante a instalação:**
   - ✅ Marque "Add Python to PATH"
   - ✅ Marque "Install for all users"
3. **Verifique:** Abra um novo terminal e digite `python --version`

## 🔌 **PASSO 2: Instalar PlatformIO CLI**

```powershell
# Instalar PlatformIO via pip
pip install platformio

# Verificar instalação
pio --version
```

## 📦 **PASSO 3: Instalar Dependências do Projeto**

```powershell
# Navegue para o diretório do projeto
cd "c:\Users\Lenovo\Desktop\Lazaro 18-09-25\PROJETOS\Projetos_Flutter\Medidor_Temperatura\esp32"

# Instalar dependências automaticamente
pio lib install

# Compilar o projeto
pio run

# Upload para ESP32 (conecte via USB)
pio run --target upload

# Monitor serial (para debug)
pio device monitor
```

## 🎯 **PASSO 4: Configurar VS Code**

### **4.1 Extensões Necessárias:**

- ✅ PlatformIO IDE (já instalada)
- ✅ C/C++ (Microsoft)
- ✅ ESP-IDF (opcional, para recursos avançados)

### **4.2 Abrir Projeto:**

1. Abra VS Code
2. File > Open Folder
3. Selecione a pasta `esp32`
4. PlatformIO detectará automaticamente o projeto

### **4.3 Interface PlatformIO:**

- **Ícone PlatformIO:** Barra lateral esquerda (formiga azul)
- **Build:** Clique em "Build" na barra inferior
- **Upload:** Clique em "Upload" na barra inferior
- **Serial Monitor:** Clique no ícone de plug na barra inferior

## 🔧 **PASSO 5: Configurações Específicas**

### **platformio.ini** (já criado)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
upload_speed = 921600
monitor_speed = 115200

lib_deps = 
    tzapu/WiFiManager@^2.0.17
    bblanchon/ArduinoJson@^6.21.3
    adafruit/DHT sensor library@^1.4.4
    adafruit/Adafruit Unified Sensor@^1.1.9
```

## 📋 **COMANDOS ÚTEIS**

```powershell
# Listar bibliotecas instaladas
pio lib list

# Atualizar bibliotecas
pio lib update

# Limpar build
pio run --target clean

# Build específico para ESP32
pio run --environment esp32dev

# Monitor com filtros de debug
pio device monitor --filter esp32_exception_decoder
```

---

## 📦 **OPÇÃO 2: Uso Direto da Extensão VS Code (Mais Simples)**

Se preferir, você pode usar apenas a extensão PlatformIO no VS Code:

### **2.1 Após instalar Python:**

1. Abra VS Code na pasta `esp32`
2. A extensão PlatformIO detectará o `platformio.ini`
3. Clique em "Install" quando aparecer notificação
4. Use os botões na barra inferior:
   - ✅ **Build** - Compilar
   - 🔄 **Upload** - Enviar para ESP32
   - 🔌 **Serial Monitor** - Debug

### **2.2 Verificar Instalação:**

- Abra o PlatformIO Home (Ctrl+Shift+P > "PlatformIO: Home")
- Vá em "Libraries" para ver as dependências instaladas
- Em "Devices" para ver ESP32 conectado

---

## 🚨 **SOLUÇÃO DE PROBLEMAS**

### **Erro: "Python não encontrado"**

1. Instale Python: <https://www.python.org/downloads/>
2. Reinicie VS Code e terminal
3. Verifique: `python --version`

### **Erro: "ESP32 não detectado"**

1. Instale driver CH340 ou CP2102
2. Verifique porta COM no Device Manager
3. Use cabo USB com dados (não só carregamento)

### **Erro: "Permission denied"**

1. Execute VS Code como Administrador
2. Ou configure permissões da porta serial

### **Erro de compilação:**

1. Limpe o projeto: `pio run --target clean`
2. Delete pasta `.pio` e recompile
3. Verifique versões das bibliotecas

---

## ✅ **RESULTADO ESPERADO**

Após seguir estes passos, você terá:

- ✅ Python instalado e funcionando
- ✅ PlatformIO CLI instalado
- ✅ Todas as bibliotecas ESP32 instaladas
- ✅ Projeto compilando sem erros
- ✅ Capacidade de fazer upload para ESP32
- ✅ Monitor serial funcionando para debug

**Próximo passo:** Conectar ESP32 via USB e fazer upload do código!
