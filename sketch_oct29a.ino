#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <RCSwitch.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <Preferences.h> 
#include <HTTPClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Default Wi-Fi credentials 
const char* default_ssid = "XXXXXXXXXXXXXXXXX";
const char* default_password = "XXXXXXXX";

// Telegram Bot credentials
const char* botToken = "XXXXXXXXXXXXXXXXXXXXX";
const char* chatId = "XXXXXXXXXXXXXXXXXXXXXXX"; 

// Google Script Web App URL
String googleSheetLink = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"; 

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // IST Time
const int   daylightOffset_sec = 0;

WiFiClientSecure client; 
UniversalTelegramBot bot(botToken, client);
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Preferences preferences; 

// --- 433MHz RCSwitch Receiver ---
RCSwitch receiver; 

const int rxPin = 27;    // GPIO 27
const int relayPin = 5;
const int btnToggle = 12;
const int buzzerPin = 14; 

int currentWaterLevel = 0;
bool isEmergencyMode = false; 

// RF Connection Status for LCD
unsigned long lastRfRxTime = 0;
bool rfConnected = false;

// Global Language Code (Default: Tamil)
String selectedLang = "ta";
bool notificationsEnabled = true; 

int autoOnLevel = 25;
int autoOffLevel = 100;
String currentSsid = "";
String currentPass = "";

// Timer Settings
bool timerEnabled = false;
String timerOnTime = "06:00";
String timerOffTime = "07:00";
int tOnHour = 0, tOnMin = 0;
int tOffHour = 0, tOffMin = 0;
int lastCheckedMinute = -1;

String lastMotorOnTime = "-";
String lastMotorOffTime = "-";
bool buzzerFullPlayed = false;
bool buzzerLowPlayed = false;

int inputState = 0; 
String tempSsid = ""; 

volatile bool buttonPressed = false;
unsigned long lastInterruptTime = 0;
unsigned long lastTimeBotRan = 0;
const int botRequestDelay = 1500; 

volatile bool sendAlertOff = false;
volatile bool sendAlertOn = false;
volatile bool sendAlertCritical = false;
volatile bool sendManualStartAlert = false;
volatile bool sendManualStopAlert = false;
volatile bool sendManualFullAlert = false;
volatile bool sendTimerStartAlert = false;
volatile bool sendTimerStopAlert = false;
volatile bool updateLcdFlag = false;

bool timeSynced = false;
bool bootMsgSent = false;

struct SheetLog {
  String state;
  String onTime;
  String offTime;
  String logDate;
  String logTime;
};
SheetLog logQueue[10];
volatile int queueHead = 0;
volatile int queueTail = 0;
volatile int queueCount = 0;

String getDate() {
  if (!timeSynced) return "Offline";
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) return "Offline";
  char buff[20]; strftime(buff, sizeof(buff), "%d-%m-%Y", &timeinfo); return String(buff);
}
String getTime() {
  if (!timeSynced) return "Offline";
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) return "Offline";
  char buff[20]; strftime(buff, sizeof(buff), "%I:%M:%S %p", &timeinfo); return String(buff);
}
String getCurrentTime() {
  if (!timeSynced) return (selectedLang == "ta" ? "(ஆஃப்லைன்)" : "(Offline Mode)");
  return getDate() + " " + getTime();
}

void parseTimerStrings() {
  sscanf(timerOnTime.c_str(), "%d:%d", &tOnHour, &tOnMin);
  sscanf(timerOffTime.c_str(), "%d:%d", &tOffHour, &tOffMin);
}

String urlEncode(String str) {
  String encodedString = "";
  char c, code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

void enqueueSheetLog(String state, String onTime, String offTime) {
  if (queueCount < 10) {
    logQueue[queueTail].state = state;
    logQueue[queueTail].onTime = onTime;
    logQueue[queueTail].offTime = offTime;
    logQueue[queueTail].logDate = getDate();
    logQueue[queueTail].logTime = getTime();
    queueTail = (queueTail + 1) % 10;
    queueCount++;
  }
}

void setMotorState(bool state, String reason) {
  if (state == HIGH && digitalRead(relayPin) == LOW) {
    digitalWrite(relayPin, HIGH);
    lastMotorOnTime = getTime();
    enqueueSheetLog(reason, lastMotorOnTime, "-");
  } 
  else if (state == LOW && digitalRead(relayPin) == HIGH) {
    digitalWrite(relayPin, LOW);
    lastMotorOffTime = getTime();
    enqueueSheetLog(reason, lastMotorOnTime, lastMotorOffTime);
  }
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Lvl:" + String(currentWaterLevel) + "% ");
  lcd.print(rfConnected ? "RF:OK" : "RF:--"); 

  lcd.setCursor(0, 1);
  lcd.print(digitalRead(relayPin) ? "Mot:ON " : "Mot:OFF");
  lcd.print(isEmergencyMode ? " EMG " : " AUT ");
  lcd.print(WiFi.status() == WL_CONNECTED ? "[W]" : "[X]");
}

void sendMainMenu(String chat_id) {
  String keyboardJson;
  String msg;

  if (selectedLang == "ta") {
    keyboardJson = "[[\"மோட்டார் ஆன்\", \"மோட்டார் ஆஃப்\"], [\"அவசர ஆன்\", \"அவசர ஆஃப்\"], [\"சாதாரண நிலை\", \"நிலைமை\"], [\"அமைப்புகள்\"]]";
    msg = "பிரதான மெனு. கட்டளையைத் தேர்ந்தெடுக்கவும்:";
  } else if (selectedLang == "hi") {
    keyboardJson = "[[\"मोटर चालू\", \"मोटर बंद\"], [\"इमरजेंसी चालू\", \"इमरजेंसी बंद\"], [\"सामान्य मोड\", \"स्थिति देखें\"], [\"सेटिंग्स\"]]";
    msg = "मुख्य मेनू। कमांड चुनें:";
  } else if (selectedLang == "ar") {
    keyboardJson = "[[\"تشغيل المضخة\", \"إيقاف المضخة\"], [\"طوارئ تشغيل\", \"طوارئ إيقاف\"], [\"وضع عادي\", \"فحص الحالة\"], [\"الإعدادات\"]]";
    msg = "القائمة الرئيسية. اختر أمرا:";
  } else {
    keyboardJson = "[[\"Start Motor\", \"Stop Motor\"], [\"Emergency ON\", \"Emergency OFF\"], [\"Normal Mode\", \"Check Status\"], [\"Settings\"]]";
    msg = "Main Menu. Select command:";
  }
  bot.sendMessageWithReplyKeyboard(chat_id, msg, "", keyboardJson, true);
}

void IRAM_ATTR handleButtonPress() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > 300) { 
    buttonPressed = true;
    lastInterruptTime = interruptTime;
  }
}

void processWaterLevel(int newLevel) {
  if (newLevel == currentWaterLevel && rfConnected) {
    lastRfRxTime = millis(); 
    return; 
  }

  currentWaterLevel = newLevel;
  rfConnected = true;
  lastRfRxTime = millis(); 

  Serial.print("✅ Water Level Changed: ");
  Serial.print(currentWaterLevel);
  Serial.println("%");
  
  if (currentWaterLevel >= autoOffLevel) {
    buzzerLowPlayed = false; 
    if (!buzzerFullPlayed) {
      for(int i=0; i<2; i++){ digitalWrite(buzzerPin, HIGH); delay(800); digitalWrite(buzzerPin, LOW); delay(200); } 
      buzzerFullPlayed = true;
    }
  } else if (currentWaterLevel <= autoOnLevel) {
    buzzerFullPlayed = false; 
    if (!buzzerLowPlayed) {
      for(int i=0; i<3; i++){ digitalWrite(buzzerPin, HIGH); delay(150); digitalWrite(buzzerPin, LOW); delay(100); } 
      buzzerLowPlayed = true;
    }
  } else {
    buzzerFullPlayed = false;
    buzzerLowPlayed = false;
  }
  
  if (!isEmergencyMode) {
    if (currentWaterLevel >= autoOffLevel && digitalRead(relayPin) == HIGH) {
      setMotorState(LOW, "Auto OFF");
      sendAlertOff = true; 
    }
    if (currentWaterLevel <= autoOnLevel && digitalRead(relayPin) == LOW) {
      setMotorState(HIGH, "Auto ON");
      sendAlertOn = true; 
    }
    if (currentWaterLevel <= 5 && digitalRead(relayPin) == HIGH) {
      setMotorState(LOW, "Critical OFF");
      sendAlertCritical = true; 
    }
  }
  updateLCD();
}

void networkTask(void * parameter) {
  unsigned long lastWifiCheck = 0;
  bool wasConnected = false;

  for(;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!wasConnected) {
        wasConnected = true;
        Serial.print("\n[Background] WiFi Connected! IP: ");
        Serial.println(WiFi.localIP());
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        updateLcdFlag = true;
      }

      if (!timeSynced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
          timeSynced = true;
          Serial.println("[Background] Time Synced!");
        }
      }

      if (!bootMsgSent) {
        Serial.println("[Telegram] Connecting to api.telegram.org...");
        if (notificationsEnabled) {
          if (bot.sendMessage(chatId, "ESP32 Receiver Online! ✅\n💧 Water Level: " + String(currentWaterLevel) + "%\nSend /start to open menu.", "")) {
            Serial.println("[Telegram] Startup Message Sent OK! ✅");
            bootMsgSent = true;
          } else {
            Serial.println("[Telegram] Handshake busy, retrying in 5s...");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
          }
        } else {
          bootMsgSent = true;
        }
      }

      if (queueCount > 0 && timeSynced) {
        SheetLog item = logQueue[queueHead];
        String d = (item.logDate == "Offline") ? getDate() : item.logDate;
        String t = (item.logTime == "Offline") ? getTime() : item.logTime;
        String onT = (item.onTime == "Offline") ? t : item.onTime;
        String offT = (item.offTime == "Offline") ? t : item.offTime;

        WiFiClientSecure sheetClient;
        sheetClient.setInsecure(); 
        sheetClient.setTimeout(3000);
        HTTPClient http;
        
        String url = googleSheetLink + "?date=" + urlEncode(d) + 
                     "&time=" + urlEncode(t) + 
                     "&ontime=" + urlEncode(onT) + 
                     "&offtime=" + urlEncode(offT) + 
                     "&state=" + urlEncode(item.state);
        
        http.begin(sheetClient, url); 
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); 
        int httpCode = http.GET();
        if(httpCode > 0) {
          queueHead = (queueHead + 1) % 10;
          queueCount--;
        }
        http.end();
      }

      // Notification Handlers (Text Alerts)
      if (sendAlertOff) { 
        sendAlertOff = false; 
        if(notificationsEnabled) {
          String msg = (selectedLang == "ta") ? "⚠️ தொட்டி நிரம்பிவிட்டது (" + String(autoOffLevel) + "%).\nநேரம்: " : "⚠️ Tank FULL (" + String(autoOffLevel) + "%).\nAuto-stopped at: ";
          bot.sendMessage(chatId, msg + getCurrentTime(), ""); 
        }
      }
      if (sendAlertOn) { 
        sendAlertOn = false; 
        if(notificationsEnabled) {
          String msg = (selectedLang == "ta") ? "⚠️ நீர் மட்டம் குறைவு (" + String(autoOnLevel) + "%).\nநேரம்: " : "⚠️ Tank level LOW (" + String(autoOnLevel) + "%).\nAuto-started at: ";
          bot.sendMessage(chatId, msg + getCurrentTime(), ""); 
        }
      }
      if (sendAlertCritical) { 
        sendAlertCritical = false; 
        if(notificationsEnabled) {
          String msg = (selectedLang == "ta") ? "🚨 நீர் மட்டம் மிகக் குறைவு (5%).\nநிறுத்தப்பட்டது: " : "🚨 Tank level CRITICAL (5%).\nMotor Stopped at: ";
          bot.sendMessage(chatId, msg + getCurrentTime(), ""); 
        }
      }
      if (sendManualStartAlert) { 
        sendManualStartAlert = false; 
        if(notificationsEnabled) {
          String msg = (selectedLang == "ta") ? "🔧 மோட்டார் சுவிட்ச் மூலம் இயக்கப்பட்டது.\n" : "🔧 Motor started MANUALLY via switch.\n";
          bot.sendMessage(chatId, msg + getCurrentTime(), ""); 
        }
      }
      if (sendManualStopAlert) { 
        sendManualStopAlert = false; 
        if(notificationsEnabled) {
          String msg = (selectedLang == "ta") ? "🔧 மோட்டார் சுவிட்ச் மூலம் நிறுத்தப்பட்டது.\n" : "🔧 Motor stopped MANUALLY via switch.\n";
          bot.sendMessage(chatId, msg + getCurrentTime(), ""); 
        }
      }
      if (sendManualFullAlert) { 
        sendManualFullAlert = false; 
        if(notificationsEnabled) {
          bot.sendMessage(chatId, (selectedLang == "ta") ? "⚠️ தொட்டி நிரம்பியுள்ளது." : "⚠️ Cannot start manually. Tank is FULL.", ""); 
        }
      }
      if (sendTimerStartAlert) {
        sendTimerStartAlert = false;
        if (notificationsEnabled) {
          bot.sendMessage(chatId, "⏰ Timer Motor Started: " + getCurrentTime(), "");
        }
      }
      if (sendTimerStopAlert) {
        sendTimerStopAlert = false;
        if (notificationsEnabled) {
          bot.sendMessage(chatId, "⏰ Timer Motor Stopped: " + getCurrentTime(), "");
        }
      }

      if (millis() - lastTimeBotRan > botRequestDelay) {
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        
        while (numNewMessages) {
          for (int i = 0; i < numNewMessages; i++) {
            String text = bot.messages[i].text;
            String chat_id = String(bot.messages[i].chat_id);
            
            Serial.print("[Telegram] In: ");
            Serial.println(text);

            if (inputState > 0) {
              if (text == "Cancel" || text == "ரத்து செய்") {
                inputState = 0;
                bot.sendMessage(chat_id, "Cancelled.", "");
                sendMainMenu(chat_id);
                continue;
              }

              if (inputState == 1) { 
                tempSsid = text;
                inputState = 2;
                bot.sendMessage(chat_id, "Enter Wi-Fi Password:", "");
              } 
              else if (inputState == 2) { 
                preferences.putString("ssid", tempSsid);
                preferences.putString("pass", text);
                currentSsid = tempSsid;
                currentPass = text;
                inputState = 0;
                bot.sendMessage(chat_id, "Network Saved! Reconnecting... 🔄", "");
                WiFi.disconnect();
                WiFi.begin(currentSsid.c_str(), currentPass.c_str());
              } 
              else if (inputState == 3) { 
                autoOnLevel = text.toInt();
                preferences.putInt("on_lvl", autoOnLevel);
                inputState = 4;
                bot.sendMessage(chat_id, "Enter Motor OFF % (e.g. 100):", "");
              } 
              else if (inputState == 4) { 
                autoOffLevel = text.toInt();
                preferences.putInt("off_lvl", autoOffLevel);
                inputState = 0;
                bot.sendMessage(chat_id, "Settings Saved! ✅", "");
                sendMainMenu(chat_id);
              }
              else if (inputState == 5) {
                timerOnTime = text;
                preferences.putString("t_on", timerOnTime);
                inputState = 6;
                bot.sendMessage(chat_id, "Enter Motor OFF time (24-hr format, e.g. 07:15):", "");
              } 
              else if (inputState == 6) {
                timerOffTime = text;
                preferences.putString("t_off", timerOffTime);
                inputState = 0;
                parseTimerStrings(); 
                bot.sendMessage(chat_id, "⏰ Timer Saved!", "");
                sendMainMenu(chat_id);
              }
              continue; 
            }

            // Command Processing
            if (text == "/start") {
              sendMainMenu(chat_id);
            }
            else if (text == "Settings" || text == "அமைப்புகள்" || text == "सेटिंग्स" || text == "الإعدادات") {
              String kbd = "[[\"🌐 World Languages\", \"Network Reset\"], [\"Timer Setup\", \"Timer Toggle\"], [\"Water % Setup\", \"Notif Toggle\"], [\"Back\"]]";
              bot.sendMessageWithReplyKeyboard(chat_id, "⚙️ Settings:", "", kbd, true);
            }
            // --- WORLD REGIONS MENU ---
            else if (text == "🌐 World Languages" || text.indexOf("Languages") >= 0) {
              String kbd = "[[\"🇮🇳 South Asia (India)\", \"🌍 Europe & Americas\"], [\"🌏 East & SE Asia\", \"🕌 Middle East & Africa\"], [\"Back\"]]";
              bot.sendMessageWithReplyKeyboard(chat_id, "🌍 Select World Region / உலகப் பகுதியைத் தேர்ந்தெடுக்கவும்:", "", kbd, true);
            }
            // --- REGION 1: SOUTH ASIA / INDIA ---
            else if (text == "🇮🇳 South Asia (India)") {
              String kbd = "[[\"தமிழ் (Tamil)\", \"English (India)\"], [\"हिंदी (Hindi)\", \"తెలుగు (Telugu)\"], [\"മലയാളം (Malayalam)\", \"ಕನ್ನಡ (Kannada)\"], [\"বাংলা (Bengali)\", \"मराठी (Marathi)\"], [\"ગુજરાતી (Gujarati)\", \"ਪੰਜਾਬੀ (Punjabi)\"], [\"اردو (Urdu)\", \"नेपाली (Nepali)\"], [\"🌐 World Languages\"]]";
              bot.sendMessageWithReplyKeyboard(chat_id, "🇮🇳 Select Language:", "", kbd, true);
            }
            // --- REGION 2: EUROPE & AMERICAS ---
            else if (text == "🌍 Europe & Americas") {
              String kbd = "[[\"English (US/UK)\", \"Español (Spanish)\"], [\"Français (French)\", \"Deutsch (German)\"], [\"Italiano (Italian)\", \"Português (Portuguese)\"], [\"Русский (Russian)\", \"Türkçe (Turkish)\"], [\"Nederlands (Dutch)\", \"Polski (Polish)\"], [\"🌐 World Languages\"]]";
              bot.sendMessageWithReplyKeyboard(chat_id, "🌍 Select Language:", "", kbd, true);
            }
            // --- REGION 3: EAST & SE ASIA ---
            else if (text == "🌏 East & SE Asia") {
              String kbd = "[[\"中文 (Mandarin)\", \"日本語 (Japanese)\"], [\"한국어 (Korean)\", \"Bahasa Indonesia\"], [\"Tiếng Việt (Vietnamese)\", \"ภาษาไทย (Thai)\"], [\"Filipino (Tagalog)\", \"Bahasa Melayu\"], [\"🌐 World Languages\"]]";
              bot.sendMessageWithReplyKeyboard(chat_id, "🌏 Select Language:", "", kbd, true);
            }
            // --- REGION 4: MIDDLE EAST & AFRICA ---
            else if (text == "🕌 Middle East & Africa") {
              String kbd = "[[\"العربية (Arabic)\", \"فارسی (Persian)\"], [\"Kiswahili (Swahili)\", \"עברית (Hebrew)\"], [\"🌐 World Languages\"]]";
              bot.sendMessageWithReplyKeyboard(chat_id, "🕌 Select Language:", "", kbd, true);
            }
            // --- LANGUAGE SWITCH HANDLERS ---
            else if (text == "தமிழ் (Tamil)") { selectedLang = "ta"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "மொழி தமிழ் என மாற்றப்பட்டது 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "English (India)" || text == "English (US/UK)") { selectedLang = "en"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Language set to English 🌐 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "हिंदी (Hindi)") { selectedLang = "hi"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "भाषा हिंदी में चुनी गई 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "తెలుగు (Telugu)") { selectedLang = "te"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "భాష తెలుగుగా మార్చబడింది 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "മലയാളം (Malayalam)") { selectedLang = "ml"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "ഭാഷ മലയാളത്തിലേക്ക് മാറ്റി 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "ಕನ್ನಡ (Kannada)") { selectedLang = "kn"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "ಭಾಷೆಯನ್ನು ಕನ್ನಡಕ್ಕೆ ಹೊಂದಿಸಲಾಗಿದೆ 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "বাংলা (Bengali)") { selectedLang = "bn"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "ভাষা বাংলায় সেট করা হয়েছে 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "मराठी (Marathi)") { selectedLang = "mr"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "भाषा मराठीत निवडली गेली 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "ગુજરાતી (Gujarati)") { selectedLang = "gu"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "ભાષા ગુજરાતીમાં સેટ થઈ 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "ਪੰਜਾਬੀ (Punjabi)") { selectedLang = "pa"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "ਭਾਸ਼ਾ ਪੰਜਾਬੀ ਵਿੱਚ ਸੈੱਟ ਕੀਤੀ ਗਈ 🇮🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "اردو (Urdu)") { selectedLang = "ur"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "زبان اردو منتخب کر لی گئی ✅", ""); sendMainMenu(chat_id); }
            else if (text == "नेपाली (Nepali)") { selectedLang = "ne"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "भाषा नेपालीमा सेट गरियो ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Español (Spanish)") { selectedLang = "es"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Idioma configurado a Español 🇪🇸 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Français (French)") { selectedLang = "fr"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Langue définie sur Français 🇫🇷 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Deutsch (German)") { selectedLang = "de"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Sprache auf Deutsch eingestellt 🇩🇪 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Italiano (Italian)") { selectedLang = "it"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Lingua impostata su Italiano 🇮🇹 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Português (Portuguese)") { selectedLang = "pt"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Idioma definido para Português 🇵🇹 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Русский (Russian)") { selectedLang = "ru"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Язык установлен на Русский 🇷🇺 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Türkçe (Turkish)") { selectedLang = "tr"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Dil Türkçe olarak ayarlandı 🇹🇷 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Nederlands (Dutch)") { selectedLang = "nl"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Taal ingesteld op Nederlands 🇳🇱 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Polski (Polish)") { selectedLang = "pl"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Język ustawiony na Polski 🇵🇱 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "中文 (Mandarin)") { selectedLang = "zh"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "语言已设置为中文 🇨🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "日本語 (Japanese)") { selectedLang = "ja"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "言語が日本語に設定されました 🇯🇵 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "한국어 (Korean)") { selectedLang = "ko"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "언어가 한국어로 설정되었습니다 🇰🇷 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Bahasa Indonesia") { selectedLang = "id"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Bahasa diatur ke Indonesia 🇮🇩 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Tiếng Việt (Vietnamese)") { selectedLang = "vi"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Ngôn ngữ đã đổi sang Tiếng Việt 🇻🇳 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "ภาษาไทย (Thai)") { selectedLang = "th"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "ตั้งค่าภาษาเป็นภาษาไทย 🇹🇭 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Filipino (Tagalog)") { selectedLang = "fil"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Wika ay naitakda sa Filipino 🇵🇭 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Bahasa Melayu") { selectedLang = "ms"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Bahasa ditukar ke Melayu 🇲🇾 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "العربية (Arabic)") { selectedLang = "ar"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "تم تفعيل اللغة العربية 🇦🇪 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "فارسی (Persian)") { selectedLang = "fa"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "زبان به فارسی تنظیم شد 🇮🇷 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "Kiswahili (Swahili)") { selectedLang = "sw"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "Lugha imewekwa kuwa Kiswahili 🇰🇪 ✅", ""); sendMainMenu(chat_id); }
            else if (text == "עברית (Hebrew)") { selectedLang = "he"; preferences.putString("lang", selectedLang); bot.sendMessage(chat_id, "השפה שונתה לעברית 🇮🇱 ✅", ""); sendMainMenu(chat_id); }

            else if (text == "Back" || text == "பின்செல்") {
              sendMainMenu(chat_id);
            }
            else if (text == "Notif Toggle") { 
              notificationsEnabled = !notificationsEnabled;
              preferences.putBool("notif_en", notificationsEnabled);
              bot.sendMessage(chat_id, notificationsEnabled ? "🔔 Notifications ON" : "🔕 Notifications OFF", "");
              sendMainMenu(chat_id);
            }
            else if (text == "Timer Toggle" || text == "டைமர் ON/OFF") {
              timerEnabled = !timerEnabled;
              preferences.putBool("timer_en", timerEnabled);
              bot.sendMessage(chat_id, timerEnabled ? "⏰ Timer is ON" : "⏰ Timer is OFF", "");
              sendMainMenu(chat_id);
            }
            else if (text == "Network Reset" || text == "நெட்வொர்க் மாற்று") {
              inputState = 1;
              bot.sendMessageWithReplyKeyboard(chat_id, "Enter new Wi-Fi SSID:", "", "[[\"Cancel\"]]", true);
            }
            else if (text == "Water % Setup" || text == "நீர் % மாற்று") {
              inputState = 3;
              bot.sendMessageWithReplyKeyboard(chat_id, "Enter Motor ON % (e.g. 25):", "", "[[\"Cancel\"]]", true);
            }
            else if (text == "Timer Setup" || text == "டைமர் அமைப்பு") {
              inputState = 5;
              bot.sendMessageWithReplyKeyboard(chat_id, "Enter Motor ON time (e.g. 06:30):", "", "[[\"Cancel\"]]", true);
            }
            else if (text == "Start Motor" || text == "மோட்டார் ஆன்" || text == "मोटर चालू" || text == "تشغيل المضخة") {
              isEmergencyMode = false;
              if (currentWaterLevel >= autoOffLevel) {
                bot.sendMessage(chat_id, "⚠️ Tank is already FULL.", "");
              } else { 
                setMotorState(HIGH, "Telegram Start"); 
                if(notificationsEnabled) bot.sendMessage(chat_id, "Motor is ON ✅\n" + getCurrentTime(), ""); 
              }
              updateLcdFlag = true;
            }
            else if (text == "Stop Motor" || text == "மோட்டார் ஆஃப்" || text == "मोटर बंद" || text == "إيقاف المضخة") {
              isEmergencyMode = false;
              setMotorState(LOW, "Telegram Stop"); 
              if(notificationsEnabled) bot.sendMessage(chat_id, "Motor is OFF ❌\n" + getCurrentTime(), ""); 
              updateLcdFlag = true;
            }
            else if (text == "Emergency ON" || text == "அவசர ஆன்" || text == "इमरजेंसी चालू" || text == "طوارئ تشغيل") {
              isEmergencyMode = true;
              setMotorState(HIGH, "Emergency ON"); 
              if(notificationsEnabled) bot.sendMessage(chat_id, "🚨 EMERGENCY: Motor forced ON!\n" + getCurrentTime(), ""); 
              updateLcdFlag = true;
            }
            else if (text == "Emergency OFF" || text == "அவசர ஆஃப்" || text == "इमरजेंसी बंद" || text == "طوارئ إيقاف") {
              isEmergencyMode = true;
              setMotorState(LOW, "Emergency OFF"); 
              if(notificationsEnabled) bot.sendMessage(chat_id, "🚨 EMERGENCY: Motor forced OFF!\n" + getCurrentTime(), ""); 
              updateLcdFlag = true;
            }
            else if (text == "Normal Mode" || text == "சாதாரண நிலை" || text == "सामान्य मोड" || text == "وضع عادي") {
              isEmergencyMode = false; 
              if(notificationsEnabled) bot.sendMessage(chat_id, "✅ NORMAL MODE ACTIVATED.\n" + getCurrentTime(), ""); 
              updateLcdFlag = true;
            }
            else if (text == "Check Status" || text == "நிலைமை" || text == "स्थिति देखें" || text == "فحص الحالة") {
              String rfStr = rfConnected ? "CONNECTED (OK)" : "DISCONNECTED (--)";
              
              String statusMsg = "📅 Time: " + getCurrentTime() + 
                                 "\n💧 Water Level: " + String(currentWaterLevel) + "%" +
                                 "\n📡 RF Link: " + rfStr +
                                 "\n⚙️ Motor is " + (digitalRead(relayPin) ? "ON" : "OFF") +
                                 "\n🔄 Mode: " + (isEmergencyMode ? "🚨 Emergency" : "✅ Normal") +
                                 "\n⚙️ Auto ON: " + String(autoOnLevel) + "% | Auto OFF: " + String(autoOffLevel) + "%" +
                                 "\n🌐 Language: " + selectedLang;
              bot.sendMessage(chat_id, statusMsg, ""); 
            }
          }
          numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
        lastTimeBotRan = millis();
      }
    } else {
      if (wasConnected) {
        wasConnected = false;
        Serial.println("\n[Background] WiFi Disconnected!");
        updateLcdFlag = true;
      }
      if (millis() - lastWifiCheck > 10000) {
        lastWifiCheck = millis();
        WiFi.reconnect();
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 

  Serial.begin(115200);
  delay(1000);

  preferences.begin("tank_app", false);
  currentSsid = preferences.getString("ssid", default_ssid);
  currentPass = preferences.getString("pass", default_password);
  autoOnLevel = preferences.getInt("on_lvl", 25);
  autoOffLevel = preferences.getInt("off_lvl", 100);
  selectedLang = preferences.getString("lang", "ta");
  notificationsEnabled = preferences.getBool("notif_en", true);
  
  timerEnabled = preferences.getBool("timer_en", false);
  timerOnTime = preferences.getString("t_on", "06:00");
  timerOffTime = preferences.getString("t_off", "07:00");
  parseTimerStrings();

  pinMode(relayPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(btnToggle, INPUT_PULLUP);
  digitalWrite(relayPin, LOW);
  digitalWrite(buzzerPin, LOW);

  Wire.begin(21, 22); 
  lcd.begin(16, 2); 
  lcd.backlight();
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); 
  WiFi.setTxPower(WIFI_POWER_8_5dBm); 
  WiFi.begin(currentSsid.c_str(), currentPass.c_str());

  IPAddress dns1(8, 8, 8, 8);
  IPAddress dns2(1, 1, 1, 1);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);

  client.setInsecure(); 
  client.setTimeout(4000); 

  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 16384, NULL, 1, NULL, 0);

  delay(200); 

  attachInterrupt(digitalPinToInterrupt(btnToggle), handleButtonPress, FALLING);

  receiver.enableReceive(digitalPinToInterrupt(rxPin));
  
  updateLCD();
  Serial.println("ESP32 Receiver Ready (Clean Menu - All World Languages)");
}

void loop() {
  if (updateLcdFlag) {
    updateLcdFlag = false;
    updateLCD();
  }

  if (rfConnected && (millis() - lastRfRxTime > 10000)) {
    rfConnected = false;
    updateLCD();
  }

  if (receiver.available()) {
    unsigned long command = receiver.getReceivedValue();

    if (command != 0) {
      Serial.print("Received: ");
      Serial.println(command);

      if (command == 12300) {
        processWaterLevel(0);
      }
      else if (command == 12325 || command == 12345) {
        processWaterLevel(25);
      }
      else if (command == 12350) {
        processWaterLevel(50);
      }
      else if (command == 12375) {
        processWaterLevel(75);
      }
      else if (command == 12400) {
        processWaterLevel(100);
      }
    }

    receiver.resetAvailable();
  }

  if (timeSynced) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      if (timeinfo.tm_min != lastCheckedMinute) { 
        lastCheckedMinute = timeinfo.tm_min;
        
        if (timerEnabled && !isEmergencyMode) {
          if (timeinfo.tm_hour == tOnHour && timeinfo.tm_min == tOnMin) {
            if (currentWaterLevel < autoOffLevel && digitalRead(relayPin) == LOW) {
              setMotorState(HIGH, "Timer ON");
              sendTimerStartAlert = true;
              updateLCD();
            }
          }
          if (timeinfo.tm_hour == tOffHour && timeinfo.tm_min == tOffMin) {
            if (digitalRead(relayPin) == HIGH) {
              setMotorState(LOW, "Timer OFF");
              sendTimerStopAlert = true;
              updateLCD();
            }
          }
        }
      }
    }
  }

  if (buttonPressed) {
    buttonPressed = false;
    isEmergencyMode = false; 
    if (digitalRead(relayPin) == LOW) {
      if (currentWaterLevel >= autoOffLevel) {
        sendManualFullAlert = true;
      } else {
        setMotorState(HIGH, "Manual ON");
        sendManualStartAlert = true;
      }
    } else {
      setMotorState(LOW, "Manual OFF");
      sendManualStopAlert = true;
    }
    updateLCD();
  }
}