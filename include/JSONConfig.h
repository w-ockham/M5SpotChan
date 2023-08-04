#ifndef CONFIG_SETTING
#define CONFIG_SETTING
#include <M5Unified.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <SPIFFS.h>

class JSONConfig
{
private:
    Preferences prefs;
    DynamicJsonDocument &json_prefs;
    char *ns_config;
    const char *key_config = "settings";

public:
    JSONConfig(char *name, DynamicJsonDocument &d) : json_prefs(d){
      ns_config = name;
    };

    ~JSONConfig(){};

    void begin()
    {
        prefs.begin(ns_config, false);
    }

    void end()
    {
        prefs.end();
    }

    size_t saveNVM()
    {
        String prefStr;
        size_t rs;

        begin();
        serializeJson(json_prefs, prefStr);
#if DEBUG_LEVEL > 1
        Serial.println("Save Config:" + prefStr);
#endif
        rs = prefs.putString(key_config, prefStr);
        end();
        return rs;
    }

    boolean loadNVM()
    {
        String prefStr;

        begin();
        prefStr = prefs.getString(key_config);
        end();

        if (prefStr == "")
        {
            Serial.println("Load Config from NVM fails");
            return false;
        }
        deserializeJson(json_prefs, prefStr);
#if DEBUG_LEVEL > 1
        Serial.print("Load Config from NVM");
        Serial.println(prefStr);
#endif
        return true;
    }

    void clearNVM()
    {
        begin();
        prefs.remove(key_config);
        end();
    }

    boolean readFromSD(const char *fname)
    {
        if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) {
            Serial.println("SD initialization error");
            return false;
        }

        if (SD.exists(fname))
        {
            File file = SD.open(fname, "r");
            DeserializationError error = deserializeJson(json_prefs, file);
            if (error)
            {
                Serial.print("JSON File format error");
                Serial.println(error.c_str());
                Serial.println(fname);
                return false;
            }
            return true;
        }
        Serial.print("JSON file not found:");
        Serial.println(fname);
        return false;
    }

    boolean readFromSPIFFS(const char *fname)
    {
        if (SPIFFS.exists(fname))
        {
            File file = SD.open(fname, "r");
            DeserializationError error = deserializeJson(json_prefs, file);
            if (error)
            {
                Serial.print("JSON File format error");
                Serial.println(error.c_str());
                Serial.println(fname);
                return false;
            }
            return true;
        }
        Serial.print("JSON file not found:");
        Serial.println(fname);
        return false;
    }
};
#endif