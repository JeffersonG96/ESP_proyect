#include <Arduino.h>
#include "Colors.h"
#include "IoTicosSplitter.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Wire.h>
#include<stdlib.h>

using namespace std;

//lib MPU6050
#include <fall_proyecto_inferencing.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

//libMAX30100
#include <MAX30100_PulseOximeter.h>

//variables para MAX30100
PulseOximeter max30100;
// uint8_t addres_Max30100 = 0x57;
int counter = 5;
float getHeart=0.0;
float getSpo2=0.0;
float averageHeart =0.0;
float averageSpo2 =0.0;
float send_data_max30100_heart=0.0;
float send_data_max30100_spo2=0.0;

//crear una tarea para loop2
TaskHandle_t Task1;

Adafruit_MPU6050 mpu;
//variable para enviar data entre nucleo
int ready_send_data_MPU = 0;
int send_counter_mpu = 0;
long time_last_mpu =0;

//variable para edge//
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
size_t feature_ix = 0;

//variables para server
String email = "test1@test.com";
String password = "123456";
String webApi = "http://192.168.100.160:3000/api/login/";
String webApiFind = "http://192.168.100.160:3000/api/login/find";
const char* mqttServer = "192.168.100.160";
int mqttPort = 1883;

//pines
#define led 2

//credenciales WiFi
const char *wifi_ssid = "TELECOM_Pato";
const char *wifi_pass = "P@to1984$";

//Variables globales
long lastReconnect;
WiFiClient espClient;
PubSubClient client(espClient);

long varsLastS[20];

DynamicJsonDocument mqttDataDoc(2048);
StaticJsonDocument<1024> dataServer;

//funciones
void conectar();
bool SendDataServer();
bool getMqttCredentials();
void checkMqttConnection();
bool reconnect();
void procesarSensores();
void sendData();

//LOOP2
void loop2(void *parameter){
  for (;;){

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  features[feature_ix++] = a.acceleration.x;
  features[feature_ix++] = a.acceleration.y;
  features[feature_ix++] = a.acceleration.z;
 
  if(feature_ix == EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE){
    signal_t signal;
    ei_impulse_result_t result;
    int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  if (err != 0) {
        ei_printf("Failed to create signal from buffer (%d)\n", err);
        return;
    }

  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, true);
  if(res != 0) return;

    // print the predictions
    
    ei_printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
        result.timing.dsp, result.timing.classification, result.timing.anomaly);
    ei_printf(": \n");

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf(" %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
        if(result.classification[ix].value > 0.98){
          if(result.classification[ix].label == "fall"){
            ready_send_data_MPU = 1;
          }
          if(result.classification[ix].label == "AVD"){
            // Serial.println("*****-AVD-****");
            // ready_send_data_MPU = false;
          }
        }
    }

#if EI_CLASSIFIER_HAS_ANOMALY == 1
ei_printf("    anomaly score: %.3f\n", result.anomaly);
#endif
    feature_ix = 0;
  }


  max30100.update();
  getHeart = max30100.getHeartRate();
  getSpo2 = max30100.getSpO2();

}} //for  //loop2

void onBeatDetected(){ 
    if(getHeart < 50 || getSpo2 < 50 ){
        return;
      }

      if(counter > 0){
        averageHeart = ((averageHeart + getHeart)/2);
        averageSpo2 = ((averageSpo2 + getSpo2)/2);
        counter = counter -1;
      }

      if(counter == 0){
      send_data_max30100_heart=averageHeart;
      send_data_max30100_spo2=averageSpo2;
      counter = 20;
      }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Wire.begin();

  pinMode(led, OUTPUT);
  conectar();
  SendDataServer();
  // getMqttCredentials();
  //?MPU6050
  Serial.println(mpu.begin() ? F("IMU iniciado correctamente") : F("Error al iniciar IMU"));
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);
   //?Iniciar parametros de task2
  xTaskCreatePinnedToCore(loop2,"Task_1",5000,NULL,1,&Task1,0);
  //?MAX30100
  Serial.println(max30100.begin() ? F("MAX30100 iniciado correctamente") : F("Error al iniciar MAX30100"));
  max30100.setIRLedCurrent(MAX30100_LED_CURR_37MA);
  max30100.setOnBeatDetectedCallback(onBeatDetected);

  delay(100);
}

void loop() {
  //TODO: funciones de MAX30100
  
  checkMqttConnection();
  procesarSensores();
  sendData();
  // serializeJsonPretty(mqttDataDoc, Serial);
  // delay(8000);
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

  //*obtener BPM
  char char_array1[20];
  snprintf(char_array1,sizeof(char_array1),"%0.1f",send_data_max30100_heart);
  mqttDataDoc["variables"][1]["variableName"] = "heart";
  mqttDataDoc["variables"][1]["frecuencia"] = 10;
  mqttDataDoc["variables"][1]["last"]["value"] = char_array1;
  mqttDataDoc["variables"][1]["last"]["save"] =1;
  // mqttDataDoc["variables"][1]["last"]["value"] = 

  //*spo2
  char char_array2[20];
  snprintf(char_array2, sizeof(char_array2),"%0.1f",send_data_max30100_spo2);
  mqttDataDoc["variables"][2]["variableName"] = "spo2";
  mqttDataDoc["variables"][2]["frecuencia"] = 10;
  mqttDataDoc["variables"][2]["last"]["value"] = char_array2;
  mqttDataDoc["variables"][2]["last"]["save"] =1;

  //*status ENVIAR ESTABILIDAD DEL USUARIO
if(ready_send_data_MPU == 0){
  mqttDataDoc["variables"][3]["variableName"] = "status";
  mqttDataDoc["variables"][3]["frecuencia"] = 10; //*frecuencia a enviar el dato 
  mqttDataDoc["variables"][3]["last"]["value"] = "Normal";
  mqttDataDoc["variables"][3]["last"]["save"] = 1;
    mqttDataDoc["variables"][3]["last"]["alarm"] = 0;
  time_last_mpu = millis();
  digitalWrite(led,LOW);
  send_counter_mpu = 0;
}

if (ready_send_data_MPU == 1){
  long now = millis();
  mqttDataDoc["variables"][3]["variableName"] = "status";
  mqttDataDoc["variables"][3]["frecuencia"] = 30; //*frecuencia al enviar el dato 
  mqttDataDoc["variables"][3]["last"]["value"] = "En el piso";

  int frec = mqttDataDoc["variables"][3]["frecuencia"];
  if(now - time_last_mpu > frec*1000){
  ready_send_data_MPU = 0;
  send_counter_mpu = 0;
  }
  if(send_counter_mpu == 0){
  mqttDataDoc["variables"][3]["last"]["alarm"] = 1;
  mqttDataDoc["variables"][3]["last"]["save"] = 1;
                //100 - 30 = 70
  varsLastS[3] = millis() - frec*1000;
  send_counter_mpu = 1;
  digitalWrite(led,HIGH);
  }
} 

}

void sendData(){

  long now = millis();

  for(int i = 0; i<mqttDataDoc["variables"].size(); ++i){

  int frec = mqttDataDoc["variables"][i]["frecuencia"];
     //110 - 100 = 10
  if((now - varsLastS[i]) > frec * 1000){
   varsLastS[i] = millis();

  String uid = mqttDataDoc["uid"];
  String variableName =mqttDataDoc["variables"][i]["variableName"];
  String topic = uid +"/" + variableName + "/sdata";

  String toSend = "";
  serializeJson(mqttDataDoc["variables"][i]["last"], toSend);

  client.publish(topic.c_str(),toSend.c_str());

  } }
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
Serial.println(" ");
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



