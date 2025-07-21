#include <Arduino.h>

// Build in libs
#include <Esp.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_rom_gpio.h"
#include <FS.h>                  
#include <WiFi.h>                     
#include <ESPmDNS.h>                   
#include <ESPAsyncWebServer.h>         
#include <HTTPClient.h>                 
#include <base64.h>                   
//#include <Preferences.h>

// my includes
//#include "fs_switch.h"   // switch between SPIFFS and LittleFS
#include "debug_print.h" // debug_print macros

// my libs
#include "FileVarStore.h"
#include "AsyncWebLog.h" // auch ohne WEB_APP dann wird auf Serial1 umgeleitet

#ifdef WEB_APP
//#include "AsyncWebApp.h" todo: noch integrieren
#include "AsyncWebOTA.h"
#endif

#include "ESP32ntp.h"
#include "XPString.h"
#include "SmartGrid.h"
#include "LuxWebsocket.h"
#include "SMLdecode.h"
#include "PicoMQTT.h"

#ifdef SHI_MODBUS
#include "LuxModbusSHI.h"
#endif

// special for S2 and S4
#if defined ESP_S2_MINI || ESP_S3_ZERO
#include "driver/temp_sensor.h"
#endif

#include "relay.h"
#include "ds100.h"

#ifdef ESP32_DEVKIT1
#if !((defined ESP32_RELAY_X4 || defined ESP32_RELAY_X2) || defined ESP32_NORELAY_X4)
#pragma message("Info : ESP32_DEVKIT")
#define LED_GPIO 2
#endif
#endif

#ifdef ESP32_S2_MINI
#pragma message("Info : ESP32_S2_MINI")
#define LED_GPIO LED_BUILTIN    
#endif

#ifdef ESP32_S3_ZERO
 #pragma message("Info : ESP32_S3_ZERO")
 #define NEOPIXEL 21
#endif

#ifdef M5_COREINK
 #pragma message("Info : M5Stack CoreInk Module")
 #define LED_GPIO 10
#endif

const char* SYS_Version = "V 0.9.6";
const char* SYS_CompileTime =  __DATE__;
static String  SYS_IP = "0.0.0.0";

#ifdef WEB_APP
// internal Webserver                                
AsyncWebServer webserver(80);
#endif

WiFiClient wificlient; // for Tibber-Pulse und Shelly oder Tasmota Relais REST-Interface *and* ModbusTCPClient

#ifdef LUX_WEBSERVICE
LuxWebsocket luxws; // Luxtronik Webservice
#endif

#if defined SML_TIBBER || defined SML_TASMOTA
// Energy-Meter data from Tibber-Pulse
SMLdecode smldecoder;
#endif

#ifdef MQTT_CLIENT
PicoMQTT::Client mqtt;
#endif

#ifdef SHI_MODBUS
LuxModbusSHI modbusSHI;
#endif

// fetch String;
String sFetch;

static bool _bNewDay = false;

// ntp client
const char* TimeServerLocal = "192.168.2.1";
const char* TimeServer      = "europe.pool.ntp.org";
const char* TimeZone        = "CET-1CEST,M3.5.0,M10.5.0/3";       // Central Europe
ESP32ntp ntpclient;

const long   TimerFastDuration = 300;
long   TimerFast = 0;
const long   TimerSlowDuration   = 2000;   
long   TimerSlow = 0;    

#ifdef ESP32_S3_ZERO
static char neopixel_color = 'w';
#define  setcolor(...) neopixel_color = __VA_ARGS__
#else
#define setcolor(...)
#endif

/// @brief  set builtin LED
/// @param i = HIGH / LOW
void setLED(uint8_t i)
{
#ifndef ESP32_S3_ZERO
 digitalWrite(LED_GPIO, i);
#else
  if (i==0)
  {
    neopixelWrite(NEOPIXEL,0,0,0); // off
  }
  else
  {
    switch (neopixel_color)
    {
    case 'r': 
      neopixelWrite(NEOPIXEL,6,0,0); // red
      break;
    case 'g':
      neopixelWrite(NEOPIXEL,0,6,0); // green
      break;
    case 'b':
      neopixelWrite(NEOPIXEL,0,0,6); // blue
      break;
    case 'y':
       neopixelWrite(NEOPIXEL,4,2,0); // yellow
      break;
    case 'w':
      neopixelWrite(NEOPIXEL,2,2,2); // white
      break;
    default:
       break;
    }
  }
#endif
}

uint8_t blnk=0;
static void blinkLED()
{
  blnk = !blnk;
  setLED(blnk);
}
////////////////////////////////////////////
/// @brief init builtin LED
////////////////////////////////////////////
inline void initLED()
{
 #ifndef ESP32_S3_ZERO
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, HIGH);
#else
  neopixel_color='w';
#endif
  setcolor('w'); //white
  setLED(1);
}

void inline initFS()
{
  if (!myFS.begin())
  {
   debug_println("*** ERROR:FS Mount failed");
  } 
  else
  {
   debug_println("* INFO:FS Mount succesfull");
  }
}

 

//////////////////////////////////////////////////////
/// @brief  expand Class "FileVarStore" with variables
//////////////////////////////////////////////////////
class WPFileVarStore final: public FileVarStore 
{
 public:  
  // Device-Parameter
   String varDEVICE_s_name  = "ESP_LUXTRONIK2";
  // Wifi-Parameter
   String varWIFI_s_mode    = "AP"; // STA=client connect with Router,  AP=Access-Point-Mode (needs no router)
   String varWIFI_s_password= "";
   String varWIFI_s_ssid    = "espluxtronik2";

   String varLUX_s_url      = "192.168.2.101";
   String varLUX_s_password = "999999";

  #ifdef MQTT_CLIENT
   String varMQTT_s_url       = "192.168.2.22";
   String varMQTT_s_shellyht = "shellies/shellyhtg3-wohnzimmer/status/temperature:0";
  #endif

#if defined EPEX_PRICE || defined SG_READY
   int    varEPEX_i_low     = 22;   // cent per kwh
   int    varEPEX_i_high    = 26;   // cent per kwh
   int    varCOST_i_mwst    = 19;   // MwSt (=tax percent) of hour price
   float  varCOST_f_fix     = 17.2; // fix part of hour price in cent
#endif
#if defined SML_TASMOTA || defined SML_TIBBER
   String varSML_s_url      = "";
   String varSML_s_user     = "";
   String varSML_s_password = "";
#endif
#ifdef SG_READY
   String varSG_s_rule1     = "00,00,0,0,FIX"; // start_hour, stop_hour, val1, val2, rulemode ("FIX, "EPEX_LOWHOUR", "EPEX_HIGHHOUR", "EPEX_HIGHLIMIT" EPEX_LOWLIMIT)
   String varSG_s_rule2     = "00,00,0,0,FIX";
   String varSG_s_rule3     = "00,00,0.0,FIX";
   String varSG_s_rule4     = "00,00,0,0,FIX";
   String varSG_s_rule5     = "00,00,0,0,FIX";
   String varSG_s_rule6     = "00,00,0,0,FIX";

   String varSG_s_out1      = "GPIO_12";
   String varSG_s_out2      = "GPIO_10";
   String varSG_s_sg1       = "";        //  http://192.168.2.108/fetch?imax=6; for my ABL-Wallbox ESP Project     
   String varSG_s_sg2       = "";        //  http://192.168.2.137/cm?cmnd=Power1%201 Tasmota Relais1 1 on    (replace % with # in config)
   String varSG_s_sg3       = "";        //  http://192.168.2.137/cm?cmnd=Power1%200 Tasmota Relais1 2 off   ("                             ")
   String varSG_s_sg4       = "";        //  http://192.168.2.137/cm?cmnd=Backlog%20Power1%201%3BPower2%200   Tasmota Relais1=on, Relais2=off (replace % with # in config) switch Relais 1 and Relais 2 
#endif
#ifdef SHI_MODBUS
uint16_t varSHI_i_pcsp1      = 14;
uint16_t varSHI_i_pcsp2      = 14;
uint16_t varSHI_i_pcsp3      = 14;
uint16_t varSHI_i_pcsp4      = 14;
#endif


 protected:
   void GetVariables() 
   {
     varDEVICE_s_name     = GetVarString(GETVARNAME(varDEVICE_s_name));
     varWIFI_s_mode       = GetVarString(GETVARNAME(varWIFI_s_mode)); //STA or AP
     varWIFI_s_password   = GetVarString(GETVARNAME(varWIFI_s_password));
     varWIFI_s_ssid       = GetVarString(GETVARNAME(varWIFI_s_ssid));
#if defined SML_TASMOTA || defined SML_TIBBER
     varSML_s_url         = GetVarString(GETVARNAME(varSML_s_url));
     varSML_s_user        = GetVarString(GETVARNAME(varSML_s_user));
     varSML_s_password    = GetVarString(GETVARNAME(varSML_s_password));
#endif
     varLUX_s_url         = GetVarString(GETVARNAME(varLUX_s_url));
     varLUX_s_password    = GetVarString(GETVARNAME(varLUX_s_password));

#ifdef SG_READY
     varEPEX_i_low        = GetVarInt(GETVARNAME(varEPEX_i_low));
     varEPEX_i_high       = GetVarInt(GETVARNAME(varEPEX_i_high));

     varSG_s_rule1         = GetVarString(GETVARNAME(varSG_s_rule1));
     varSG_s_rule2         = GetVarString(GETVARNAME(varSG_s_rule2));
     varSG_s_rule3         = GetVarString(GETVARNAME(varSG_s_rule3));
     varSG_s_rule4         = GetVarString(GETVARNAME(varSG_s_rule4));
     varSG_s_rule5         = GetVarString(GETVARNAME(varSG_s_rule5));
     varSG_s_rule6         = GetVarString(GETVARNAME(varSG_s_rule6));

     varSG_s_out1          = GetVarString(GETVARNAME(varSG_s_out1));
     varSG_s_out2          = GetVarString(GETVARNAME(varSG_s_out2));
     varSG_s_sg1           = GetVarString(GETVARNAME(varSG_s_sg1));
     varSG_s_sg2           = GetVarString(GETVARNAME(varSG_s_sg2));
     varSG_s_sg3           = GetVarString(GETVARNAME(varSG_s_sg3));
     varSG_s_sg4           = GetVarString(GETVARNAME(varSG_s_sg4));
#endif
#if defined SML_TASMOTA || defined SML_TIBBER
     varSML_s_url         = GetVarString(GETVARNAME(varSML_s_url));
     varSML_s_password    = GetVarString(GETVARNAME(varSML_s_password));
     varSML_s_user        = GetVarString(GETVARNAME(varSML_s_user));
#endif 
#ifdef SHI_MODBUS
     varSHI_i_pcsp1     = GetVarInt(GETVARNAME(varSHI_i_pcsp1),12);
     varSHI_i_pcsp2     = GetVarInt(GETVARNAME(varSHI_i_pcsp2),12);
     varSHI_i_pcsp3     = GetVarInt(GETVARNAME(varSHI_i_pcsp3),12);
     varSHI_i_pcsp4     = GetVarInt(GETVARNAME(varSHI_i_pcsp4),12);
#endif
#ifdef MQTT_CLIENT      
     varMQTT_s_url       = GetVarString(GETVARNAME(varMQTT_s_url));
     varMQTT_s_shellyht  = GetVarString(GETVARNAME(varMQTT_s_shellyht)); 
#endif
   }
};

WPFileVarStore varStore;



#ifdef MQTT_CLIENT
char _buf[30];
XPString s(_buf,30);
char _bufval[6];
XPString sval(_bufval,6);
void inline initMQTT()
{
  // MQTT settings can be changed or set here instead
  mqtt.host = varStore.varMQTT_s_url;
  mqtt.client_id = "esplux" + WiFi.macAddress();
  // Subscribe to a topic and attach a callback: shelly HT3
  mqtt.subscribe(varStore.varMQTT_s_shellyht, [](const char * topic, const char * payload)
  {
    debug_printf("MQTT: '%s' = %s\n", topic, payload); 
    AsyncWebLog.printf("MQTT: %s: %s\r\n", topic, payload);
    s = payload;
    s.substringBeetween(sval,"tC",4,",",0);
    
    AsyncWebLog.printf("Room-Temp: (%s) \r\n", sval.c_str());
    luxws.tempRoom = atof(sval);
  });
  mqtt.begin();
}
#endif


#ifdef SG_READY
void setSGreadyOutput(uint8_t mode, uint8_t hour=24);
#endif

#if defined EPEX_PRICE || defined SG_READY
//////////////////////////////////////////////////////
/// @brief  expand Class "FileVarStore" with variables
//////////////////////////////////////////////////////
class SmartGridEPEX : public SmartGrid
{ 
  public:
  SmartGridEPEX(const char* epex) : SmartGrid(epex) {}

#ifndef M5_COREINK
  /// @brief 
  /// @return 
  bool getAppRules() final
  {

    for (size_t i = 0; i < SG_HOURSIZE; i++)
    {
      setHour_iVarSGready(i, DEFAULT_SGREADY_MODE);
    }
    bRule_OFF = false; // is set at rule "OFF"
#ifdef SG_READY
    calcSmartGridfromConfig(varStore.varSG_s_rule1.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule2.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule3.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule4.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule5.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule6.c_str());  
#endif
   return true;
  }

#ifdef SG_READY
  /// @brief set hour output for Application
  /// @param hour 
  void setAppOutputFromRules(uint8_t hour) final
  {
    setSGreadyOutput(getHour_iVarSGready(hour));
  }
#endif
#endif


  /// @brief your individual calculation for Enduserprice (here: Tibber DE)
  /// @param rawprice 
  /// @return 
  float calcUserkWhPrice(float rawprice)
  {
    float f = (rawprice / 10.0) + (rawprice*varStore.varCOST_i_mwst/1000.0) +  varStore.varCOST_f_fix;
    //debug_printf("Epex-price:%f\r\n",rawprice);
    //debug_printf("User-price:%f\r\n",f);
    return f;
  }

#ifdef CALC_HOUR_ENERGYPRICE
  float getUserkWhFixPrice()
  { 
   return varStore.varCOST_f_kwh_fix;
  }
#endif
};

#define URL_ENERGY_CHARTS "api.energy-charts.info" 
SmartGridEPEX smartgrid(URL_ENERGY_CHARTS);
#endif

#ifdef SG_READY
/// @brief  Post a REST message to a web-device instaed of switching a Relais-Output-Pin
/// @param s  REST url
/// @return 
bool sendSGreadyURL(String s)
{
  HTTPClient http;
  
  if (s.startsWith("http:") == false)
  {
    AsyncWebLog.printf("No HTTP sendSGreadString:  %s\r\n" ,s.c_str());
     return false;   
  }

  AsyncWebLog.printf("sendSGreadURL: %s\r\n", s.c_str());
  int getlength;
  s.replace('#','%'); // because % % is used as html insert marker
  http.setConnectTimeout(800); // 16.02.2025 fix: if url not found or down
  http.begin(wificlient, s);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) 
  {
    getlength = http.getSize();
    if (getlength > 0)
    {
      AsyncWebLog.printf("send OK: SG ready url: %s \r\n",s.c_str());
      http.end();
      return true;
    }
  }
  else 
  {
    AsyncWebLog.printf("ERROR: SG ready url: %s\r\n", s.c_str());
    debug_printf("sendSGreadyURL httpResponseCode: %d\r\n", httpResponseCode);
  }
  // Free resources
  http.end();

  return false;
}

/// @brief internel helper function
/// @param val of PC-Setpoint
void setSHIPCSetpoint(uint val)
{
#ifdef SHI_MODBUS
    modbusSHI.setPCSetpoint(val);
#endif
}


uint getSHIPCSetpoint(uint8_t sgrmode)
{
  uint setpoint = 10;
#ifdef SHI_MODBUS
  switch (sgrmode)
  {
  case 1:
    setpoint = varStore.varSHI_i_pcsp1;
    break;
  case 2:
    setpoint = varStore.varSHI_i_pcsp2;
    break;
  case 3:
    setpoint = varStore.varSHI_i_pcsp3;
    break;
  case 4:
     setpoint = varStore.varSHI_i_pcsp4;
    break;
  default:
    break;
  }
#endif
  return setpoint;
}


/// @brief 
/// @param mode SmartGrid-Mode
/// @param h    hour to set SmartGrid-Mode
void setSGreadyOutput(uint8_t mode, uint8_t hour)
{
  debug_printf("SetSGSGreadyOutput: %d hour:%d\r\n",mode, hour);
  if (hour > 23)
  { hour = ntpclient.getTimeInfo()->tm_hour;}

#ifdef WEB_APP
  AsyncWebLog.printf("SGreadyMode: %d  hour:%d\r\n",mode, hour);   
#endif
  smartgrid.setHour_iVarSGready(hour, mode); // override if value has changed
  String sURL;
  sURL.reserve(50);
  
  // activate switch only for actual hour
  if (hour != ntpclient.getTimeInfo()->tm_hour)
  {
    return;
  }

#ifdef SG_READY
  switch (mode)
  {
  case 1:
    setcolor('b');
    sendSGreadyURL(varStore.varSG_s_sg1);
#ifdef SHI_MODBUS
    setSHIPCSetpoint(varStore.varSHI_i_pcsp1);
#endif
    setRelay(1,1);
    setRelay(2,0);
    
    break;
  case 2:
    setcolor('g');
    sendSGreadyURL(varStore.varSG_s_sg2);
#ifdef SHI_MODBUS
    setSHIPCSetpoint(varStore.varSHI_i_pcsp2);
#endif
    setRelay(1,0);
    setRelay(2,0);
    break;
  case 3:
    setcolor('y');
    sendSGreadyURL(varStore.varSG_s_sg3);
#ifdef SHI_MODBUS
    setSHIPCSetpoint(varStore.varSHI_i_pcsp3);
#endif
    setRelay(1,0);
    setRelay(2,1);
    break;
  case 4:
    setcolor('r');
    sendSGreadyURL(varStore.varSG_s_sg4);
#ifdef SHI_MODBUS
    setSHIPCSetpoint(varStore.varSHI_i_pcsp4);
#endif
    setRelay(1,1);
    setRelay(2,1);
    break;  
  default:
    break;
  }
#elif defined LED_PRICE
   if (smartgrid.getUserkWhPrice(h) > varStore.varEPEX_i_high)
   {
     setcolor('r');
   }
   else if (smartgrid.getUserkWhPrice(h) > varStore.varEPEX_i_low)
   {
     setcolor('y');
   }
   else
  {
    setcolor('g');
  }
#endif
}
// endif SG_READY
#endif 


static String readString(File s) 
{
  String ret;
  int c = s.read();
  while (c >= 0) {
    ret += (char)c;
    c = s.read();
  }
  return ret;
}   

void initFileVarStore()
{
  varStore.Load();
}       

//////////////////////////////////////////
/// @brief Init Wifi
/////////////////////////////////////////
void initWiFi()
{
#ifdef MINI_32
   WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG,0); // brownout problems
#endif   
   if (varStore.varWIFI_s_mode == "STA")
   {
    debug_printf("INFO-WIFI:STA-Mode\r\n");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(varStore.varDEVICE_s_name.c_str());
    WiFi.begin(varStore.varWIFI_s_ssid.c_str(), varStore.varWIFI_s_password.c_str());
    
    int i = 0;
    delay(200);
    debug_printf("SSID:%s connecting\r\n", varStore.varWIFI_s_ssid);
    ///debug_printf("Passwort:%s\r\n", varStore.varWIFI_s_Password);
    while (!WiFi.isConnected())
    {
        debug_print(".");
        blinkLED();
        i++;  
        delay(400);
        if (i > 20)
        {
          ESP.restart();
        }
    }

    #if defined ESP32_S3_ZERO || defined MINI_32 || defined M5_COREINK
     WiFi.setTxPower(WIFI_POWER_7dBm);// brownout problems with some boards or low battery load for M5_COREINK
     //WiFi.setTxPower(WIFI_POWER_15dBm);// Test 15dB
    #elseif defined DEBUG_PRINT && (defined ESP32_RELAY_X4 || defined ESP32_RELAY_X2)
    //WiFi.setTxPower(WIFI_POWER_MINUS_1dBm); // decrease power over serial TTY-Adapter
    #else
     //WiFi.setTxPower(WIFI_POWER_19dBm);
     WiFi.setTxPower(WIFI_POWER_19_5dBm);
    #endif

    Serial.println("CONNECTED!");
    Serial.printf("WiFi-Power:%d\r\n",WiFi.getTxPower());
    Serial.printf("WiFi-RSSI:%d\r\n",WiFi.RSSI());
      
    SYS_IP = WiFi.localIP().toString();
    Serial.printf("IP Address: %s", SYS_IP);

    if (!ntpclient.begin(TimeZone, TimeServerLocal, TimeServer))
    {
     ESP.restart();
    }
   }
   else
   {
    Serial.println("INFO-WIFI:AP-Mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("esp-luxtronik");   
    Serial.printf("IP Address: %s", WiFi.softAPIP().toString());
   }
}     

void testWiFiReconnect()
{
  // Test if wifi is lost from router
  if ((varStore.varWIFI_s_mode == "STA") && (WiFi.status() != WL_CONNECTED))
    {
     debug_println("Reconnecting to WiFi...");
     delay(300);
     if (!WiFi.reconnect())
     {
      delay(200);
      ESP.restart();
     } 
   }
}

#ifdef WEB_APP
// -------------------- WEBSERVER -------------------------------------------
// --------------------------------------------------------------------------

/*
Absturz:
TIME: 20:37:44
func:setHtmlVar: DEVICEID
func:setHtmlVar: DEVICEID
func:setHtmlVar: LUX_IP
func:setHtmlVar: DEVICEID
Guru Meditation Error: Core  1 panic'ed (InstrFetchProhibited). Exception was unhandled.

... tritt setzt nicht mehr auf

*/

/// @brief replace placeholder "%<variable>%" in HTML-Code
/// @param var 
/// @return String
String sLocal;
String setHtmlVar(const String& var)
{
  sLocal = "";
  debug_print("func:setHtmlVar: ");
  debug_println(var);
 
  if (var == "CONFIG") // read config.txt
  {
    if (!myFS.exists("/config.txt")) 
    {
     return String(F("Error: File 'config.txt' not found!"));
    }
    // read "config.txt" 
    fs::File configfile = myFS.open("/config.txt", "r");   
    String sConfig;     
    if (configfile) 
    {
      sConfig = readString(configfile);
      configfile.close();
    }
    else 
    { // no "config.txt"
      sConfig = "";
    }
    return sConfig;
  } 
  else
  if (var== "DEVICEID")
  {
    return varStore.varDEVICE_s_name;
  }
  else
  if (var == "INFO")
  {
     sLocal =    "Version    :";
     sLocal += SYS_Version;  
     sLocal += "\nPlatform   :";
     sLocal +=  ESP.getChipModel();
     sLocal += "\nBuild-Date :";
     sLocal +=  F(__DATE__);
     sLocal += "\nIP-Addr    :";
     sLocal += SYS_IP;
     sLocal += "\nRSSI       :";
     sLocal += WiFi.RSSI();
     sLocal += "\nTemp.      :";
     sLocal += temperatureRead();   
     sLocal += "\nFreeHeap   :";
     sLocal += ESP.getFreeHeap();
     sLocal += "\nMinFreeHeap:";
     sLocal += ESP.getMinFreeHeap();
#ifdef SG_READY
     sLocal += "\n\nEPEX NEXT DATE:";
     sLocal +=  smartgrid.getWebDate(true); //sEPEXdateNext;
     sLocal +=  "\n";
     sLocal += smartgrid.getWebHourValueString(true);

     sLocal += "\n\nEPEX TODAY:";
     sLocal +=  "\n";
     sLocal += smartgrid.getWebHourValueString(false); //sEPEXPriceToday;
     //sLocal += "\n---\n\n";
#endif
     return sLocal;
  }
#ifdef EPEX_PRICE
  else
  if (var =="PRICE_LOW")
  {
     return String(varStore.varEPEX_i_low);      
  }
 else
  if (var =="PRICE_HIGH")
  {
     return String(varStore.varEPEX_i_high);     
  }
  else
  if (var == "COST_MWST")
  {
    return String(varStore.varCOST_i_mwst);
  }
  else
  if (var == "COST_FIX")
  {
    return String(varStore.varCOST_f_fix);
  }
  else
  if (var == "EPEX_ARRAY")
  {
    //return  smartgrid.getWebHourValueString(false);                      //EPEXPriceToday; // jetzt TODAY !!!
    String ret = smartgrid.getWebHourValueString(false);
    if (smartgrid.getWebHourValueString(true).length() > 0)
    {
     ret += ", ";
     ret += smartgrid.getWebHourValueString(true);
    }
    return ret;
  }
  else
  if (var == "EPEX_DATE")
  {
    String ret = smartgrid.getWebDate(false);
    ret += "   -    ";
    ret += smartgrid.getWebDate(true);
    return ret;
  }
  else
  if (var == "SGMODE")
  {
    //switch (SmartGridReadyStatus)
    switch (smartgrid.getHour_iVarSGready(ntpclient.getTimeInfo()->tm_hour))
    {
    case 1:
      return "sg1";
      break;
    case 2:
      return "sg2";
      break;
    case 3:
      return "sg3";
      break;
    case 4:
      return "sg4";
      break;
    default:
      break;
    }
    return "";
  }
  else
  if (var == "SGHOURMODE")
  {
    for (size_t i = 0; i < SG_HOURSIZE; i++)
    {
#ifdef SHI_MODBUS
      sLocal += getSHIPCSetpoint(smartgrid.getHour_iVarSGready(i)); // new: 2025-04-16
#else
      sLocal += smartgrid.getHour_iVarSGready(i);
#endif
      if (i < SG_HOURSIZE-1)
      {
        sLocal += ',';
      }
    }
    //sLocal += '0';
    return sLocal;
  }
#ifdef CALC_HOUR_ENERGYPRICE
  else
  if (var == "COSTINFO")
  { 
     sLocal =      "Flex(ct):";
    sLocal += String((smartgrid.getUserkWhPrice(ntpclient.getTimeInfo()->tm_hour)),1);
    sLocal +=   "  Fix (cnt):";
    sLocal += String(smartgrid.getUserkWhFixPrice(),1);
   
    sLocal +=  " Hour-kWh : ";
    sLocal +=  String((smldecoder.getInputkWh() /* valSML_kwh_in */ - smartgrid.hourprice_kwh_start),3);

    sLocal += "\r\n\r\nHour-flex:";
    sLocal += String(smartgrid.getFlexprice_hour(ntpclient.getTimeInfo()->tm_hour),2);
  
    sLocal +=   "  Day-flex :";
    sLocal += String(smartgrid.getFlexprice_monthday(ntpclient.getTimeInfo()->tm_mday),2);
    
    sLocal +=    " Month-flex:";
    sLocal += String(smartgrid.getFlexprice_month(ntpclient.getTimeInfo()->tm_mon),2);

    //sLocal +=     "\r\nSum-flex     : ";
    //sLocal += String(smartgrid.Flexprice_sum + smartgrid.getAktFlexprice(),2);

    sLocal +=    "\r\nHour-fix: ";
    sLocal += String(smartgrid.getFixprice_hour(ntpclient.getTimeInfo()->tm_hour),2);

    sLocal +=     "  Day-fix  :";
    sLocal += String(smartgrid.getFixprice_monthday(ntpclient.getTimeInfo()->tm_mday),2);

    sLocal +=     "  Month-fix:";
    sLocal += String(smartgrid.getFixprice_month(ntpclient.getTimeInfo()->tm_mon) ,2);

    //sLocal +=     "\r\nSum-fix     : ";
    //sLocal += String(smartgrid.fixprice_sum + smartgrid.akt_fixprice,2);
    sLocal += "";

    return sLocal;
  }

  else
  if (var == "PRICE_FIX")
  {
     return String(smartgrid.getUserkWhFixPrice(),1);
  }

  else 
  if  (var == "PRICE_HOUR_FLEX")
  {
     for (size_t i = 0; i < 24; i++)
     {
      //sLocal += String(smartgrid.flexprice_hour[i],2);
      sLocal += String(smartgrid.getFlexprice_hour(i),2);
      if (i < 23)
      {
       sLocal += ", ";
      }
     }
     debug_println(sLocal);
     return sLocal;
  }
  else
  if (var == "PRICE_HOUR_FIX")
  {
     for (size_t i = 0; i <24; i++)
     {
      //sLocal += String(smartgrid.fixprice_hour[i],2);
      sLocal += String(smartgrid.getFixprice_hour(i),2);
      if (i < 23)
      {
       sLocal += ", ";
      }
     }
     return sLocal;
  }
  else
  if (var == "PRICE_MONTHDAY_FLEX")
  {
     for (size_t i = 1; i < 32; i++)
     {
      //sLocal += String(smartgrid.flexprice_monthday[i],2);
      sLocal += String(smartgrid.getFlexprice_monthday(i),2);
      if (i < 31)
      {
       sLocal += ", ";
      }
     }
     return sLocal;
  }
  else
  if (var == "PRICE_MONTHDAY_FIX")
  {
     for (size_t i = 1; i < 32; i++) // begin with "1" !!
     {
      //sLocal += String(smartgrid.fixprice_monthday[i],2);
      sLocal += String(smartgrid.getFixprice_monthday(i),2);
      if (i < 31)
      {
       sLocal += ", ";
      }
     }
     return sLocal;
  }
  else
  if (var == "PRICE_MONTH_FLEX")
  {
     for (size_t i = 0; i < 12; i++)
     {
      //sLocal += String(smartgrid.flexprice_month[i],2);
      sLocal += String(smartgrid.getFlexprice_month(i),2);
      if (i < 11)
      {
       sLocal += ", ";
      }
     }
     return sLocal;
  }
  else
  if (var == "PRICE_MONTH_FIX")
  {
     for (size_t i = 0; i < 12; i++)
     {
      //sLocal += String(smartgrid.fixprice_month[i],2);
      sLocal += String(smartgrid.getFixprice_month(i),2);
      if (i < 11)
      {
       sLocal += ", ";
      }
     }
     return sLocal;
  }
  else
  if (var == "COST_AKT_MONTH") // read config.txt
  {
    if (!myFS.exists("/cost_akt_month.txt")) 
    {
     return String(F("Error: File 'cost_akt_month.txt' not found!"));
    }
    // read "config.txt" 
    fs::File cfile = myFS.open("/cost_akt_month.txt", "r");   
    String sData;     
    if (cfile) 
    {
      sData = readString(cfile);
      cfile.close();
    }
    else 
    { // no "config.txt"
      sData = "";
    }
    return sData;
  }
  else
  if (var == "COST_AKT_YEAR") // read config.txt
  {
    if (!myFS.exists("/cost_akt_year.txt")) 
    {
     return String(F("Error: File 'cost_akt_year.txt' not found!"));
    }
    // read "config.txt" 
    fs::File cfile = myFS.open("/cost_akt_year.txt", "r");   
    String sData;     
    if (cfile) 
    {
      sData = readString(cfile);
      cfile.close();
    }
    else 
    { // no "config.txt"
      sData = "";
    }
    return sData;
  }
  #endif
  // EPEX_PRICE
  #endif 
  else
  if (var == "LUX_IP")
  {
    return varStore.varLUX_s_url;
  }
#ifdef SHI_MODBUS
  else
  if (var == "SHIHEOFFSET")
  {
    //return "1.0";
    return String(float(modbusSHI.getHeatOffsetX10()/10.0), 1);
  }
  else
  if (var == "SHIPCSETPOINT")
  {
    //return "1.6";
    return String(float(modbusSHI.getPCSetpoint())/10.0, 1);
  }
#endif

  return "";
}


void notFound(AsyncWebServerRequest *request) 
{
    request->send(404, "text/plain", "Not found");
}

void initWebServer()
{ 
  
  sFetch.reserve(250);
  // --------------- Base Pages:----------------------------------
  //Route for root / web page
  webserver.on("/",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/index.html", String(), false, setHtmlVar);
  });
  //Route for root /index web page
  webserver.on("/index.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/index.html", String(), false, setHtmlVar);
  });
 
  //Route for setup web page
  webserver.on("/setup.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/setup.html", String(), false, setHtmlVar);
  });
  //Route for config web page
  webserver.on("/config.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/config.html", String(), false, setHtmlVar);
  });
  //Route for Info-page
  webserver.on("/info.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/info.html", String(), false, setHtmlVar);
  });

  // config.txt GET
  webserver.on("/reboot.html", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(404, "text/plain", "RESTART !");
    delay(300);
    //saveHistory();
    ESP.restart();
  });
  //----------------------------------------------------------------------

  // Temp Icons:
  webserver.on("/temp_inside.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/temp_inside.png", String(), false, setHtmlVar);
  });
  
  webserver.on("/temp_outside.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/temp_outside.png", String(), false, setHtmlVar);
  });

  //Route for Luxtronik details page
  webserver.on("/luxtronik.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/luxtronik.html", String(), false, setHtmlVar);
  });

  //Route for meter page
  webserver.on("/meter.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/meter.html", String(), false, setHtmlVar);
  });
  
  //.. some code for the navigation icons
  webserver.on("/home.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/home.png", String(), false);
  });
  webserver.on("/file-list.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/file-list.png", String(), false);
  });

  webserver.on("/settings.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {

  webserver.on("/smartgrid.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(myFS, "/smartgrid.html", String(), false, setHtmlVar);
  });
  request->send(myFS, "/settings.png", String(), false);
});


#ifdef SG_READY
  webserver.on("/sgready.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(myFS, "/sgready.html", String(), false, setHtmlVar);
  });
  webserver.on("/reload.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/reload.png", String(), false);
  });
#endif

// Smart-Home-Interface --------------------------------
  webserver.on("/shi.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
    // !!! WORKAROUNT !!! ... bis neues JSON interface läuft
    luxws.setpollstate(WS_POLL_STATUS::NO_POLLING); 
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    request->send(myFS, "/shi.html", String(), false, setHtmlVar);
  });

  webserver.on("/shi.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(myFS, "/shi.png", String(), false);
  });
  

  webserver.on("/fetch", HTTP_GET, [](AsyncWebServerRequest *request)
  {
                                                                      // Index:
    sFetch = ntpclient.getTimeString();                               // 0 = Time: 00:00
    sFetch += ",";
#ifdef SG_READY
    sFetch += smartgrid.getHour_iVarSGready(ntpclient.getTimeInfo()->tm_hour);// 1 = sg_status
#else
    sFetch += "-";
#endif
     sFetch += ',';
/* z.Z. Wert von Anlage anzeigen !
#ifdef DS100_MODBUS
    sFetch += valDS100_L1_W / 1000;                                    // 2 DS100 Power L1
    sFetch += ',';
#else 
*/
    sFetch += luxws.getval(LUX_VAL_TYPE::POWER_IN, true);              // 2 Power In
    sFetch += luxws.getCSVfetch(true);                                 // 3...36

    sFetch += ",end";     

    //debug_printf("server.on fetch: %s",sFetch.c_str());
    request->send(200, "text/plain", sFetch);  
  });

  // Route for style-sheet
  webserver.on("/style.css",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/style.css", String(), false);
  });

  
  // ...a lot of code only for icons and favicons ;-))
  webserver.on("/manifest.json",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/manifest.json", String(), false);
  });
  webserver.on("/favicon.ico",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/favicon.ico", String(), false);
  });
  webserver.on("/apple-touch-icon.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/apple-touch-icon.png", String(), false);
  });
  webserver.on("/android-chrome-192x192.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/android-chrome-192x192.png", String(), false);
  });
  webserver.on("/android-chrome-384x384.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/android-chrome-384x384.png", String(), false);
  });
  

  // App-Icons
  webserver.on("/current.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/current.png", String(), false);
  });
  webserver.on("/sg.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/sg.png", String(), false);
  });
  webserver.on("/sgready.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/sgready.png", String(), false);
  });
  webserver.on("/heatpump.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/heatpump.png", String(), false);
  });
   webserver.on("/luxtronik.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/luxtronik.png", String(), false);
  });
  webserver.on("/homeauto.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/homeauto.png", String(), false);
  });

  // at index.html Luxtronic status
  webserver.on("/poweroff.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/poweroff.png", String(), false);
  });
  
  webserver.on("/hotwater.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/hotwater.png", String(), false);

  }); webserver.on("/radiator.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/radiator.png", String(), false);

  }); webserver.on("/defrost.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(myFS, "/defrost.png", String(), false);
  });

  // fetch GET
  webserver.on("/fetchmeter", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    sFetch = "";
#ifdef DS100_MODBUS   
    sFetch = valDS100_L1_W;       // 0
    sFetch+= ',';
    sFetch+= valDS100_L2_W;       // 1
    sFetch+= ',';               
    sFetch+= valDS100_L3_W;       // 2
    sFetch+= ',';
    sFetch+= valDS100_L1_KWH*10;  // 3
    sFetch+= ',';
    sFetch+= valDS100_L2_KWH*10;  // 4
    sFetch+= ',';
    sFetch+= valDS100_L3_KWH*10;  // 5
    sFetch+= ',';
    sFetch+= valDS100_mV;         //6
#else
    sFetch = "--,--,--,--,--,--,--";
#endif
#if defined SML_TIBBER || defined SML_TASMOTA
    sFetch+= ',';
    sFetch+= smldecoder.getWatt();  // 7
    sFetch+= ',';
    sFetch+= smldecoder.getInputkWh(); // 8
    sFetch+= ',';
    sFetch+= smldecoder.getOutputkWh(); // 9
#else
    sFetch+= ",--,--,--";
#endif
    request->send(200, "text/plain", sFetch);
    //debug_println("server.on /fetchmeter: "+ s);
  });

  // -------------------- POST --------------------------------------------------
   // sgready.html POST
   #ifdef SG_READY
   webserver.on("/sgready.html",          HTTP_POST, [](AsyncWebServerRequest *request)
   {
    uint8_t i = 0;
    int iVal  = request->arg(i).toInt();
    const String sArg = request->argName(0);
    debug_printf(      "sgready POST: arg: %s  value:%d\r\n",sArg.c_str(), iVal);
    //AsyncWebLog.printf("sgready POST: arg: %s  value:%d\r\n",sArg.c_str(), iVal);
 
    if (sArg == "sg1")
    {
       setSGreadyOutput(1, iVal);
    }
    else
    if (sArg == "sg2")
    {
       setSGreadyOutput(2, iVal);
    }
    else
    if (sArg == "sg3")
    {
       setSGreadyOutput(3, iVal);
    }
    else
    if (sArg == "sg4")
    {
       setSGreadyOutput(4, iVal);
    }
    else 
    if (sArg == "sgsreload")
    {
     debug_println("sgsreload");
     smartgrid.getAppRules();   // old: setSmartGridRules(); // calulate rules from config
     smartgrid.setAppOutputFromRules(ntpclient.getTimeInfo()->tm_hour);
    }

    request->send(myFS, "/sgready.html", String(), false, setHtmlVar); 
   });
   #endif      

    // config.html POST
    webserver.on("/config.html",          HTTP_POST, [](AsyncWebServerRequest *request)
    {
     //debug_println("Argument: " + request->argName(0));
     //debug_println("Value: ");
     uint8_t i = 0;
     String s  = request->arg(i);
     debug_println(s);
     if (request->argName(0) == "saveconfig")
     {
         varStore.Save(s);
         varStore.Load();
#ifdef SG_READY
         smartgrid.refreshWebData(true); // new: 30.10.2024
         smartgrid.getAppRules();   // old: setSmartGridRules(); // calulate rules from config
#endif
     }
     request->send(myFS, "/config.html", String(), false, setHtmlVar);
    });

#ifdef SHI_MODBUS
    // config.html POST
    webserver.on("/shi.html",          HTTP_POST, [](AsyncWebServerRequest *request)
    {
         debug_printf("SHI- Argument: %s \r\n",request->argName(0));
         String sName = request->argName(size_t(0));
         String sVal  = request->arg(size_t(0));

         debug_printf("SHI-Arg: %s, Value: %s\r\n",sName.c_str(), sVal.c_str());
         if (sName == "range_temp")
         {
           debug_println("[SHI] SET-TEMP-OFFSET !!!");
           modbusSHI.setHeatOffset(int16_t(sVal.toFloat()*10.0));
         }
         else
         if (sName == "range_kw")
         {
          debug_println("[SHI] SET-KW !!!");
          modbusSHI.setPCSetpoint(int16_t(sVal.toFloat()*10.0));
         }
         request->send(myFS, "/shi.html", String(), false, setHtmlVar); 
    });
 #endif
  
  AsyncWebLog.begin(&webserver);
  AsyncWebOTA.begin(&webserver);
  webserver.onNotFound(notFound);
  webserver.begin();
  debug_println("Init-Webserver OK!")
  }
#endif // WEB_APP


////////////////////////////////////////////////////
/// @brief setup
////////////////////////////////////////////////////
void setup() 
{
  Serial.begin(115200);                                           
  delay(500);
  Serial.println("***START***");
  delay(500);
  initLED();
  initRelay();
  initFS();
  initFileVarStore(); 
  setcolor('b'); // blue
  initWiFi();
  initWebServer(); 
  setLED(0);
  delay(500);
#ifdef EPEX_PRICE
  smartgrid.init();
#endif
#ifdef SML_TIBBER
  smldecoder.init(varStore.varSML_s_url.c_str(), varStore.varSML_s_user.c_str(), varStore.varSML_s_password.c_str());
#endif
#ifdef DS100_MODBUS
  DS100Init();
#endif
#ifdef MQTT_CLIENT
  initMQTT();
#endif
#ifdef SHI_MODBUS
  modbusSHI.init(varStore.varLUX_s_url.c_str());
#endif

  luxws.init(varStore.varLUX_s_url.c_str(), varStore.varLUX_s_password.c_str());
  delay(1000);
  Serial.println("***INIT-END***");
}

static uint old_minute, old_hour, old_day = 99;
///////////////////////////////////// TimePageSprite.drawString(20,10,buf, &Font0);///////////
/// @brief loop
////////////////////////////////////////////////
void loop() 
{
   luxws.loop();
#ifdef MQTT_CLIENT
   mqtt.loop();
#endif

   if (millis() - TimerSlowDuration > TimerSlow) 
   {
    TimerSlow = millis();                      // Reset time for next event
    testWiFiReconnect();
    ntpclient.update();
    tm* looptm = ntpclient.getTimeInfo();

    debug_printf("\r\nTIME: %s:%02d\r\n", ntpclient.getTimeString(), looptm->tm_sec);
    AsyncWebLog.printf("TIME: %s:%02d\r\n", ntpclient.getTimeString(), looptm->tm_sec);
    
#ifdef MQTT_CLIENT
    AsyncWebLog.printf("Raum-Temp: %2.1f \r\n", luxws.tempRoom);
#endif
#ifdef SML_TIBBER
    smldecoder.read(); 
    luxws.power_Main_InMeter = smldecoder.getWatt();
#endif  
#ifdef DS100_MODBUS
    DS100read();
    luxws.energy_Sub_InMeter =  valDS100_L1_KWH;
#endif
#ifdef EPEX_PRICE
    time_t tt = ntpclient.getUnixTime();
    smartgrid.loop(&tt);
    if (smartgrid.bRule_OFF)
    {AsyncWebLog.printf("[SGR] Rule **OFF** !\r\n");}
#endif
#ifdef SHI_MODBUS
    modbusSHI.poll();
    AsyncWebLog.printf("[SHI] Heat-mode:%d val:%d\r\n", modbusSHI.getHeatMode(), modbusSHI.getHeatOffsetX10());
    AsyncWebLog.printf("[SHI] PC-  mode:%d val:%d\r\n", modbusSHI.getPCMode(),   modbusSHI.getPCSetpoint());  
#endif

    /*  ----- !!! WORKAROUND !!! ...bi neues JSON Protokoll realisiert ist keine Websocket Kommunikation !!
    if (looptm->tm_min != old_minute) // Test min
    {
      old_minute = looptm->tm_min;
      luxws.setpollstate(WS_POLL_STATUS::SEND_LOGIN); 
    }

    // jede Stunde neuer Login, da bei Login immer die Bedienung direkt an der Steuerungs-Einheit gestört wird (springt in Menues)
    //if (looptm->tm_hour != old_hour) // Test new hour --> Luxtronik new Login sequence
    //{
    //  old_hour = looptm->tm_hour;
    //  luxws.setpollstate(WS_POLL_STATUS::SEND_LOGIN); 
    //}
    */

    if (looptm->tm_mday != old_day) // Test new Day --> reset dayly COP calculation
    { 
      _bNewDay = true;
      old_day = looptm->tm_mday;
    }

    luxws.poll(_bNewDay);
    _bNewDay = false;
    
    if (luxws.isConnected())
    {
      AsyncWebLog.printf("WS-POLL-State:    \t %s\r\n", luxws.getval(LUX_VAL_TYPE::STATUS_POLL,     false));
      AsyncWebLog.printf("WP-State:         \t %s\r\n", luxws.getval(LUX_VAL_TYPE::STATUS_HEATPUMP, false));
      AsyncWebLog.printf("WP-PowerIn:       \t %s\r\n", luxws.getval(LUX_VAL_TYPE::POWER_IN, false));

      //debug_printf("[LUX]  Abschaltung: %s\r\n",        luxws.getval(LUX_VAL_TYPE::SWITCHOFF_INFO, false));
      //AsyncWebLog.printf("Watt                  \t %d \r\n",smldecoder.getWatt());
      //AsyncWebLog.printf("Leistung OUT-HEAT:    \t %s\r\n", luxws.getval(LUX_VAL_TYPE::ENERGY_OUT_HE, false));
    }
    else
    {
       AsyncWebLog.printf("...wait Luxtronik connecting\r\n");
    }
    /*
    // for test :
    String s = ntpclient.getTimeString();
    s += ':';
    s += String(ntpclient.getTimeInfo()->tm_sec);
    mqtt.publish("luxtronik/time",s.c_str());
    */
  }
  // fast blink
  if (millis() - TimerFastDuration > TimerFast)
  {
    TimerFast = millis();
    blinkLED();
  }

}

