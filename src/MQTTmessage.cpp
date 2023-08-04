#include "MQTTmessage.h"

String MQTTMessage::getUUID(String &share_code)
{
  secure_client.setCACert(root_ca_keyserv);
  secure_client.setTimeout(10000);
  if (!secure_client.connect(keyserv, 443))
  {
    Serial.println("Connection failed!");
    return String("");
  }
  String HttpHeader = String("GET /api/myact/potalog?command=IMPORT&sharekey=") + share_code + String(" HTTP/1.1\r\nHost: wwww.sotalive.net\r\n\r\n");
  secure_client.print(HttpHeader);
  while (!secure_client.available())
    ;
  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!secure_client.find(endOfHeaders))
  {
    Serial.println(F("Invalid response"));
    return String("");
  }
  StaticJsonDocument<256> jsonBuffer;
  String res = secure_client.readString();
  Serial.println(res);
  DeserializationError error = deserializeJson(jsonBuffer, res);
  String result = "";
  if (error)
  {
    Serial.println("Parsing failed!");
    return result;
  }
  else
  {
    const char *errors = jsonBuffer["errors"];
    if (strcmp(errors, "OK") == 0)
    {
      result = String((const char *)jsonBuffer["uuid_hunt"]);
      Serial.printf("client UUID=%s\n", result.c_str());
    }
    return result;
  }
}

void MQTTMessage::enqueue(const char *topic, byte *payload, int length)
{
  if ((tail + 1) % MQTT_BUFFER_SIZE == head)
  {
    Serial.println("Error MQTT Buffer Full.");
    return;
  }
  String *topicStr = new String(topic);
  byte *p;

  if (topicStr->indexOf("js/spot/") >= 0)
  {
    topic_buffer[tail] = topicStr;
    p = (byte *)malloc(length);
    memcpy(p, payload, length);
    payload_buffer[tail] = p;
    Serial.printf("New topic %d = %s\n", tail, topicStr->c_str());
    tail = (tail + 1) % MQTT_BUFFER_SIZE;
  }
  else
  {
    Serial.printf("Discard topic %s\n", topicStr->c_str());
    delete topicStr;
  }
}

int MQTTMessage::available()
{
  int len = tail - head;
  if (len < 0)
    len += MQTT_BUFFER_SIZE;

  return len;
}

int MQTTMessage::dequeue(String &newspot, String &body, String &summary, bool &has_JA)
{
  String *topic;
  byte *payload;
  DynamicJsonDocument doc(512);
  char buff[512];

  if (tail == head)
  {
    Serial.printf("Error MQTT Buffer Empty head=%d tail=%d avail %d", head, tail, available());
    return -1;
  }

  topic = topic_buffer[head];
  payload = payload_buffer[head];

  Serial.printf("Dequeue message %d =  %s\n", head, topic->c_str());
  deserializeJson(doc, payload);

  bool newref = false;
  String ref = doc["refid"];
  const char *call = doc["call"];
  String name = doc["name"];
  const char *namek = doc["namek"];
  const float freq = doc["freq"];
  const char *mode = doc["mode"];
  const int qso = doc["qso"];

  const char *prog = "POTA";
  int is_spot = topic->indexOf("js/spot/");

  if (topic->indexOf("sota") > 0)
  {
    prog = "SOTA";
    name.replace(" pts", "point");
    name.replace(" pt", "point");
    summary = String(call) + " at " + ref;
  }
  else
  {
    summary = String(call) + " in " + ref;
    if (is_spot > 0 && qso == 0)
      newref = true;
  }

  ref.replace("-", "");

  if (ref.indexOf("JA") == 0)
  {
    has_JA = true;
    ref.replace("/", " ");
    if (newref)
      sprintf(buff, "未ハントの新しい%sスポットです|", prog);
    else
      sprintf(buff, "新しい%sスポットです|", prog);
    newspot = buff;

    if (strlen(mode) > 0)
    {
      sprintf(buff, "<say-as interpret-as='characters'>%s</say-as>局が,<say-as interpret-as='verbatim'>%s</say-as>,|%sから,%.3fメガヘルツ,モード%sでアクティベーション中です",
              call, ref.c_str(), namek, freq, mode);
    }
    else
    {
      sprintf(buff, "<say-as interpret-as='characters'>%s</say-as>局が,<say-as interpret-as='verbatim'>%s</say-as>,|%sから,%.3fメガヘルツでアクティベーション中です",
              call, ref.c_str(), namek, freq);
    }
    body = buff;
  }
  else
  {
    has_JA = false;
    if (newref)
      sprintf(buff, "New Unhunted %s Spot|", prog);
    else
      sprintf(buff, "New %s Spot|", prog);
    newspot = buff;

    if (strlen(mode) > 0)
    {
      sprintf(buff, "<say-as interpret-as='characters'>%s</say-as> is activating %s,| %s,at %.3f MHz, mode %s.",
              call, ref.c_str(), name.c_str(), freq, mode);
    }
    else
    {
      sprintf(buff, "<say-as interpret-as='characters'>%s</say-as> is activating %s,| %s, at %.3f MHz.",
              call, ref.c_str(), name.c_str(), freq);
    }
    body = buff;
  }

  delete topic;
  free(payload);
  head = (head + 1) % MQTT_BUFFER_SIZE;
  return 0;
}