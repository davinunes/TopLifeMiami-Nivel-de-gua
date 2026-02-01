/*
  SKETCH PORTADO PARA ESP32-C3 COM DISPLAY OLED 0.42"
  - Biblioteca de display alterada para U8g2.
  - Mantém todas as funcionalidades originais.

  Bibliotecas necessárias (instalar via Gerenciador de Bibliotecas do Arduino IDE):
  - "U8g2" by oliver
  - "Ultrasonic" by Erick Simões
  - "ArduinoJson" by Benoit Blanchon
*/

// ==================== BIBLIOTECAS ====================
#include <WiFi.h>
#include "esp_wifi.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <Ultrasonic.h>
#include <Wire.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Update.h>
#include "mbedtls/base64.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ==================== CONFIGURAÇÕES DA PLACA E FIRMWARE ====================
#define BOARD_MODEL "ESP32-C3-OLED-042"
#define FW_VERSION 3.0

// ==================== CONFIGURAÇÕES DE PINOS (ESP32-C3) ====================
#define LED_PIN 8
#define TRIGGER_PIN 20  // GPIO20 como trigger
#define ECHO_PIN 21     // GPIO21 como echo
#define I2C_SDA_PIN 5
#define I2C_SCL_PIN 6

// ==================== CONFIGURAÇÕES U8G2 ====================
// Construtor para o display SSD1306 de 72x40 pixels em modo I2C de hardware.
// U8G2_R0 = Sem rotação. Se a tela ficar de cabeça para baixo, troque para U8G2_R2.
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);


// ==================== CONFIGURAÇÕES GERAIS ====================
#define AP_MODE_DURATION 120000

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600;
const int   daylightOffset_sec = 0;

int idSensor = 3;
int distanciaSonda = 0;
int alturaAgua = 200;
String nomeDaSonda = "Torre A Reservatório 01";
String novoUrlSite = "https://app.digitalinovation.com.br";
String urlVersao = "https://app.digitalinovation.com.br/version.json";
String remoteConfigUrl = "https://app.digitalinovation.com.br/remote.json";
String pingBaseUrl = "https://app.digitalinovation.com.br/ping/";
int alertLevelCm = 50;
String deviceUuid = "";

// ==================== VARIÁVEIS GLOBAIS ====================
// ==================== CLASSE DE LOG (CIRCULAR BUFFER) ====================
#define LOG_SIZE 2048
class DebugLog {
  private:
    char buffer[LOG_SIZE];
    int head;
    bool wrapped;
    SemaphoreHandle_t mutex;
  
  public:
    DebugLog() : head(0), wrapped(false) {
        mutex = xSemaphoreCreateMutex();
        memset(buffer, 0, LOG_SIZE);
    }

    void log(String msg) {
        if (mutex != NULL) xSemaphoreTake(mutex, portMAX_DELAY);
        
        String timeStr = "[" + String(millis()) + "] ";
        String fullMsg = timeStr + msg + "\n";
        
        // Serial output imediato
        Serial.print(fullMsg);
        
        // Circular Buffer
        for(int i=0; i < fullMsg.length(); i++) {
            buffer[head] = fullMsg[i];
            head++;
            if(head >= LOG_SIZE) {
                head = 0;
                wrapped = true;
            }
        }
        
        if (mutex != NULL) xSemaphoreGive(mutex);
    }
    
    String getLogString() {
        if (mutex != NULL) xSemaphoreTake(mutex, portMAX_DELAY);
        String s = "";
        s.reserve(LOG_SIZE);
        
        if (wrapped) {
            for(int i=head; i < LOG_SIZE; i++) s += buffer[i];
            for(int i=0; i < head; i++) s += buffer[i];
        } else {
            for(int i=0; i < head; i++) s += buffer[i];
        }
        
        if (mutex != NULL) xSemaphoreGive(mutex);
        return s;
    }
    
    void clear() {
        if (mutex != NULL) xSemaphoreTake(mutex, portMAX_DELAY);
        head = 0;
        wrapped = false;
        memset(buffer, 0, LOG_SIZE);
        if (mutex != NULL) xSemaphoreGive(mutex);
    }

    String toHtml() {
      if (mutex != NULL) xSemaphoreTake(mutex, portMAX_DELAY);
      String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>";
      html += "<style>body{background:#1e1e1e;color:#d4d4d4;font-family:monospace;padding:20px;}";
      html += ".log{white-space:pre-wrap;word-wrap:break-word;} .time{color:#569cd6;} .err{color:#f44747;}</style></head><body>";
      html += "<h2>Debug Log (Auto Refresh 5s)</h2><form action='/debug/clear' method='POST'><button>Limpar Log</button></form><hr><div class='log'>";
      
      String content = "";
      content.reserve(LOG_SIZE);
      if (wrapped) {
          for(int i=head; i < LOG_SIZE; i++) content += buffer[i];
          for(int i=0; i < head; i++) content += buffer[i];
      } else {
          for(int i=0; i < head; i++) content += buffer[i];
      }
      
      // Escape basico e formatacao
      content.replace("<", "&lt;");
      content.replace(">", "&gt;");
      content.replace("\n", "<br>");
      
      html += content;
      html += "</div></body></html>";
      
      if (mutex != NULL) xSemaphoreGive(mutex);
      return html;
    }
};

DebugLog debugLog;

// ==================== VARIÁVEIS GLOBAIS ====================
Ultrasonic sonar1(TRIGGER_PIN, ECHO_PIN, 40000UL); // Timeout aumentado
int distance = 0;

// Urls atualizadas (REMOVED DUPLICATE)

// Mutexes para Thread Safety
SemaphoreHandle_t logMutex;
SemaphoreHandle_t configMutex;
SemaphoreHandle_t sensorMutex;
TaskHandle_t iotTaskHandle;

// Historico para moda
#define HISTORY_SIZE 120
int history[HISTORY_SIZE];
int historyCount = 0;
int historyIndex = 0;

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
String StatusInternet = "Sem Wifi...";

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 500;
unsigned long lastTime = 0;
unsigned long timerDelay = 15000;
unsigned long bootTime = 0;
bool inAPMode = false;

unsigned long lastLedToggle = 0;
bool ledState = false;
unsigned long ledInterval = 1000;

struct Config {
  String ssid;
  String pass;
  String sensorId;
  String nomeSonda;
};

// ==================== PROTÓTIPOS DE FUNÇÕES ====================
void switchToAPMode();
void switchToStationMode();
void updateLed();
void handleWiFiEvent(WiFiEvent_t event);
void sonar();
void tela();
void IoT();
Config loadConfig();
void saveConfig(String ssid, String pass, String sensorId, String nomeSonda);
void startAccessPoint();
void setupWebServer();
void handleRoot();
void handleSave();
void performUpdate(String url);
void getRemoteConfig();
void sendPing();
void publishSensorReading(int sensorValue);
void handleEsquema();
void handleWiFiScan();
void handleNotFound();
String urlEncode(const String& str);
void setupDeviceID();
void syncTime();
String escapeHTML(String input);

// Forward Declarations
void initRemoteParams();
void saveRemoteParams();
void handleDebug();
void handleDebugClear();

// ==================== SETUP ====================
// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  
  // Inicializa Mutexes
  logMutex = xSemaphoreCreateMutex();
  configMutex = xSemaphoreCreateMutex();
  sensorMutex = xSemaphoreCreateMutex();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_STA);
  // WiFi.enableIPv6();  // Descomentar se necessario e suportado pela versao do core C3
  
  setupDeviceID();

  // Inicializa o I2C e o display com U8g2
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  u8g2.begin();

  // Mensagem inicial com U8g2
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf); // Seleciona uma fonte pequena
  u8g2.drawStr(0, 10, "Iniciando...");
  u8g2.sendBuffer(); // Envia para a tela
  delay(1000);

  Config cfg = loadConfig();
  idSensor = cfg.sensorId.toInt();
  nomeDaSonda = cfg.nomeSonda;

  WiFi.onEvent(handleWiFiEvent);

  bootTime = millis();
  
  // Inicializa em modo AP para configuracao
  switchToAPMode();
  
  initRemoteParams(); // Carrega parametros salvos
  setupWebServer();
  debugLog.log("Setup completo. Iniciando Tasks...");

  // Inicializa Task IoT no Core 0 (Unico no C3)
  xTaskCreatePinnedToCore(
      IoT_Task,       // Funcao da tarefa
      "IoT_Task",     // Nome
      16384,          // Stack Size (16KB)
      NULL,           // Params
      1,              // Prioridade
      &iotTaskHandle, // Handle
      0               // Core ID (0)
  );
}

// ==================== LOOP PRINCIPAL ====================
// ==================== LOOP PRINCIPAL ====================
void loop() {
  // Leitura do Sonar (Agora seguro por mutex)
  if (!inAPMode) {
    sonar();
  }

  // Atualizacao do Display (Mantendo logica original)
  if (millis() - lastDisplayUpdate >= displayInterval) {
    tela();
    lastDisplayUpdate = millis();
  }

  // Timeout do modo AP
  if (inAPMode && (millis() - bootTime > AP_MODE_DURATION)) {
    switchToStationMode();
  }

  updateLed();
  server.handleClient(); // WebServer roda no Loop principal

  if (inAPMode) {
    dnsServer.processNextRequest();
  }
  
  // Yield para dar chance ao WiFi Stack (Crucial no Single Core)
  delay(2); 
}

// ==================== FUNÇÃO DE TELA (com U8g2) ====================
void tela() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  if (inAPMode) {
    // --- TELA PARA MODO ACCESS POINT (SETUP) ---
    // Linha 1: Tempo restante para modo estação
    unsigned long timeElapsed = millis() - bootTime;
    long timeLeftMs = AP_MODE_DURATION - timeElapsed;
    if (timeLeftMs < 0) timeLeftMs = 0;
    int minutes = (timeLeftMs / 1000) / 60;
    int seconds = (timeLeftMs / 1000) % 60;
    char timeStr[20];
    sprintf(timeStr, "Tempo: %02d:%02d", minutes, seconds);
    u8g2.drawStr(0, 10, timeStr);

    // Linha 2: Nome da rede AP
    u8g2.drawStr(0, 22, WiFi.softAPSSID().c_str());

    // Linha 3: IP do AP
    u8g2.drawStr(0, 34, WiFi.softAPIP().toString().c_str());

  } else {
    // --- TELA PARA MODO ESTAÇÃO (NORMAL) ---
    // Linha 1: Nível da água
    char buffer[20];
    sprintf(buffer, "Nivel: %dcm", distance);
    u8g2.drawStr(0, 10, buffer);

    // Linha 2: Status do WiFi
    const char* wifiStatus = (WiFi.status() == WL_CONNECTED) ? "WiFi: UP" : "WiFi: DOWN";
    u8g2.drawStr(0, 22, wifiStatus);

    // Linha 3: IP ou mensagem de conexão
    if (WiFi.status() == WL_CONNECTED) {
      u8g2.drawStr(0, 34, WiFi.localIP().toString().c_str());
    } else {
      u8g2.drawStr(0, 34, "Conectando...");
    }
  }
  u8g2.sendBuffer();
}


// ==================== FUNÇÕES DE IOT E REDE (sem alterações na lógica) ====================

// ==================== HELPER FUNCTIONS ====================
// Calcula a moda das ultimas leituras para estabilidade
int calculateMode() {
    if (historyCount == 0) return 0;
    
    // Copia para ordenar
    int sorted[HISTORY_SIZE];
    int count = historyCount;
    
    // Acesso thread-safe ao historico
    if (xSemaphoreTake(sensorMutex, portMAX_DELAY)) {
         for(int i=0; i<count; i++) sorted[i] = history[i];
         xSemaphoreGive(sensorMutex);
    }
    
    // Bubble sort simples (array pequeno)
    for(int i=0; i<count-1; i++) {
        for(int j=0; j<count-i-1; j++) {
            if(sorted[j] > sorted[j+1]) {
                int temp = sorted[j];
                sorted[j] = sorted[j+1];
                sorted[j+1] = temp;
            }
        }
    }
    
    int mode = sorted[0];
    int maxCount = 1;
    int currentCount = 1;
    
    for(int i=1; i<count; i++) {
        if (sorted[i] == sorted[i-1]) {
            currentCount++;
        } else {
            if (currentCount > maxCount) {
                maxCount = currentCount;
                mode = sorted[i-1];
            }
            currentCount = 1;
        }
    }
    if (currentCount > maxCount) {
        mode = sorted[count-1];
    }
    
    return mode;
}

// Helper Base64 simplificado usando mbedtls
String base64Encode(String input) {
  size_t outputLength = 0;
  unsigned char *outputBuffer = new unsigned char[input.length() * 2]; // Aloca com sobra
  
  int ret = mbedtls_base64_encode(outputBuffer, input.length() * 2, &outputLength, (const unsigned char*)input.c_str(), input.length());
  
  String encoded = "";
  if (ret == 0) {
      outputBuffer[outputLength] = '\0'; 
      encoded = String((char*)outputBuffer);
  } else {
      encoded = "encode_error";
  }
  
  delete[] outputBuffer;
  return encoded;
}

// ==================== HELPER DE REDE (DEEP DEBUG) ====================
int performHttpGet(String url, String &payload) {
    if (WiFi.status() != WL_CONNECTED) {
        debugLog.log("Abort Http: Sem WiFi");
        return -1;
    }

    // === DEEP DEBUGGING START ===
    unsigned long startTotal = millis();
    String host = "";
    // Extrai o host para debug de DNS
    int slashIndex = url.indexOf("//");
    if (slashIndex != -1) {
        int nextSlash = url.indexOf("/", slashIndex + 2);
        if (nextSlash != -1) host = url.substring(slashIndex + 2, nextSlash);
        else host = url.substring(slashIndex + 2);
    }
    
    // Debug DNS
    if(host.length() > 0) {
        IPAddress resolvedIP;
        unsigned long startDNS = millis();
        bool dnsFound = WiFi.hostByName(host.c_str(), resolvedIP);
        long dnsTime = millis() - startDNS;
        if(dnsFound) {
            debugLog.log("DNS: " + host + " -> " + resolvedIP.toString() + " (" + String(dnsTime) + "ms)");
        } else {
            debugLog.log("DNS FAIL: " + host + " (" + String(dnsTime) + "ms)");
            return -1; // Falha DNS aborta cedo
        }
    }
    
    debugLog.log("Req: " + url);
    debugLog.log("Heap: " + String(ESP.getFreeHeap()));

    HTTPClient http;
    int httpCode = -1;
    
    if (url.startsWith("https")) {
        WiFiClientSecure client;
        client.setInsecure();
        client.setHandshakeTimeout(30000); // 30s handshake timeout
        
        http.begin(client, url);
        http.setTimeout(30000); 
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        httpCode = http.GET();
        
        if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
             payload = http.getString();
        }
    } else {
        WiFiClient client;
        http.begin(client, url);
        http.setTimeout(30000); 
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        httpCode = http.GET();
        
        if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
             payload = http.getString();
        }
    }
    
    // Debug Connection + Request
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
             int pLen = payload.length();
             debugLog.log("Payload bytes: " + String(pLen));
             if (pLen > 0) {
                 String preview = payload.substring(0, (pLen > 100 ? 100 : pLen));
                 preview.replace("\n", " ");
                 preview.replace("\r", "");
                 debugLog.log("Data: " + preview + "...");
             }
        }
    } else {
         debugLog.log("Erro Req: " + http.errorToString(httpCode));
    }
    
    http.end();
    debugLog.log("Total Time: " + String(millis() - startTotal) + "ms");
    debugLog.log("---------------------------");
    
    return httpCode;
}

void IoT_Task(void *parameter) {
    debugLog.log("Tarefa IoT iniciada no Core " + String(xPortGetCoreID()));
    
    // Tentativa inicial de conexao
    if (!inAPMode) {
        Config cfg = loadConfig();
        if (cfg.ssid != "" && cfg.ssid != "Wokwi") {
             debugLog.log("Tentando conectar a: " + cfg.ssid);
             WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
        } else {
             debugLog.log("SSID nao configurado. Iniciando AP.");
             switchToAPMode();
        }
    }

    // Variaveis locais para controle de tentativas
    static int connectionAttempts = 0;
    const int maxAttempts = 10;

    for(;;) {
        // Verifica conexao
        if (WiFi.status() == WL_CONNECTED) {
            // Reset contador se conectado
            connectionAttempts = 0;

            // Log de IP para debug
            static bool ipLogged = false;
            if (!ipLogged) {
                 debugLog.log("WiFi OK. IPv4: " + WiFi.localIP().toString());
                 ipLogged = true;
            }

            // OBTEM A MODA DAS ULTIMAS LEITURAS
            int stableValue = calculateMode();
            
            // Envia dados para o servidor
            publishSensorReading(stableValue);
            
            // Verifica por atualizacoes de firmware e configuracoes
            getRemoteConfig();

            // Envia ping para monitoramento
            sendPing();
            
        } else {
             // Se nao estiver em modo AP, tenta gerenciar a conexao
             if (!inAPMode) {
                 connectionAttempts++;
                 debugLog.log("IoT: Sem WiFi (" + String(connectionAttempts) + "/" + String(maxAttempts) + ")");
                 
                 // Na primeira tentativa da sequencia, carrega as credenciais e inicia a conexao
                 if (connectionAttempts == 1) {
                      Config cfg = loadConfig();
                      if (cfg.ssid != "" && cfg.ssid != "Wokwi") {
                          debugLog.log("Iniciando conexao com: " + cfg.ssid);
                          WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
                      } else {
                          debugLog.log("SSID nao configurado. Mudando para AP.");
                          switchToAPMode();
                          connectionAttempts = 0;
                      }
                 }
                 else if (connectionAttempts >= maxAttempts) {
                      debugLog.log("Falha na conexao. Mudando para AP.");
                      switchToAPMode();
                      connectionAttempts = 0;
                 } 
             }
        }
        
        // Aguarda o intervalo (bloqueando apenas esta tarefa)
        unsigned long delayTime = timerDelay > 10000 ? timerDelay : 60000;
        
        // Se estiver desconectado mas tentando, aguarda menos tempo para retentar
        if (!inAPMode && WiFi.status() != WL_CONNECTED) {
            delayTime = 5000; // Tenta a cada 5 segundos
        }
        
        vTaskDelay(pdMS_TO_TICKS(delayTime)); 
    }
}

void performUpdate(String url) {
    // Atualiza tela com U8g2
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, "Atualizando...");
    u8g2.drawStr(0, 22, "Baixando FW...");
    u8g2.sendBuffer();

    // Configura Client (Secure ou Insecure) usando HEAP
    HTTPClient http;
    WiFiClientSecure *clientSecure = nullptr;
    WiFiClient *clientInsecure = nullptr;
    
    // Logica de HTTPS
    if (url.startsWith("https")) {
        clientSecure = new WiFiClientSecure();
        clientSecure->setInsecure();
        clientSecure->setHandshakeTimeout(30000); 
        http.begin(*clientSecure, url);
    } else {
        clientInsecure = new WiFiClient();
        http.begin(*clientInsecure, url);
    }
    
    http.setTimeout(30000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    int httpCode = http.GET();
    // Redirect manual removido pois setFollowRedirects resolve

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Erro no download, codigo: %d\n", httpCode);
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "Falha no FW!");
        String codeStr = "Cod: " + String(httpCode);
        u8g2.drawStr(0, 22, codeStr.c_str());
        u8g2.sendBuffer();
        
        http.end();
        if (clientSecure) delete clientSecure;
        if (clientInsecure) delete clientInsecure;
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("Tamanho do conteudo invalido.");
        http.end();
        if (clientSecure) delete clientSecure;
        if (clientInsecure) delete clientInsecure;
        return;
    }

    if (!Update.begin(contentLength)) {
        Serial.println("Nao ha espaco para atualizar.");
        http.end();
        if (clientSecure) delete clientSecure;
        if (clientInsecure) delete clientInsecure;
        return;
    }

    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Gravando...");
    u8g2.drawStr(0, 22, "Nao desligue!");
    u8g2.sendBuffer();

    size_t written = Update.writeStream(http.getStream());

    if (written != contentLength) {
          Serial.printf("Escrita falhou! Escrito %d de %d bytes\n", written, contentLength);
    } else {
          if (Update.end()) {
              Serial.println("Atualizacao finalizada com sucesso!");
              u8g2.clearBuffer();
              u8g2.drawStr(0, 10, "Sucesso!");
              u8g2.drawStr(0, 22, "Reiniciando...");
              u8g2.sendBuffer();
              delay(2000);
              
              http.end();
              if (clientSecure) delete clientSecure;
              if (clientInsecure) delete clientInsecure;
              
              ESP.restart();
          } else {
              Serial.println("Erro ao finalizar a atualizacao: " + String(Update.getError()));
          }
    }
    
    http.end();
    if (clientSecure) delete clientSecure;
    if (clientInsecure) delete clientInsecure;
}


// =========================================================================
// AS FUNÇÕES ABAIXO NÃO FORAM ALTERADAS, APENAS REPRODUZIDAS INTEGRALMENTE
// =========================================================================


// ==================== FUNÇÕES DE REDE ====================
void switchToAPMode() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  startAccessPoint();
  StatusInternet = "Modo AP";
  inAPMode = true;
  ledInterval = 100;
  digitalWrite(LED_PIN, HIGH);
  Serial.println("Modo AP ativado");
}

void switchToStationMode() {
  Config cfg = loadConfig();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  inAPMode = false;
  ledInterval = 1000;
  Serial.println("Tentando conectar como estação...");
}

void startAccessPoint() {
  Serial.println("Configurando Access Point...");
  IPAddress apIP(192, 168, 40, 1);
  IPAddress gateway(192, 168, 40, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  Config cfg = loadConfig();
  String apSsid;
  int id = cfg.sensorId.toInt();

  if (id >= 1 && id <= 6) {
    const char* torres[6] = {"E", "F", "A", "B", "C", "D"};
    apSsid = "Sensor_T" + String(torres[id - 1]);
  } else {
    apSsid = "Sensor_ID" + cfg.sensorId;
  }

  if (WiFi.softAP(apSsid.c_str(), "12345678", 6, false, 4)) {
    Serial.print("AP iniciado: "); Serial.println(apSsid);
    Serial.print("IP do AP: "); Serial.println(WiFi.softAPIP()); // Função IoT() antiga removida (substituida por IoT_Task)
    if (dnsServer.start(53, "*", apIP)) {
      Serial.println("Servidor DNS iniciado.");
    } else {
      Serial.println("Falha ao iniciar servidor DNS!");
    }
  } else {
    Serial.println("Falha ao iniciar AP!");
  }
}

void handleWiFiEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Conectado ao AP");
      StatusInternet = "WiFi Conectado";
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("IP obtido: "); Serial.println(WiFi.localIP());
      StatusInternet = WiFi.localIP().toString();
      ledInterval = 250;
      syncTime();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Desconectado do WiFi");
      StatusInternet = "Sem Wifi...";
      ledInterval = 1000;
      if (!inAPMode) {
        static int failedAttempts = 0;
        failedAttempts++;
        if (failedAttempts >= 5) {
          switchToAPMode();
          failedAttempts = 0;
        } else {
          WiFi.reconnect();
        }
      }
      break;
    default:
      break;
  }
}

// ==================== CONTROLE DO LED ====================
void updateLed() {
  if (inAPMode) {
    digitalWrite(LED_PIN, LOW); // Modo AP - LED sempre aceso
  } else {
    // Modo Estação - LED piscando
    if (millis() - lastLedToggle >= ledInterval) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastLedToggle = millis();
    }
  }
}

// ==================== FUNÇÕES DO SENSOR ====================
// ==================== FUNÇÕES DO SENSOR ====================
void sonar() {
  int raw = sonar1.read();
  // Validacao basica
  if (raw > 0 && raw < 400) {
       int calc = raw - distanciaSonda; // Ajuste de offset
       
       // Seta variavel global para display
       distance = calc;
       
       // Adiciona ao historico para moda (Thread Safe)
       if (xSemaphoreTake(sensorMutex, portMAX_DELAY)) {
            history[historyIndex] = calc;
            historyIndex = (historyIndex + 1) % HISTORY_SIZE;
            if (historyCount < HISTORY_SIZE) historyCount++;
            xSemaphoreGive(sensorMutex);
       }
  }
  
  // Serial de feedback removido para reduzir ruido, ja temos log
}


String urlEncode(const String& str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encodedString += c;
    } else {
      code0 = (c >> 4) & 0xF;
      code1 = (c & 0xF);
      encodedString += '%';
      encodedString += (code0 < 10) ? ('0' + code0) : ('A' + code0 - 10);
      encodedString += (code1 < 10) ? ('0' + code1) : ('A' + code1 - 10);
    }
  }
  return encodedString;
}

Config loadConfig() {
  Config cfg;
  prefs.begin("wifi-config", true);
  cfg.ssid = prefs.getString("ssid", "Wokwi");
  cfg.pass = prefs.getString("pass", "");
  cfg.sensorId = prefs.getString("sensorId", "99");
  cfg.nomeSonda = prefs.getString("nomeSonda", "Laboratorio-Dev");
  prefs.end();
  return cfg;
}

void saveConfig(String ssid, String pass, String sensorId, String nomeSonda) {
  prefs.begin("wifi-config", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("sensorId", sensorId);
  prefs.putString("nomeSonda", nomeSonda);
  prefs.end();
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/esquema", handleEsquema);
  server.on("/scan-wifi", handleWiFiScan);
  server.on("/generate_204", handleRoot);
  
  // Endpoint de Debug
  server.on("/debug", handleDebug);
  server.on("/debug/clear", HTTP_POST, handleDebugClear);

  // Endpoint API Status (JSON)
  server.on("/api/status", HTTP_GET, []() {
      String json = "{";
      json += "\"distance\":" + String(distance) + ",";
      json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
      json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
      json += "\"mode_value\":" + String(calculateMode()); 
      json += "}";
      server.send(200, "application/json", json);
  });

  server.onNotFound(handleNotFound);
  server.begin();
}

void handleNotFound() {
  handleRoot();
}

String escapeHTML(String input) {
  input.replace("&", "&amp;");
  input.replace("\"", "&quot;");
  input.replace("'", "&#39;");
  input.replace("'", "&#39;");
  input.replace("<", "&lt;");
  input.replace(">", "&gt;");
  return input;
}

// ==================== HANDLER RAIZ (CONFIG + DASHBOARD) ====================
void handleRoot() {
  Config cfg = loadConfig();
  String html = R"rawliteral(
  <!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>Configuração do Sensor (C3)</title>
  <style>
    body{font-family:Arial,sans-serif;margin:20px;background-color:#f5f5f5;}
    .container{background-color:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);max-width:500px;margin:0 auto;}
    h2{color:#333;text-align:center;}
    form{margin-top:20px;}
    .form-group{margin-bottom:15px;}
    label{display:block;margin-bottom:5px;font-weight:bold;}
    input[type="text"],input[type="password"],input[type="number"],select{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;font-size:16px;}
    .btn-group {display: flex; gap: 10px; margin-top: 15px;}
    input[type="submit"],button{flex: 1; background-color:#4CAF50;color:white;padding:12px;border:none;border-radius:4px;cursor:pointer;font-size:16px;}
    button.secondary {background-color: #2196F3;}
    button.debug {background-color: #FF5722;}
    input[type="submit"]:hover,button:hover{opacity:0.9;}
    #ssid-list{display:none;}
    .loading{display:none;text-align:center;}
  </style>
  </head><body>
  <div class="container">
    <h2>Sensor Liquid Sky (C3)</h2>
    
    <!-- REALTIME DASHBOARD -->
    <div style="background: #e0f7fa; padding: 15px; border-radius: 5px; margin-bottom: 20px; text-align: center; border: 1px solid #b2ebf2;">
        <h3 style="margin-top: 0; color: #006064;">Nível Atual (Moda)</h3>
        <div style="font-size: 48px; font-weight: bold; color: #00838f;" id="dash-dist">-- cm</div>
        <div style="display: flex; justify-content: space-around; margin-top: 10px; font-size: 14px; color: #555;">
            <span>Signal: <strong id="dash-rssi">--</strong> dBm</span>
            <span>Heap: <strong id="dash-heap">--</strong> B</span>
            <span>Latência: <strong id="dash-lat">--</strong> ms</span>
        </div>
    </div>
    
    <form action="/save" method="GET">
      <h3>WiFi & ID</h3>
      <div class="form-group"><label for="ssid">SSID:</label>
        <div style="display:flex; gap:5px;">
            <input type="text" id="ssid" name="ssid" value=")rawliteral";
            html += escapeHTML(cfg.ssid);
            html += R"rawliteral(">
            <button type="button" onclick="scanWiFi()" class="secondary" style="flex:0 0 60px;">Scan</button>
        </div>
        <div id="loading" class="loading">Buscando...</div>
        <select id="ssid-list" onchange="document.getElementById('ssid').value=this.value"><option value="">Selecionar...</option></select>
      </div>

      <div class="form-group"><label for="pass">Senha WiFi:</label><input type="text" id="pass" name="pass" value=")rawliteral" + escapeHTML(cfg.pass) + R"rawliteral("></div>
      
      <div class="form-group"><label for="sensor-id">ID do Sensor (1-6 para Torres):</label>
        <input type="number" id="sensor-id" name="sensor_id" value=")rawliteral" + cfg.sensorId + R"rawliteral(" min="1" required>
      </div>
      
      <div class="form-group"><label for="nome_sonda">Nome Local:</label><input type="text" id="nome_sonda" name="nome_sonda" value=")rawliteral" + escapeHTML(cfg.nomeSonda) + R"rawliteral("></div>

      <h3>Parametros Remotos</h3>
      <div class="form-group"><label>URL Ping:</label><input type="text" name="ping_url" value=")rawliteral" + escapeHTML(pingBaseUrl) + R"rawliteral("></div>
      <div class="form-group"><label>URL Remote Config:</label><input type="text" name="remote_url" value=")rawliteral" + escapeHTML(remoteConfigUrl) + R"rawliteral("></div>
      <div class="form-group"><label>URL Leitura (Sonda):</label><input type="text" name="sonda_url" value=")rawliteral" + escapeHTML(novoUrlSite) + R"rawliteral("></div>

      <div class="btn-group">
        <input type="submit" value="Salvar">
      </div>
    </form>
    
    <div class="btn-group">
       <button type="button" class="secondary" onclick="window.location='/esquema'">Esquema</button>
       <button type="button" class="debug" onclick="window.location='/debug'">Debug Log</button>
    </div>
    
  </div>
  <script>
  // REALTIME POLLING
  setInterval(function() {
      var start = Date.now();
      fetch('/api/status')
        .then(response => response.json())
        .then(data => {
            var lat = Date.now() - start;
            document.getElementById('dash-dist').innerText = data.mode_value + ' cm';
            document.getElementById('dash-rssi').innerText = data.rssi;
            document.getElementById('dash-heap').innerText = data.heap;
            document.getElementById('dash-lat').innerText = lat;
        })
        .catch(e => console.log('Erro poll:', e));
  }, 2000);

  function scanWiFi(){
    const lst=document.getElementById('ssid-list'),load=document.getElementById('loading');
    load.style.display='block';lst.style.display='none';
    fetch('/scan-wifi').then(r=>r.json()).then(d=>{
        lst.innerHTML='<option value="">Selecionar...</option>';
        d.networks.forEach(n=>{
            let opt=document.createElement('option');
            opt.value=n.ssid;
            opt.textContent=n.ssid+' ('+n.rssi+')';
            lst.appendChild(opt);
        });
        lst.style.display='block';load.style.display='none';
    });
  }
  </script>
  </body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleEsquema(){
  String html = R"rawliteral(

<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-C3 com Sensor</title>
    <style>
        body {
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            background-color: #f0f0f0;
            margin: 0;
            font-family: Arial, sans-serif;
            gap: 30px;
        }

        .main-container {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 30px;
        }

        .board-container {
            position: relative;
            width: 200px;
            height: 400px;
            background-color: #333;
            border-radius: 8px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            display: flex;
            justify-content: space-between;
            padding: 20px 10px;
            box-sizing: border-box;
        }

        .board-label {
            position: absolute;
            top: 20px;
            left: 50%;
            transform: translateX(-50%);
            color: #eee;
            font-size: 1.5em;
            font-weight: bold;
            text-shadow: 1px 1px 2px rgba(0,0,0,0.5);
            pointer-events: none;
        }

        .oled-display {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            width: 100px;
            height: 100px;
            background-color: #000;
            border: 2px solid #555;
            border-radius: 4px;
        }

        .micro-usb {
            width: 40px;
            height: 15px;
            background-color: #888;
            border-radius: 3px;
            position: absolute;
            bottom: -10px;
            left: 50%;
            transform: translateX(-50%);
            z-index: 10;
            border: 1px solid #666;
        }

        .pin-column {
            display: flex;
            flex-direction: column-reverse;
            justify-content: space-around;
            height: 100%;
        }

        .pin {
            width: 30px;
            height: 30px;
            background-color: #bbb;
            border-radius: 50%;
            border: 1px solid #888;
            box-sizing: border-box;
            display: flex;
            justify-content: center;
            align-items: center;
            font-size: 12px;
            color: #444;
            font-weight: bold;
            margin: 3px 0;
            flex-shrink: 0;
        }

        /* Cores ESP32 */
        .pin.v5 { background-color: red; color: white; }
        .pin.gnd { background-color: black; color: white; }
        .pin.v3 { background-color: orange; color: white; }
        .pin.rx { background-color: #007bff; color: white; }
        .pin.tx { background-color: #ffc107; color: black; }
        .pin.sda { background-color: teal; color: white; }
        .pin.scl { background-color: #4CAF50; color: white; }
        .pin.trigger { background-color: #9C27B0; color: white; }
        .pin.echo { background-color: #FF5722; color: white; }
        .pin.led { background-color: #00BCD4; color: white; }

        /* Estilo do Sensor */
        .sensor-container {
            position: relative;
            width: 180px;
            height: 60px;
            background-color: #333;
            border-radius: 8px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            display: flex;
            flex-direction: column;
            align-items: center;
            padding-top: 10px;
        }

        .sensor-label {
            color: #eee;
            font-size: 1.2em;
            font-weight: bold;
            text-shadow: 1px 1px 2px rgba(0,0,0,0.5);
            margin-bottom: 15px;
        }

        .sensor-pins {
            display: flex;
            justify-content: center;
            gap: 15px;
            width: 100%;
            position: absolute;
            bottom: -15px;
        }

        .sensor-pin {
            width: 25px;
            height: 25px;
            background-color: #bbb;
            border-radius: 50%;
            border: 1px solid #888;
            display: flex;
            justify-content: center;
            align-items: center;
            font-size: 10px;
            font-weight: bold;
            position: relative;
        }

        /* Cores do Sensor */
        .sensor-pin.vcc { background-color: red; color: white; }
        .sensor-pin.trig { background-color: #9C27B0; color: white; }
        .sensor-pin.echo { background-color: #FF5722; color: white; }
        .sensor-pin.gnd { background-color: black; color: white; }

        .connection-line {
            position: absolute;
            background-color: #666;
            width: 2px;
            height: 30px;
            left: 50%;
            bottom: -30px;
        }
    </style>
</head>
<body>
    <div class="main-container">
        <div class="board-container">
            <div class="board-label">ESP32-C3</div>
            <div class="oled-display"></div>
            <div class="micro-usb"></div>

            <div class="pin-column left">
                <div class="pin" title="GPI10 (Echo)">10</div>
                <div class="pin" title="GPIO09 (Trig)">09</div>
                <div class="pin" title="GPIO8 (LED)">8</div>
                <div class="pin" title="GPIO7">7</div>
                <div class="pin" title="GPIO6 (SCL)">6</div>
                <div class="pin" title="GPIO5 (SDA)">5</div>
                <div class="pin" title="GPIO4">4</div>
                <div class="pin" title="GPIO3">3</div>
            </div>

            <div class="pin-column right">
                <div class="pin v5" title="V5">V5</div>
                <div class="pin gnd" title="GND">GD</div>
                <div class="pin" title="3V3">3V3</div>
                <div class="pin rx" title="RX">RX</div>
                <div class="pin tx" title="TX">TX</div>
                <div class="pin" title="GPIO2">2</div>
                <div class="pin" title="GPIO1">1</div>
                <div class="pin" title="GPIO0">0</div>
            </div>
        </div>

        <div class="sensor-container">
            <div class="sensor-label">HC-SR04</div>
            <div class="sensor-pins">
                <div class="pin v5" title="VCC">VCC</div>
                <div class="pin rx" title="Trigger">TRI</div>
                <div class="pin tx" title="Echo">ECH</div>
                <div class="pin gnd" title="GND">GND</div>
            </div>
        </div>
    </div>
</body>
</html>

)rawliteral";
  server.send(200, "text/html", html);
}

void handleDebug() {
    server.send(200, "text/html", debugLog.toHtml());
}

void handleDebugClear() {
    debugLog.clear();
    server.sendHeader("Location", "/debug");
    server.send(303);
}

void handleWiFiScan() {
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  for (int i = 0; i < n; ++i) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]}";
  server.sendHeader("Content-Type", "application/json");
  server.send(200, "application/json", json);
  WiFi.scanDelete();
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String sensorId = server.arg("sensor_id");
  String nomeSonda = server.arg("nome_sonda");

  if (ssid.isEmpty() || sensorId.isEmpty()) {
    server.send(400, "text/plain", "Erro: Preencha todos os campos.");
    return;
  }
  
  // Salva WIFI
  saveConfig(ssid, pass, sensorId, nomeSonda);
  
  // Salva urls extras (se presentes)
  if (server.hasArg("ping_url")) pingBaseUrl = server.arg("ping_url");
  if (server.hasArg("remote_url")) remoteConfigUrl = server.arg("remote_url");
  if (server.hasArg("sonda_url")) novoUrlSite = server.arg("sonda_url");
  saveRemoteParams();

  String response = "<html><meta charset='UTF-8'><body><h1>Configuracoes salvas!</h1><p>O dispositivo sera reiniciado em 5 segundos...</p></body></html>";
  server.send(200, "text/html; charset=UTF-8", response);
  delay(5000);
  ESP.restart();
}

// ==================== PERSISTENCIA DE PARAMETROS REMOTOS ====================
void initRemoteParams() {
  debugLog.log("Carregando parametros remotos do disco...");
  prefs.begin("global-config", true);
  
  if (prefs.isKey("novoUrlSite")) novoUrlSite = prefs.getString("novoUrlSite", novoUrlSite);
  if (prefs.isKey("pingBaseUrl")) pingBaseUrl = prefs.getString("pingBaseUrl", pingBaseUrl);
  if (prefs.isKey("remoteConfigUrl")) remoteConfigUrl = prefs.getString("remoteConfigUrl", remoteConfigUrl);
  if (prefs.isKey("timerDelay")) timerDelay = prefs.getULong("timerDelay", timerDelay);
  if (prefs.isKey("alertLevelCm")) alertLevelCm = prefs.getInt("alertLevelCm", alertLevelCm);
  
  prefs.end();
  
  debugLog.log("Params carregados:");
  debugLog.log(" Remote: " + remoteConfigUrl);
  debugLog.log(" Ping: " + pingBaseUrl);
  debugLog.log(" Sonda: " + novoUrlSite);
}

void saveRemoteParams() {
  debugLog.log("Salvando novos parametros no disco...");
  prefs.begin("global-config", false);
  prefs.putString("novoUrlSite", novoUrlSite);
  prefs.putString("pingBaseUrl", pingBaseUrl);
  prefs.putString("remoteConfigUrl", remoteConfigUrl);
  prefs.putULong("timerDelay", timerDelay);
  prefs.putInt("alertLevelCm", alertLevelCm);
  prefs.end();
}

void getRemoteConfig() {
  // String remoteConfigUrl = ... (REMOVED to use Global)
  debugLog.log("Buscando configuracoes remotas...");
  
  String payload;
  int httpCode = performHttpGet(remoteConfigUrl, payload);

  if (httpCode != HTTP_CODE_OK) {
    debugLog.log("Falha config: " + String(httpCode));
    return;
  }
  
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, payload);
  
  JsonObject generalConfig = doc["general_config"];
  if (!generalConfig.isNull()) {
      if (generalConfig.containsKey("ping_base_url")) {
          String val = generalConfig["ping_base_url"].as<String>();
          debugLog.log("Attr ping_base: " + val);
          pingBaseUrl = val;
      }
      if (generalConfig.containsKey("update_interval_ms")) timerDelay = generalConfig["update_interval_ms"];
      if (generalConfig.containsKey("url_leituras_sensor")) {
           String val = generalConfig["url_leituras_sensor"].as<String>();
           debugLog.log("Attr sonda_url: " + val);
           novoUrlSite = val;
      }
      
      // Salva no disco
      saveRemoteParams();
  } else {
      debugLog.log("JSON general_config missing or null");
      // Debug do payload inteiro se falhar
      debugLog.log("Payload: " + payload.substring(0, 100)); 
  }

  debugLog.log("Config atualizada. Ping: " + pingBaseUrl);

  JsonObject fotaConfig = doc["fota_config"][BOARD_MODEL];
  if (fotaConfig.isNull()) {
    debugLog.log("Sem FOTA para: " + String(BOARD_MODEL));
    return;
  }
  
  float newVersion = fotaConfig["version"];
  if (newVersion > FW_VERSION) {
    debugLog.log("Nova versao: " + String(newVersion));
    String firmwareUrl = fotaConfig["url"];
    performUpdate(firmwareUrl);
  } else {
    debugLog.log("FW Atualizado.");
  }
}

void sendPing() {
  if (pingBaseUrl.length() == 0) return;
  Config cfg = loadConfig();
  
  // Remove : do UUID
  String cleanUuid = deviceUuid;
  cleanUuid.replace(":", "");
  
  // Prepara Log codificado
  String logEncoded = "";
  if (xSemaphoreTake(logMutex, 100)) {
      String logs = debugLog.getLogString();
      // Pega os ultimos 500 chars se for muito grande
      if (logs.length() > 500) logs = logs.substring(logs.length() - 500);
      logEncoded = base64Encode(logs);
      xSemaphoreGive(logMutex);
  }

  String pingUrl = pingBaseUrl;
  pingUrl += "?uuid=" + urlEncode(cleanUuid);
  pingUrl += "&board=" + urlEncode(String(BOARD_MODEL));
  pingUrl += "&site_esp=" + urlEncode(nomeDaSonda);
  pingUrl += "&ssid=" + urlEncode(cfg.ssid);
  pingUrl += "&sensorId=" + urlEncode(String(idSensor));
  pingUrl += "&version=" + urlEncode(String(FW_VERSION));
  pingUrl += "&log_b64=" + urlEncode(logEncoded);

  debugLog.log("Ping: " + pingUrl);
  
  String payload;
  performHttpGet(pingUrl, payload);
}

void publishSensorReading(int sensorValue) {
  if (novoUrlSite.length() == 0) return;
  String publishUrl = novoUrlSite + "/sonda/?sensor=" + String(idSensor) + "&valor=" + String(sensorValue);
  
  debugLog.log("Pub Leitura: " + publishUrl);
  
  String payload;
  performHttpGet(publishUrl, payload);
}

void setupDeviceID() {
  prefs.begin("device-info", false);
  deviceUuid = prefs.getString("uuid", "");
  if (deviceUuid.length() == 0) {
    Serial.println("Gerando novo UUID...");
    deviceUuid = String(BOARD_MODEL) + "-" + WiFi.macAddress();
    prefs.putString("uuid", deviceUuid);
  }
  Serial.println("ID do Dispositivo: " + deviceUuid);
  prefs.end();
}

void syncTime() {
  Serial.println("Sincronizando horario...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Falha ao obter horario");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}
