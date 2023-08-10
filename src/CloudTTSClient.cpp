#include "CloudTTSClient.h"
#include "network_param_tts.h"

uint8_t CloudTTSClient::buffer[TTS_BUFFER_SIZE];

const char *CloudTTSClient::conv_table_jp_call[] = {
    "1", "ワン", "2", "ツー", "3", "スリー", "4", "フォー", "5", "ファイブ", "6", "シックス", "7", "セブン", "8", "エイト", "9", "ナイン", "0", "ゼロ",
    "A", "エイ", "B", "ビー", "C", "シー", "D", "ディー", "E", "イー", "F", "エフ", "G", "ジー", "H", "エイチ", "I", "アイ",
    "J", "ジェイ", "K", "ケイ", "L", "エル", "M", "エム", "N", "エヌ", "O", "オー", "P", "ピー", "Q", "キュー", "R", "アール",
    "S", "エス", "T", "ティー", "U", "ユー", "V", "ブイ", "W", "ダブリュー", "X", "エックス", "Y", "ワイ", "Z", "ゼット", "/", "ストローク",
    NULL, NULL};

const char *CloudTTSClient::conv_table_jp_ref[] = {
    "A", "エイ", "B", "ビー", "C", "シー", "D", "ディー", "E", "イー", "F", "エフ", "G", "ジー", "H", "エイチ", "I", "アイ",
    "J", "ジェイ", "K", "ケイ", "L", "エル", "M", "エム", "N", "エヌ", "O", "オー", "P", "ピー", "Q", "キュー", "R", "アール",
    "S", "エス", "T", "ティー", "U", "ユー", "V", "ブイ", "W", "ダブリュー", "X", "エックス", "Y", "ワイ", "Z", "ゼット", NULL, NULL};

CloudTTSClient::CloudTTSClient()
{
#ifdef ENABLE_SSML_TO_SPEECH
  secure_client.setCACert(root_ca_tts);
  secure_client.setTimeout(10000);
  if (!secure_client.connect(server_tts, 443))
    Serial.println("Connection failed!");
#endif
}

CloudTTSClient::~CloudTTSClient()
{
}

void CloudTTSClient::convPronunce(String &out_text, String &in_text, CloudTTSClient::TTS_LANG lang)
{
  const char *speak_in = "<speak>";
  const char *speak_out = "</speak>";
  const char *say_as_in = "<say-as interpret-as='";
  const char *say_as_out = "</say-as>";

  int tag_speak_in = in_text.indexOf(speak_in);
  int tag_speak_out = -1;
  int tag_sayas_in = in_text.indexOf(say_as_in);
  int tag_sayas_out = -1;
  int conv = 0;

  for (int pos = 0; pos < in_text.length();)
  {
    if (pos == tag_speak_in)
    {
      pos += strlen(speak_in);
      tag_speak_out = in_text.indexOf(speak_out, pos);
      continue;
    }
    if (pos == tag_speak_out)
    {
      pos += strlen(speak_out);
      tag_speak_in = in_text.indexOf(speak_in, pos);
      continue;
    }
    if (pos == tag_sayas_in)
    {
      if (in_text.indexOf("characters",pos) >= 0)
        conv = 1;
      else
        conv = 2;
      pos = in_text.indexOf(">",pos) + 1;
      tag_sayas_out = in_text.indexOf(say_as_out, pos);
      continue;
    }
    else if (pos == tag_sayas_out)
    {
      pos += strlen(say_as_out);
      tag_sayas_in = in_text.indexOf(say_as_in, pos);
      conv = 0;
      continue;
    }

    if (lang == JA_JP && conv)
    {
      int i;
      const char **table;
      if (conv == 1)
        table = conv_table_jp_call;
      else
        table = conv_table_jp_ref;

      for (i = 0; table[i] != NULL; i += 2)
      {
        if (in_text[pos] == table[i][0])
        {
          out_text += table[i + 1];
          break;
        }
      }
      if (table[i] == NULL)
      {
        out_text += in_text[pos];
      }
    }
    else
    {
      out_text += in_text[pos];
    }
    pos++;
  }
};

uint CloudTTSClient::textToSpeach(CloudTTSClient::TTS_LANG lang, String &text)
{
  String converted;
  String lang_code;

  convPronunce(converted, text, lang);
  Serial.printf("TTS text = %s\n", converted.c_str());
  if (lang == CloudTTSClient::JA_JP)
  {
    lang_code = "ja";
  }
  else
  {
    lang_code = "en";
  }

  String link = "http" + tts.getSpeechUrl(converted, lang_code).substring(5);
  //Serial.println(link);

  http.begin(client, link);
  http.setReuse(true);
  int code = http.GET();
  if (code != HTTP_CODE_OK)
  {
    http.end();
    return 0;
  }
  WiFiClient *ttsclient = http.getStreamPtr();
  ttsclient->setTimeout(10000);
  if (ttsclient->available() > 0)
  {
    int i = 0;
    int len = sizeof(buffer);
    int count = 0;

    bool data_end = false;
    while (!data_end)
    {
      if (ttsclient->available() > 0)
      {

        int bytesread = ttsclient->read(&buffer[i], len);
        i = i + bytesread;
        if (i > sizeof(buffer))
        {
          break;
        }
        else
        {
          len = len - bytesread;
          if (len <= 0)
            break;
        }
      }
      {
        //Serial.printf(" %d Bytes Read\n", i);
        int lastms = millis();
        data_end = true;
        while (millis() - lastms < 600)
        { // データ終わりか待ってみる
          if (ttsclient->available() > 0)
          {
            data_end = false;
            break;
          }
          yield();
        }
      }
    }

    Serial.printf("Total %d Bytes Read\n", i);
    ttsclient->stop();
    http.end();

    return i;
  }
  return 0;
}

#ifdef ENABLE_SSML_TO_SPEECH
inline uint8_t b64dec(uint8_t ch)
{
  // "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
  // "0000000000111111111122222222223333333333444444444455555555556666"
  // "0123456789012345678901234567890123456789012345678901234567890123"
  if (ch == '/')
    return 63;
  else if (ch == '+')
    return 62;
  else if (ch >= 'a')
    return ch - 'a' + 26;
  else if (ch >= 'A')
    return ch - 'A';
  else if (ch >= '0')
    return ch - '0' + 52;
  return 0;
}

inline uint CloudTTSClient::base64decode(u_int in, u_int idx, u_int out)
{
  switch (idx % 4)
  {
  case 0:
    break;
  case 1:
    buffer[out++] = (b64dec(buffer[in - 1]) << 2) + ((b64dec(buffer[in]) & 0x30) >> 4);
    break;
  case 2:
    buffer[out++] = ((b64dec(buffer[in - 1]) & 0xf) << 4) + ((b64dec(buffer[in]) & 0x3c) >> 2);
    break;
  case 3:
    buffer[out++] = ((b64dec(buffer[in - 1]) & 0x3) << 6) + b64dec(buffer[in]);
    break;
  }
  return out;
}

uint CloudTTSClient::ssmlToSpeach(CloudTTSClient::TTS_LANG lang, String &text)
{
  String lang_code;
  String voice_name;

  switch (lang)
  {
  case CloudTTSClient::EN_US:
    lang_code = "en-US";
    voice_name = lang_code + "-Standard-G";
    break;
  case CloudTTSClient::JA_JP:
    lang_code = "ja-JP";
    voice_name = lang_code + "-Wavenet-B";
    break;
  }
  String gender = "FEMALE";
  String sample_rate = "8000";
  String ssml(text);
  String HttpBody = "{\"input\":{\"ssml\":\"" + ssml + "\"},\"voice\":{\"languageCode\":\"" + lang_code + "\",\"name\":\"" + voice_name + "\",\"ssmlGender\":\"" + gender + "\"},\"audioConfig\":{\"audioEncoding\":\"MP3\",\"sampleRateHertz\":" + sample_rate + "}}\r\n\r\n";
  String ContentLength = String(HttpBody.length());
  String HttpHeader = String("POST /v1/text:synthesize?key=") + tts_access_key + String(" HTTP/1.1\r\nHost: texttospeech.googleapis.com\r\nContent-Type: application/json\r\nContent-Length: ") + ContentLength + String("\r\n\r\n");
  // String HttpHeader = String("POST /v1/text:synthesize HTTP/1.1\r\nHost: texttospeech.googleapis.com\r\nContent-Type: application/json\r\nAuthorization: Bearer ") + tts_cred + String("\r\nContent-Length: ") + ContentLength + String("\r\n\r\n");
  client.print(HttpHeader);
  // Serial.print(HttpHeader);
  client.print(HttpBody);
  // Serial.print(HttpBody);
  uint in = 0, start = 0, idx = 0, out = 0;
  uint len = sizeof(buffer) - 1;
  bool data_end = false;
  const char *JSONKEY = "\"audioContent\": \"";
  while (!data_end)
  {
    if (client.available() > 0)
    {
      int byteread = client.read(&buffer[in], len);
      // buffer[in + byteread] = 0;
      // Serial.printf("Read %d bytes\n%s-----------------\n", byteread, &buffer[in]);
      //    Check Response and Skip JSON Key.
      if (in == 0)
      {
        uint8_t *ptr;
        if (ptr = (uint8_t *)strstr((const char *)buffer, JSONKEY))
        {
          start = (ptr - buffer) + strlen(JSONKEY);
        }
        else
        {
          Serial.printf("Invalid response:%s\n", &buffer[in]);
          return 0;
        }
      }
      else
      {
        start = 0;
      };

      for (uint i = start; i < byteread; i++)
      {
        // Decode base64 until quote.
        char c = buffer[in + i];
        if (c == '+' || c == '/' || isalnum(c))
        {
          // Serial.printf("%c", c);
          out = base64decode(in + i, idx++, out);
        }
        else if (c == '"')
        {
          data_end = true;
        }
      }
      in += byteread;
      len -= byteread;
      if (in > sizeof(buffer) || len <= 0)
      {
        break;
      }
      // Serial.printf("\nTotal %d bytes left %d\n", in, len);
      /*
      int lastms = millis();
      data_end = true;
      while (millis() - lastms < 800)
      {
        if (client.available() > 0)
        {
          data_end = false;
          break;
        }
        yield();
      }
      */
    }
  }
  while (client.available())
  {
    client.read();
  }
  Serial.printf("Read %d bytes, Decoded %d bytes.", idx, out);
  client.stop();
  return out;
}
#endif
