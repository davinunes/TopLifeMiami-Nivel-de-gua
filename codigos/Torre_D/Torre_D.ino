/* ESTE SKETCH É  BASEADO NO EXEMPLO DA LIBRARY esp8266 and esp32 OLED driver for SSD1306
   Bibliografia: https://www.youtube.com/watch?v=dD2BqAsN96c, https://www.curtocircuito.com.br/blog/Categoria%20IoT/desenvolvimento-de-dashboard-mqtt-com-adafruitio
   PINOUT da placa utilizada: https://raw.githubusercontent.com/AchimPieters/esp32-homekit-camera/master/Images/ESP32-30PIN-DEVBOARD.png
*/

// CONFIGURAÇÕES DA INSTALAÇÃO

// Qual id do sensor no site davinunes.eti.br? (ou outro que for escolhido)

/*
 * 1 - Torre E
 * 2 - Torre F
 * 3 - Torre A
 * 4 - Torre B
 * 5 - Torre C
 * 6 - Torre D
 * */
 
int idSensor = 6; 

// A que distancia a sonda está do nivel máximo de água?
int distanciaSonda = 20; 
// Qual a altura maxima da coluna de água? (Não considerar a distancia da Sonda)
int alturaAgua = 240; 

// Qual Nome da Sonda nas mensagens do Telegram?
/*
 * Torre A Reservatório 01
 * Torre B Reservatório 01
 * Torre C Reservatório 01
 * Torre D Reservatório 01
 * Torre E Reservatório 01
 * Torre F Reservatório 01
 * */
 
String nomeDaSonda = "Torre D Reservatório 01";

// Qual Nome da Rede Wifi na casa de máquinas?

/*
 * TAIFIBRA-BLOCO A
 * TAIFIBRA-BLOCO B
 * TAIFIBRA-BLOCO C
 * TAIFIBRA-BLOCO D
 * TAIFIBRA-BLOCO E
 * TAIFIBRA-BLOCO F
 * */
 
const char* ssid = "TAIFIBRA-BLOCO D"; 

// Qual Senha da rede Wifi = taifibratelecom
const char* password = "taifibratelecom"; 

// Endereços do github que serão utilizados para ajustar variáveis remotas:
String IntervaloDePush      = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/updateTime";
String nivelAlertaTelegram  = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/nivelAlerta";
String novoUrlSite          = "https://raw.githubusercontent.com/davinunes/TopLifeMiami-Nivel-de-gua/main/parametros/novoUrlSite";

String urlSite = "h2o-miami.davinunes.eti.br";

/* INICIALIZA O Wifi
   e o cliente http
   Todas as variáveis abaixo são referentes ao wifi e ao delay entre os acessos a internet
*/

#include <WiFi.h>
#include <HTTPClient.h>

unsigned long lastTime = 0;
unsigned long lastTimeAlert = 0;
unsigned long timerDelay = 60000; //120 segundos
unsigned long timerAlerta = 600000; //5 minutos
unsigned long nivelAlerta = 80; //80%
WiFiClient client;
String StatusInternet = "Sem Wifi...";


/* INICIALIZA O DISPLAY COM A LIB SSD1306Wire.h
   SE UTILIZAR O DISPLAY SSD1306:
   SSD1306Wire  display(0x3c, SDA, SCL);
   SDA -> GPIO21
   SCL -> GPIO22
*/

#include "SSD1306Wire.h"
SSD1306Wire display(0x3c, 21, 22);

/* INICIALIZA O Sonar com a LIB Ultrasonic.h
   Modelos HC-SR04 ou SEN136B5B
*/

#define TRIG_PIN  33 // Arduino pin tied to trigger pin on ping sensor.
#define ECHO_PIN  25 // Arduino pin tied to echo pin on ping sensor.

int progresso = 0;      // Calculo da % da barra de progresso


/*

   ===========================================
   Configuração durante o boot do ESP32
   ===========================================

*/

/*
 * 
 * Protótipos
 * 
 * das
 * 
 * funções
 */

String wget (String url);
void eti(int num);
void sonar();
void tela();
void internet();
void getParametrosRemotos();

/*
 * Setup
 */

int distance;         // Distancia medida pelo sensor em CM
int minimo;            // Menor distancia já medida
int maximo;            // Maior distancia já medida

void setup() {
  Serial.begin(115200);
  internet();
   
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
   
  // INICIALIZA O DISPLAY & INVERTE O DISPLAY VERTICALMENTE
  display.init();
  display.flipScreenVertically();
   
   WiFi.begin(ssid, password);
   int i = 0;
       while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      if (i++ > 10) {
        break;
      }

    }

}

//========================================================================

void loop() {
  sonar();
  tela();
  IoT();
}

/*
 * funções
 */


String wget (String url) {
  HTTPClient http;

  String serverPath = url;

  // Your Domain name with URL path or IP address with path
  http.begin(serverPath.c_str());

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String payload = http.getString();
    return payload;

  }
  else {
    return "erro";
  }
  http.end();
}

void eti(int num) {
  // ColeSeuTokenAqui ColeIDdoGrupoAqui TestandoEnvio
  //Vamos obter a URL perguntando também ao github
  String url = urlSite+"/sonda/?sensor=" + String(idSensor) + "&valor=" + String(num);
  Serial.println(wget(url));
}

void sonar() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(20);
  digitalWrite(TRIG_PIN, LOW);

  const unsigned long duration= pulseIn(ECHO_PIN, HIGH);
  distance= duration/29/2;
  if(duration==0){
    Serial.println("Warning: no pulse from sensor");
  } else {
    Serial.print("distance to nearest object:");
    Serial.println(distance);
    Serial.println(" cm");
  }
  delay(100);

  distance > alturaAgua ? progresso = alturaAgua : progresso = distance;
  progresso = 100 * progresso / alturaAgua;
  progresso = 100 - progresso;

}

void tela() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, StatusInternet);
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 14, String(distance));
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 14, String(minimo));
  display.drawString(64, 28, String(maximo));
  //display.drawString(0, 40, String(minimo));
  //display.drawString(60, 40, String(maximo));
  display.drawString(90, 19, String(progresso));
  display.drawProgressBar(0, 40, 127, 22, progresso);
  display.display();
  delay(500);
}

void IoT() {
  internet();
  if ((millis() - lastTime) > timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("HORA DE TAREFAS DA WEB");
      
      String msg = nomeDaSonda + " -> Distancia do Sensor: " + String(distance) + "cm";
      
      eti(distance);

      getParametrosRemotos();
      lastTime = millis(); // Esta linha sempre no final BLOCO QUE CONTROLA O TIMER!

    }
  }
}

void internet(){
  if (WiFi.status() == WL_CONNECTED) {
    StatusInternet = "NaRede!"+WiFi.localIP().toString();
    return;
  } else {
    StatusInternet = "Sem Wifi...";
    
    //Serial.println("Conectando na rede WiFi");
    
    //Serial.println("Conectado com IP: ");
    //Serial.println(WiFi.localIP());
  }
}

void getParametrosRemotos(){
      timerDelay = wget(IntervaloDePush).toInt();
      nivelAlerta = wget(nivelAlertaTelegram).toInt();
      urlSite = wget(novoUrlSite);
      Serial.println(timerDelay);
      Serial.println(nivelAlerta);
  }
