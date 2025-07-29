// Projeto: Control_Rele_8 com suporte a remoção, agendamento por dias da semana e modo AP para configuração Wi-Fi
// Placa: ESP8266 (NodeMCU ou Wemos D1 Mini)

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

#define EEPROM_SIZE 2048
#define WIFI_SSID_ADDR 1000
#define WIFI_PASS_ADDR 1064

ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -10800, 60000); // UTC-3 Brasil

const int relePins[8] = {5, 4, 14, 12, 13, 15, 0, 2};
bool releStates[8] = {false};

struct Agendamento {
  uint8_t hora;
  uint8_t minuto;
  uint8_t rele;
  bool ligar;
  uint8_t diasSemana; // bits 0 a 6 = Dom a Sab
};

Agendamento agendamentos[50];
int totalAgendamentos = 0;

char ssid[32] = "";
char password[64] = "";

void salvarWiFiConfig() {
  EEPROM.put(WIFI_SSID_ADDR, ssid);
  EEPROM.put(WIFI_PASS_ADDR, password);
  EEPROM.commit();
}

void carregarWiFiConfig() {
  EEPROM.get(WIFI_SSID_ADDR, ssid);
  EEPROM.get(WIFI_PASS_ADDR, password);
  if (strlen(ssid) == 0 || strlen(password) == 0) {
    strcpy(ssid, "");
    strcpy(password, "");
  }
}

void iniciarModoAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Control_Rele_8_Config");
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", "<form method='POST'><input name='s'><input name='p'><input type='submit'></form>");
  });
  server.on("/", HTTP_POST, []() {
    String ss = server.arg("s");
    String ps = server.arg("p");
    ss.toCharArray(ssid, sizeof(ssid));
    ps.toCharArray(password, sizeof(password));
    salvarWiFiConfig();
    server.send(200, "text/html", "<h3>Salvo! Reinicie o dispositivo.</h3>");
  });
  server.begin();
}

void salvarEEPROM() {
  EEPROM.put(0, totalAgendamentos);
  EEPROM.put(4, agendamentos);
  EEPROM.commit();
}

void carregarEEPROM() {
  EEPROM.get(0, totalAgendamentos);
  EEPROM.get(4, agendamentos);
}

String formatarHoraMinuto(int h, int m) {
  char buffer[6];
  sprintf(buffer, "%02d:%02d", h, m);
  return String(buffer);
}

String diasSemanaStr(uint8_t mask) {
  String dias[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"};
  String resultado = "";
  for (int i = 0; i < 7; i++) if (bitRead(mask, i)) resultado += dias[i] + " ";
  return resultado;
}

void verificarAgendamentos() {
  static int ultimoMinuto = -1;
  timeClient.update();
  int hora = timeClient.getHours();
  int minuto = timeClient.getMinutes();
  int diaSemana = (timeClient.getDay() + 6) % 7;

  if (minuto == ultimoMinuto) return;
  ultimoMinuto = minuto;

  for (int i = 0; i < totalAgendamentos; i++) {
    if (agendamentos[i].hora == hora && agendamentos[i].minuto == minuto) {
      if (bitRead(agendamentos[i].diasSemana, diaSemana)) {
        releStates[agendamentos[i].rele] = agendamentos[i].ligar;
        digitalWrite(relePins[agendamentos[i].rele], agendamentos[i].ligar ? LOW : HIGH);
      }
    }
  }
}

void handleRoot() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>.btn{padding:8px;margin:4px;} input{margin:3px;}</style></head><body>";
  html += "<h2>Control Rele 8</h2><p>Hora atual: " + timeClient.getFormattedTime() + "</p><hr>";

  for (int i = 0; i < 8; i++) {
    html += "<p>Rele " + String(i+1) + ": ";
    html += releStates[i] ? "<b>LIGADO</b>" : "<b>DESLIGADO</b>";
    html += " <a class='btn' href='/on" + String(i) + "'>Ligar</a>";
    html += " <a class='btn' href='/off" + String(i) + "'>Desligar</a></p>";
  }

  html += "<hr><h3>Agendamentos</h3><ul>";
  for (int i = 0; i < totalAgendamentos; i++) {
    html += "<li>" + formatarHoraMinuto(agendamentos[i].hora, agendamentos[i].minuto);
    html += " - Rele " + String(agendamentos[i].rele+1);
    html += " - ";
    html += (agendamentos[i].ligar ? "Ligar" : "Desligar");
    html += " - Dias: " + diasSemanaStr(agendamentos[i].diasSemana);
    html += " <a href='/del?i=" + String(i) + "'>Remover</a></li>";
  }
  html += "</ul><hr><h3>Novo Agendamento</h3><form method='POST' action='/add'>";
  html += "Hora: <input type='number' name='hora' min='0' max='23'><br>";
  html += "Minuto: <input type='number' name='minuto' min='0' max='59'><br>";
  html += "Relé (1-8): <input type='number' name='rele' min='1' max='8'><br>";
  html += "Ação: <select name='acao'><option value='1'>Ligar</option><option value='0'>Desligar</option></select><br>";
  html += "Dias:<br>";
  String dias[] = {"Dom","Seg","Ter","Qua","Qui","Sex","Sab"};
  for (int i = 0; i < 7; i++) {
    html += "<label><input type='checkbox' name='dia" + String(i) + "' value='1'>" + dias[i] + "</label><br>";
  }
  html += "<input type='submit' value='Agendar'></form></body></html>";
  server.send(200, "text/html", html);
}

void adicionarAgendamento() {
  if (totalAgendamentos >= 50) return;
  Agendamento a;
  a.hora = server.arg("hora").toInt();
  a.minuto = server.arg("minuto").toInt();
  a.rele = server.arg("rele").toInt() - 1;
  a.ligar = server.arg("acao") == "1";
  a.diasSemana = 0;
  for (int i = 0; i < 7; i++) if (server.arg("dia" + String(i)) == "1") bitSet(a.diasSemana, i);
  agendamentos[totalAgendamentos++] = a;
  salvarEEPROM();
  server.sendHeader("Location", "/");
  server.send(303);
}

void removerAgendamento() {
  int i = server.arg("i").toInt();
  if (i >= 0 && i < totalAgendamentos) {
    for (int j = i; j < totalAgendamentos - 1; j++) agendamentos[j] = agendamentos[j + 1];
    totalAgendamentos--;
    salvarEEPROM();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  carregarEEPROM();
  carregarWiFiConfig();

  for (int i = 0; i < 8; i++) {
    pinMode(relePins[i], OUTPUT);
    digitalWrite(relePins[i], HIGH);
  }

  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 10) {
    delay(1000);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFalha ao conectar. Entrando em modo AP...");
    iniciarModoAP();
    return;
  }

  Serial.println("\nWiFi conectado: " + WiFi.localIP().toString());
  timeClient.begin();

  server.on("/", handleRoot);
  for (int i = 0; i < 8; i++) {
    server.on( ("/on" + String(i)).c_str(), [i]() {
      releStates[i] = true;
      digitalWrite(relePins[i], LOW);
      server.sendHeader("Location", "/");
      server.send(303);
    });
    server.on( ("/off" + String(i)).c_str(), [i]() {
      releStates[i] = false;
      digitalWrite(relePins[i], HIGH);
      server.sendHeader("Location", "/");
      server.send(303);
    });
  }
  server.on("/add", HTTP_POST, adicionarAgendamento);
  server.on("/del", removerAgendamento);
  server.begin();
}

void loop() {
  server.handleClient();
  verificarAgendamentos();
}
