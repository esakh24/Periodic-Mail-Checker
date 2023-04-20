#include <UbiBuilder.h>
#include <UbiConstants.h>
#include <Ubidots.h>
#include <UbiHttp.h>
#include <UbiProtocol.h>
#include <UbiProtocolHandler.h>
#include <UbiTcp.h>
#include <UbiTypes.h>
#include <UbiUdp.h>
#include <UbiUtils.h>

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>


const char* UBIDOTS_TOKEN = "...";//enter ubidots token here
const char* DEVICE_LABEL_TO_RETRIEVE_VALUES_FROM = "...";  // Replace with your device label
const char* VARIABLE_LABEL_TO_RETRIEVE_VALUES_FROM = "...";
Ubidots ubidots(UBIDOTS_TOKEN, UBI_TCP);
const char* ssid = "..."; //enter ssid    
const char* password = "...";//enter password
// Initialize Telegram BOT
String CHAT_ID = "...";//enter your chat_id
String context="";
String BOTtoken = "...";//enter the bot token 
bool sendPhoto = false;
long currentTime;
WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOTtoken, clientTCP);

#define FLASH_LED_PIN 4
bool flashState = LOW;
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
unsigned long  interval= 20 ;  
float intervalminutes;
//CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


void configInitCamera(){
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(10);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  delay(10);
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  // Drop down frame size for higher initial frame rate
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_CIF);  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA
}


String sendPhotoTelegram() {
  const char* myDomain = "api.telegram.org";
  String getAll = "";
  String getBody = "";
  digitalWrite(FLASH_LED_PIN, HIGH);
  Serial.println("Change flash LED state");
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();  
  digitalWrite(FLASH_LED_PIN, LOW);
    Serial.println("Change flash LED state");
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
    return "Camera capture failed";
  }  
  
  Serial.println("Connect to " + String(myDomain));


  if (clientTCP.connect(myDomain, 443)) {
    Serial.println("Connection successful");
    
    String head = "--Electro\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + CHAT_ID + "\r\n--Electro\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--Electro--\r\n";

    uint16_t imageLen = fb->len;
    uint16_t extraLen = head.length() + tail.length();
    uint16_t totalLen = imageLen + extraLen;
  
    clientTCP.println("POST /bot"+BOTtoken+"/sendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(myDomain));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=Electro");
    clientTCP.println();
    clientTCP.print(head);
  
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0;n<fbLen;n=n+1024) {
      if (n+1024<fbLen) {
        clientTCP.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        clientTCP.write(fbBuf, remainder);
      }
    }  
    
    clientTCP.print(tail);
    
    esp_camera_fb_return(fb);
    
    int waitTime = 10000;   // timeout 10 seconds
    long startTimer = millis();
    boolean state = false;
    
    while ((startTimer + waitTime) > millis()){
      Serial.print(".");
      delay(100);      
      while (clientTCP.available()) {
        char c = clientTCP.read();
        if (state==true) getBody += String(c);        
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } 
        else if (c != '\r')
          getAll += String(c);
        startTimer = millis();
      }
      if (getBody.length()>0) break;
    }
    clientTCP.stop();
    Serial.println(getBody);
  }
  else {
    getBody="Connected to api.telegram.org failed.";
    Serial.println("Connected to api.telegram.org failed.");
  }
  return getBody;
}
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason){
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

unsigned long getSeconds(int hr, int days){
  unsigned long val = hr*60+ days*60*24;
  return val;
  }
  
void setup(){
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  // Init Serial Monitor
  Serial.begin(115200);
  ubidots.wifiConnect(ssid, password);
  // Set LED Flash as output
  pinMode(FLASH_LED_PIN, OUTPUT);
  //pinMode(GPIO_NUM_4, INPUT_PULLUP);
  digitalWrite(FLASH_LED_PIN, flashState);
  // Config and init the camera
  configInitCamera();
  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP()); 
  bool stat = ubidots.get(DEVICE_LABEL_TO_RETRIEVE_VALUES_FROM, "status");
  Serial.print("Status retrieved from ubidots : ");
  Serial.println(stat);
  if(stat==false){
    //go to sleep for 6 hrs 
    Serial.print("go to sleep for 6 hrs ");
    uint64_t interval_in_seconds = 6*60*60;
    //Serial.println("interval in seconds "+(String)interval_in_seconds); 
    uint64_t  val = interval_in_seconds * uS_TO_S_FACTOR;
    //Serial.println("mult factor " + String(uS_TO_S_FACTOR) + " Seconds");
    //Serial.println("final val" + String(val));
    esp_sleep_enable_timer_wakeup(val);        
    Serial.println("Setup ESP32 to sleep for every " + String(val) + " microSeconds");
    delay(400);
    Serial.flush(); 
    esp_deep_sleep_start();
  }
  intervalminutes = ubidots.get(DEVICE_LABEL_TO_RETRIEVE_VALUES_FROM, "interval");
 
  if (intervalminutes != ERROR_VALUE) {
    Serial.print("Interval in minutes ");
    Serial.println(intervalminutes);
  }

  if(intervalminutes==0){   
    Serial.println("using default interval 2 minutes");
    intervalminutes =2 ;
  }

  unsigned long interval_in_seconds = intervalminutes*60;
//  Serial.println("interval in seconds "+(String)interval_in_seconds); 
  unsigned long val = interval_in_seconds * uS_TO_S_FACTOR;
//  Serial.println("mult factor " + String(uS_TO_S_FACTOR) + " Seconds");
//  Serial.println("final val" + String(val));
  esp_sleep_enable_timer_wakeup(val);        
  Serial.println("Setup ESP32 to sleep for every " + String(val) + " microSeconds");
  print_wakeup_reason();
}

void loop() {
    Serial.println("Preparing photo");
    sendPhotoTelegram(); 
    Serial.println("Going to sleep now");
    delay(1000);
    Serial.flush(); 
    esp_deep_sleep_start();
}
