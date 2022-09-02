#include <Arduino.h>
#include "Colors.h"
#include "IoTicosSplitter.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

//variables para server
String email = "test1@test.com";
String password = "123456";
String webApi = "http://192.168.100.160:3000/api/login/";
String webApiFind = "http://192.168.100.160:3000/api/login/find";
const char* mqttServer = "192.168.100.160";
int mqttPort = 1883;

// //
// String responseBody;

//pines
#define led 2

//credenciales WiFi
const char *wifi_ssid = "TELECOM_Pato";
const char *wifi_pass = "P@to1984$";

//funciones
void conectar();
bool SendDataServer();
bool getMqttCredentials();
void checkMqttConnection();
bool reconnect();
void procesarSensores();
void sendData();


//Variables globales
long lastReconnect;
WiFiClient espClient;
PubSubClient client(espClient);

DynamicJsonDocument mqttDataDoc(1024);
StaticJsonDocument<512> dataServer;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  pinMode(led, OUTPUT);
  conectar();
  SendDataServer();
  // getMqttCredentials();


}

void loop() {
  checkMqttConnection();

  procesarSensores();
  sendData();

  // serializeJsonPretty(mqttDataDoc, Serial);
  // delay(10000);

}

int prev_temp = 0;
int prev_hum = 0;

//TODO Funciones para sensores
void procesarSensores(){

  //obtener temp
  int temp = random(1,100);
  mqttDataDoc["variables"][0]["variableName"] = "temp";
  mqttDataDoc["variables"][0]["frecuencia"] = 10; //*frecuencia a enviar el dato
  mqttDataDoc["variables"][0]["last"]["value"] = temp;
  int dif = temp - prev_temp;
  if (dif < 0) {dif *= -1;}

  if (dif >= 20) {
    mqttDataDoc["variables"][0]["last"]["save"] = 1;
  }else{
    mqttDataDoc["variables"][0]["last"]["save"] = 0;
  }
  prev_temp = temp;

  //obtener BPM
  int BPM = random(1,100);
  mqttDataDoc["variables"][1]["variableName"] = "heart";
  mqttDataDoc["variables"][1]["frecuencia"] = 10;
  mqttDataDoc["variables"][1]["last"]["value"] = BPM;
    dif = BPM - prev_hum;
  if (dif < 0) {dif *= -1;}

  if (dif >= 20) {
    mqttDataDoc["variables"][1]["last"]["save"] =1;
  }else{
    mqttDataDoc["variables"][1]["last"]["save"] = 0;
  }
  prev_hum = BPM;

  //*spo2
    int RES = random(1,100);
  mqttDataDoc["variables"][2]["variableName"] = "spo2";
  mqttDataDoc["variables"][2]["frecuencia"] = 10;
  mqttDataDoc["variables"][2]["last"]["value"] = RES;
    dif = RES- prev_hum;
  if (dif < 0) {dif *= -1;}

  if (dif >= 20) {
    mqttDataDoc["variables"][2]["last"]["save"] =1;
  }else{
    mqttDataDoc["variables"][2]["last"]["save"] = 0;
  }
  prev_hum = RES;

  //*status
    int status = random(1,100);
  mqttDataDoc["variables"][3]["variableName"] = "status";
  mqttDataDoc["variables"][3]["frecuencia"] = 10; //*frecuencia a enviar el dato
  mqttDataDoc["variables"][3]["last"]["value"] = status;
    dif = status - prev_temp;
  if (dif < 0) {dif *= -1;}

  if (dif >= 20) {
    mqttDataDoc["variables"][3]["last"]["save"] = 0;
  }else{
    mqttDataDoc["variables"][3]["last"]["save"] = 0;
  }
  prev_temp = status;


}

long varsLastS[20];
void sendData(){

  long now = millis();

  for(int i = 0; i<mqttDataDoc["variables"].size(); ++i){

    int frec = mqttDataDoc["variables"][i]["frecuencia"];

  if((now - varsLastS[i]) > frec * 1000){
   varsLastS[i] = millis();

  String uid = mqttDataDoc["uid"];
  String variableName =mqttDataDoc["variables"][i]["variableName"];
  String topic = uid +"/" + variableName + "/sdata";
  // Serial.println(topic);

  String toSend = "";
  serializeJson(mqttDataDoc["variables"][i]["last"], toSend);

  client.publish(topic.c_str(),toSend.c_str());

  }
  }

}


//TODO Funciones propias de la ESP y server
//?COnectar wiFI
void conectar(){
  WiFi.begin(wifi_ssid,wifi_pass);
  Serial.print("Espere conectando..." );
  int counter = 0;
  while ( WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
    counter ++;
    if(counter > 15){
      Serial.println("Conexión Fallida :( ");
      delay(2000);
      ESP.restart();
    }
  }
  Serial.println("\n Conectado Correctamente");
  Serial.print("IP => ");
  Serial.print(WiFi.localIP());
}
//?Conectar al servidor
bool SendDataServer(){
  Serial.print(" \n ***ENVIANDO CREDENCIALES AL SERVIDOR***");
  delay(1000);
  //preparo un objeto con las credenciales para enviar
  HTTPClient http ;
  String responseBody;

  StaticJsonDocument<512> buff;
  String jsonParams;

  buff["email"] = email;
  buff["password"] = password;

  serializeJson(buff, jsonParams);
  Serial.println();
  Serial.println(jsonParams);

  http.begin(webApi);
  http.addHeader("Content-Type","application/json");
  int statusCode = http.POST(jsonParams);
  delay(500);

  Serial.println(statusCode);

  if (statusCode != 200){
    Serial.println("##### ERROR EN LOS DATOS INGRESADOS #####");
    http.end(); //finaliza la petición
    ESP.restart();
    return false;
  }

  if (statusCode == 200){
    responseBody = http.getString();
        delay(500);

DeserializationError error = deserializeJson(dataServer, responseBody);
// DeserializationError error = deserializeJson(mqttDataDoc, responseBody);

if (error) {
  Serial.print("deserializeJson() failed: ");
  Serial.println(error.c_str());
  return false;
}

JsonObject usuario = dataServer["usuario"];
const char* usuario_uid = usuario["uid"]; // "62d9c65473a36709d8a9326f"


serializeJsonPretty(dataServer, Serial);
http.end();
return true;
  }


}

bool getMqttCredentials(){
  Serial.print(" \n ###### ENVIANDO UID ######");
  delay(1000);
  //preparo un objeto con las credenciales para enviar
  HTTPClient http ;
  String responseBody;

  StaticJsonDocument<512> buff;
  String jsonParams;

  JsonObject usuario = dataServer["usuario"];
  String uid = usuario["uid"];
  String token = dataServer["token"];
  Serial.printf(token.c_str());

  buff["uid"] = uid;
  // buff["password"] = password;

  serializeJson(buff, jsonParams);

  http.begin(webApiFind);
  http.addHeader("Content-Type","application/json");
  http.addHeader("x-token",token);
  int statusCode = http.POST(jsonParams);
  delay(500);

  Serial.println(statusCode);

  if (statusCode != 200){
    Serial.println("##### ERROR EN LOS DATOS INGRESADOS #####");
    http.end(); //finaliza la petición
    ESP.restart();
    return false;
  }

  if (statusCode == 200){
    responseBody = http.getString();
    delay(2000);

  // StaticJsonDocument<200> filter;
  // filter["usuario"] = true;

DeserializationError error = deserializeJson(mqttDataDoc, responseBody);
// , DeserializationOption::Filter(filter)

if (error) {
  Serial.print("deserializeJson() failed: ");
  Serial.println(error.c_str());
  return false;
}

// JsonObject usuario = mqttDataDoc["usuario"];
// const char* usuario_uid = usuario["uid"]; // "62d9c65473a36709d8a9326f"

serializeJsonPretty(mqttDataDoc, Serial);
http.end();
return true;
  }
}

void checkMqttConnection(){
if(WiFi.status() != WL_CONNECTED){
  Serial.println("CONEXIÓN WIFI NO DISPONIBLE");
  Serial.println("Reiniciando... ESP");
  delay(15000);
  ESP.restart();

}

if(!client.connected()){
  long now = millis();
  if (now - lastReconnect > 5000){
    lastReconnect = millis();
    if(reconnect()){
      lastReconnect = 0;
    }
  }

} else { client.loop(); }
}

bool reconnect(){
  if(!getMqttCredentials()){
    Serial.println("");
    Serial.println("Error en obtener credenciales para MQTT");
    delay(5000);
    ESP.restart();
  }

  client.setServer(mqttServer, mqttPort);
  Serial.println("Intentando Conectar al Broker MQTT (EMQX)");

  const char* userName = mqttDataDoc["username"];
  const char* password = mqttDataDoc["password"];


  JsonObject usuario = dataServer["usuario"];
  String uid = usuario["uid"];
  String topic = uid + "/+/sdata";
  String dId = "device_" + uid;


  if(client.connect(dId.c_str(), userName, password)){
   Serial.println("");
   Serial.println("CONECTADO AL BROKER MQTT");
  if(client.subscribe(topic.c_str())){
    Serial.println("Subscrito: " + topic);
  }
  return true;

  } else {
    Serial.print("No se logro conectar al Broker MQTT");
    return false;
  }
  return true;
}



