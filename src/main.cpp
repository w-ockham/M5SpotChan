#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include <AudioOutput.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include "AudioOutputM5Speaker.h"
#include <AudioFileSourcePROGMEM.h>
#include <google-tts.h>
#include <ServoEasing.hpp> // https://github.com/ArminJo/ServoEasing
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <deque>
#include "JSONConfig.h"
#include "CloudTTSClient.h"
#include "MQTTmessage.h"

#define USE_SERVO
#ifdef USE_SERVO
#if defined(ARDUINO_M5STACK_Core2)
//  #define SERVO_PIN_X 13  //Core2 PORT C
//  #define SERVO_PIN_Y 14
#define SERVO_PIN_X 33 // Core2 PORT A
#define SERVO_PIN_Y 32
#elif defined(ARDUINO_M5STACK_FIRE)
#define SERVO_PIN_X 21
#define SERVO_PIN_Y 22
#elif defined(ARDUINO_M5Stack_Core_ESP32)
// #define SERVO_PIN_X 21 // for Core PORT A
// #define SERVO_PIN_Y 22
#define SERVO_PIN_X 17 // for Core PORT C
#define SERVO_PIN_Y 16
#endif
#endif

DynamicJsonDocument config(1024);
JSONConfig configfile = JSONConfig((char *)"spotchan", config);

CloudTTSClient *tts_cloud;
CloudTTSClient::TTS_LANG LANG_CODE = CloudTTSClient::EN_US;
WiFiClient _mqtt;
PubSubClient mqtt_client(_mqtt);

/// set M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;
using namespace m5avatar;
Avatar avatar;
const Expression expressions_table[] = {
    Expression::Neutral,
    Expression::Happy,
    Expression::Sleepy,
    Expression::Doubt,
    Expression::Sad,
    Expression::Angry};

/// set M5Speaker virtual channel (0-7)
// static constexpr uint8_t m5spk_virtual_channel = 0;
static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
AudioGeneratorMP3 *mp3;
AudioFileSourceBuffer *buff = nullptr;
AudioFileSourcePROGMEM *file = nullptr;

bool mp3stop = true;
bool mute_spot = true;

#ifdef USE_SERVO
#define START_DEGREE_VALUE_X 90
#define START_DEGREE_VALUE_Y 90
ServoEasing servo_x;
ServoEasing servo_y;
#endif

void lipSync(void *args)
{
  float gazeX, gazeY;
  int level = 0;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();
  for (;;)
  {
    level = abs(*out.getBuffer());
    if (level < 100)
      level = 0;
    if (level > 15000)
    {
      level = 15000;
    }
    float open = (float)level / 15000.0;
    avatar->setMouthOpenRatio(open);
    avatar->getGaze(&gazeY, &gazeX);
    avatar->setRotation(gazeX * 5);
    delay(50);
  }
}

bool servo_home = true;

void servo(void *args)
{
  float gazeX, gazeY;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();
  for (;;)
  {
#ifdef USE_SERVO
    if (!servo_home)
    {
      avatar->getGaze(&gazeY, &gazeX);
      servo_x.setEaseTo(START_DEGREE_VALUE_X + (int)(15.0 * gazeX));
      if (gazeY < 0)
      {
        int tmp = (int)(10.0 * gazeY);
        if (tmp > 10)
          tmp = 10;
        servo_y.setEaseTo(START_DEGREE_VALUE_Y + tmp);
      }
      else
      {
        servo_y.setEaseTo(START_DEGREE_VALUE_Y + (int)(10.0 * gazeY));
      }
    }
    else
    {
      //     avatar->setRotation(gazeX * 5);
      //     float b = avatar->getBreath();
      servo_x.setEaseTo(START_DEGREE_VALUE_X);
      //     servo_y.setEaseTo(START_DEGREE_VALUE_Y + b * 5);
      servo_y.setEaseTo(START_DEGREE_VALUE_Y);
    }
    synchronizeAllServosStartAndWaitForAllServosToStop();
#endif
    delay(50);
  }
}

void Servo_setup()
{
#ifdef USE_SERVO
  if (servo_x.attach(SERVO_PIN_X, START_DEGREE_VALUE_X, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE))
  {
    Serial.println("Error attaching servo x");
  }
  if (servo_y.attach(SERVO_PIN_Y, START_DEGREE_VALUE_Y, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE))
  {
    Serial.println("Error attaching servo y");
  }
  servo_x.setEasingType(EASE_QUADRATIC_IN_OUT);
  servo_y.setEasingType(EASE_QUADRATIC_IN_OUT);
  setSpeedForAllServos(30);

  servo_x.setEaseTo(START_DEGREE_VALUE_X);
  servo_y.setEaseTo(START_DEGREE_VALUE_Y);
  synchronizeAllServosStartAndWaitForAllServosToStop();
#endif
}

void google_tts(String &text, CloudTTSClient::TTS_LANG lang)
{
  uint len;

  len = tts_cloud->textToSpeach(lang, String("<speak>") + text + String("</speak>"));
  if (len > 0)
  {
    file = new AudioFileSourcePROGMEM(tts_cloud->buffer, len);
    Serial.println("mp3 begin");
    mp3stop = false;
    mp3->begin(file, &out);
  }
  else
  {
    Serial.println("Google TTS Error");
    servo_home = true;
  }
}

struct box_t
{
  int x;
  int y;
  int w;
  int h;
  int touch_id = -1;

  void setupBox(int x, int y, int w, int h)
  {
    this->x = x;
    this->y = y;
    this->w = w;
    this->h = h;
  }
  bool contain(int x, int y)
  {
    return this->x <= x && x < (this->x + this->w) && this->y <= y && y < (this->y + this->h);
  }
};
static box_t box_servo;
static box_t box_stt;

String import_key;

void load_config()
{
  Serial.println("Press B to load config.");
  M5.Lcd.println("Press B to load config.");
  sleep(3);
  M5.update();
  if (M5.BtnB.wasPressed() || !configfile.loadNVM())
  {
    Serial.println("Read config from SD.");
    M5.Lcd.println("Read config from SD.");
    if (!configfile.readFromSD("/spotchan.json"))
    {
      Serial.println("SD file load failed.");
      M5.Lcd.println("SD file load failed.");
      sleep(60);
      ESP.restart();
    }
    const char *s = config["importkey"];
    import_key = s;
    configfile.saveNVM();
    Serial.println("Save config to NVM.");
    M5.Lcd.println("Save config to NVM.");
    sleep(3);
  }
  else
    import_key = "";

  int volume = config["volume"];
  M5.Speaker.setVolume(volume);
}

void Wifi_setup()
{
  // 前回接続時情報で接続する
  while (WiFi.status() != WL_CONNECTED)
  {
    M5.Display.print(".");
    Serial.print(".");
    delay(500);
    // 10秒以上接続できなかったら抜ける
    if (10000 < millis())
    {
      break;
    }
  }
  M5.Display.println("");
  Serial.println("");
  // 未接続の場合にはSmartConfig待受
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.mode(WIFI_STA);
    WiFi.beginSmartConfig();
    M5.Display.println("Waiting for SmartConfig");
    Serial.println("Waiting for SmartConfig");
    while (!WiFi.smartConfigDone())
    {
      delay(500);
      M5.Display.print("#");
      Serial.print("#");
      // 30秒以上接続できなかったら抜ける
      if (30000 < millis())
      {
        Serial.println("");
        Serial.println("Reset");
        ESP.restart();
      }
    }
    // Wi-fi接続
    M5.Display.println("");
    Serial.println("");
    M5.Display.println("Waiting for WiFi");
    Serial.println("Waiting for WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      M5.Display.print(".");
      Serial.print(".");
      // 60秒以上接続できなかったら抜ける
      if (60000 < millis())
      {
        Serial.println("");
        Serial.println("Reset");
        ESP.restart();
      }
    }
  }
  M5.Display.println("Done.");
  Serial.println("Done.");
}

MQTTMessage *mqtt_message;

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  mqtt_message->enqueue(topic, payload, length);
}

String clientUUID = "";

void mqtt_reconnect()
{
  // Loop until we're reconnected
  while (!mqtt_client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "Spotchan-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    const char *mqtt_user = config["mqtt_user"];
    const char *mqtt_passwd = config["mqtt_passwd"];
    if (mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_passwd))
    {
      Serial.println("connected");
      JsonArray subsc = config["subscription"].as<JsonArray>();
      for (int i = 0; i < subsc.size(); i++)
      {
        String topic = subsc[i];
        if (clientUUID != "" && topic.indexOf("pota") >= 0)
          topic = clientUUID + "/" + topic;
        mqtt_client.subscribe(topic.c_str());
        Serial.println(topic);
      }
      Serial.println("mqtt subscription done.");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup()
{
  auto cfg = M5.config();

  cfg.external_spk = true;
  /// use external speaker (SPK HAT / ATOMIC SPK)
  // cfg.external_spk_detail.omit_atomic_spk = true; // exclude ATOMIC SPK
  // cfg.external_spk_detail.omit_spk_hat    = true; // exclude SPK HAT
  cfg.internal_mic = true;

  M5.begin(cfg);
  { /// custom setting
    auto spk_cfg = M5.Speaker.config();
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
    spk_cfg.sample_rate = 48000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    M5.Speaker.config(spk_cfg);
  }

  M5.Speaker.begin();
  M5.Lcd.setTextSize(2);

  load_config();
  Servo_setup();

  Serial.println("Connecting to WiFi");
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  const char *wifi_ssid = config["wifi_ssid"];
  const char *wifi_passwd = config["wifi_passwd"];

  if (strcmp(wifi_ssid, "") != 0)
  {
    WiFi.begin(wifi_ssid, wifi_passwd);
    M5.Lcd.printf("Connecting %s", wifi_ssid);
    Serial.printf("Connecting %s", wifi_ssid);
    Wifi_setup();
    M5.Lcd.println("\nConnected");
    Serial.println("\nConnected");
  }
  else
    WiFi.begin();

  Serial.println(WiFi.localIP());
  M5.Lcd.println(WiFi.localIP());

  const char *mqtt_broker = config["mqtt_broker"];
  int mqtt_port = config["mqtt_port"];

  tts_cloud = new CloudTTSClient();

  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setBufferSize(512);
  mqtt_client.setCallback(mqtt_callback);
  mqtt_message = new MQTTMessage();

  if (import_key != "")
  {
    Serial.println("Load client UUID from myACT.");
    M5.Lcd.println("Load client UUID from myACT.");
    clientUUID = mqtt_message->getUUID(import_key);
    if (clientUUID != "")
    {
      config["import_key"] = "";
      config["clientUUID"] = clientUUID.c_str();
      configfile.saveNVM();
      Serial.println("Save new clientUUID to NVM.");
      M5.Lcd.println("Save new clientUUID to NVM.");
    }
    else
    {
      Serial.printf("Import failed. key=%s\n", import_key);
      M5.Lcd.printf("Import failed. key=%s\n", import_key);
      sleep(10);
    }
    import_key = "";
  }
  else
  {
    Serial.println("Load client UUID from NVM.");
    M5.Lcd.println("Load client UUID from NVM.");
    const char *s = config["clientUUID"];
    clientUUID = s;
  }

  sleep(2);
  audioLogger = &Serial;
  mp3 = new AudioGeneratorMP3();

  avatar.init();
  avatar.addTask(lipSync, "lipSync");
  avatar.addTask(servo, "servo");
  avatar.setSpeechFont(&fonts::efontJA_12);
  box_servo.setupBox(80, 120, 80, 80);
  box_stt.setupBox(0, 0, M5.Display.width(), 60);
}

String separator_tbl[7] = {"|", "~", "\n", ""};

int search_separator(String text)
{
  int i = 0;
  int dotIndex_min = 1000;
  int dotIndex;
  while (separator_tbl[i] != "")
  {
    dotIndex = text.indexOf(separator_tbl[i++]);
    if ((dotIndex != -1) && (dotIndex < dotIndex_min))
      dotIndex_min = dotIndex;
  }
  if (dotIndex_min == 1000)
    return -1;
  else
    return dotIndex_min;
}

const char *text1 = "みなさんこんにちは,私の名前はスポットチャンです,よろしくね.";
const char *text2 = "Hello everyone,my name is Spot Chan,nice to meet you.";
String speech_text_buffer = "";
String last_spot_summary = "";
String last_spot_all = "";
CloudTTSClient::TTS_LANG last_lang;

void say(String &str)
{
  int dotIndex;
  String sentence;

  speech_text_buffer = str;
  dotIndex = search_separator(speech_text_buffer);
  if (dotIndex != -1)
  {
    sentence = speech_text_buffer.substring(0, dotIndex);
    speech_text_buffer = speech_text_buffer.substring(dotIndex + 1);
  }
  else
  {
    sentence = speech_text_buffer;
    speech_text_buffer = "";
  }
  avatar.setExpression(Expression::Happy);
  google_tts(sentence, LANG_CODE);
  avatar.setExpression(Expression::Neutral);
}

void loop()
{
  M5.update();

  if (M5.BtnA.wasPressed())
  {
    M5.Speaker.tone(1000, 100);
    String tmp;
    if (mute_spot)
    {
      if (LANG_CODE == CloudTTSClient::JA_JP)
      {
        tmp = "スポット開始します。";
      }
      else
      {
        tmp = "Start spotting.";
      }
      avatar.setSpeechText("Start spotting.");
    }
    else
    {
      if (LANG_CODE == CloudTTSClient::JA_JP)
      {
        tmp = "スポット停止します。";
      }
      else
      {
        tmp = "Stop spotting.";
      }
      avatar.setSpeechText("Stop spotting.");
    }
    mute_spot = !mute_spot;
    if (!mp3->isRunning())
    {
      avatar.setExpression(Expression::Happy);
      google_tts(tmp, LANG_CODE);
      avatar.setExpression(Expression::Neutral);
    }
  }

  if (M5.BtnB.wasPressed())
  {
    M5.Speaker.tone(1000, 100);
    avatar.setSpeechText(last_spot_summary.c_str());
    LANG_CODE = last_lang;
    say(last_spot_all);
  }

  if (M5.BtnC.wasPressed())
  {
    String tmp;
    M5.Speaker.tone(1000, 100);
    avatar.setSpeechText("Hello");
    if (LANG_CODE == CloudTTSClient::JA_JP)
    {
      tmp = text2;
      LANG_CODE = CloudTTSClient::EN_US;
    }
    else
    {
      tmp = text1;
      LANG_CODE = CloudTTSClient::JA_JP;
    }
    avatar.setExpression(Expression::Happy);
    google_tts(tmp, LANG_CODE);
    avatar.setExpression(Expression::Neutral);
  }

  mqtt_client.loop();
  if (mqtt_message->available() > 0)
  {
    String newspot;
    bool has_JA;

    if (mute_spot)
    {
      mqtt_message->dequeue(newspot, last_spot_all, last_spot_summary, has_JA);
    }
    else if (mp3stop)
    {
      if (mqtt_message->dequeue(newspot, last_spot_all, last_spot_summary, has_JA))
      {
        Serial.println("Message Error\n");
        sleep(10);
        return;
      };

      if (has_JA)
        LANG_CODE = CloudTTSClient::JA_JP;
      else
        LANG_CODE = CloudTTSClient::EN_US;

      M5.Speaker.tone(1000, 100);
      last_lang = LANG_CODE;
      avatar.setSpeechText(last_spot_summary.c_str());
      say(newspot + last_spot_all);
      servo_home = false;
    }
  }

  if (mp3->isRunning())
  {
    if (!mp3->loop())
    {
      mp3->stop();
      if (file != nullptr)
      {
        delete file;
        file = nullptr;
        delete mp3;
        mp3 = new AudioGeneratorMP3();
      }
      Serial.println("mp3 stop");
      servo_home = true;
      if (speech_text_buffer != "")
      {
        say(speech_text_buffer);
        servo_home = false;
      }
      else
      {
        mp3stop = true;
        avatar.setSpeechText("");
      }
    }
  }
  else
  {
    if (!mqtt_client.connected())
      mqtt_reconnect();
  }
}