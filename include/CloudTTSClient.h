#ifndef _CLOUDTTSCLIENT_H
#define _CLOUDTTSCLIENT_H
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <AudioFileSourceBuffer.h>
#include "google-tts.h"

#define TTS_BUFFER_SIZE 1024 * 40
class CloudTTSClient
{
public:
  enum TTS_LANG
  {
    JA_JP,
    EN_US,
  };
  static uint8_t buffer[TTS_BUFFER_SIZE];
  CloudTTSClient();
  ~CloudTTSClient();
#ifdef ENABLE_SSML_TO_SPEECH
  uint ssmlToSpeach(CloudTTSClient::TTS_LANG lang, String &text);
#endif
  uint textToSpeach(CloudTTSClient::TTS_LANG lang, String &text);

private:
#ifdef ENABLE_SSML_TO_SPEECH
  WiFiClientSecure secure_client
#endif
  WiFiClient client;
  HTTPClient http;
  GoogleTTS tts;

  const static char *conv_table_jp_call[];
  const static char *conv_table_jp_ref[];
  uint base64decode(uint in, uint idx, uint out);
  void convPronunce(String &out_text, String &in_text, TTS_LANG lang);

};

#endif // _CLOUDTTSCLIENT_H
