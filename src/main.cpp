#include <Arduino.h>

// Build in libs
#include <Esp.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_rom_gpio.h"
#include <FS.h>
#include <LittleFS.h>                    
#include <WiFi.h>                     
#include <ESPmDNS.h>                   
#include <ESPAsyncWebServer.h>         
#include <HTTPClient.h>                 
#include <base64.h>                    
#include <Preferences.h>

// my libs
#include "FileVarStore.h"
#ifdef WEB_APP
#include "AsyncWebLog.h"
#include "AsyncWebOTA.h"
#endif
#include "ESP32ntp.h"
#include "XPString.h"
#include "SmartGrid.h"
#include "LuxWebsocket.h"
#include "SMLdecode.h"
#include "AsyncWebApp.h"

//external libs
#ifdef SML_JSON
#include "ArduinoJson.h"
#endif

// special for S2 and S4
#if defined ESP_S2_MINI || ESP_S3_ZERO
#include "driver/temp_sensor.h"
#endif

// now set in platformio.ini
//#define DEBUG_PRINT 1   
#ifdef DEBUG_PRINT
#pragma message("Info : DEBUG_PRINT=1")
#define debug_begin(...) Serial.begin(__VA_ARGS__);
#define debug_print(...) Serial.print(__VA_ARGS__);
#define debug_write(...) Serial.write(__VA_ARGS__);
#define debug_println(...) Serial.println(__VA_ARGS__);
#define debug_printf(...) Serial.printf(__VA_ARGS__);
#else
#define debug_begin(...)
#define debug_print(...)
#define debug_printf(...)
#define debug_write(...)
#define debug_println(...)
#endif

#include "relay.h"
#include "ds100.h"

#ifdef ESP32_DEVKIT1
#pragma message("Info : ESP32_DEVKIT")
#ifndef ESP32_RELAY_X4
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

const char* SYS_Version = "V 0.9.2";
const char* SYS_CompileTime =  __DATE__;
static String  SYS_IP = "0.0.0.0";

#ifdef WEB_APP
// internal Webserver                                
AsyncWebServer webserver(80);
#endif


//Luxtronik
LuxWebsocket luxws;

// Energy-Meter data from Tibber-Pulse
SMLdecode smldecoder;
WiFiClient wificlient; // for Tibber-Pulse

// fetch String;
static String sFetch;

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


void inline initLittleFS()
{
  if (!LittleFS.begin())
  {
   debug_println("*** ERROR: LittleFS Mount failed");
  } 
  else
  {
   debug_println("* INFO: LittleFS Mount succesfull");
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

   String varLUX_s_url  = "192.168.2.101";
   String varLUX_s_password = "";

   int    varEPEX_i_low     = 22;   // cent per kwh
   int    varEPEX_i_high    = 26;   // cent per kwh
   int    varCOST_i_mwst    = 19;   // MwSt (=tax percent) of hour price
   float  varCOST_f_fix     = 17.2; // fix part of hour price in cent
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

 protected:
   void GetVariables() 
   {
     varDEVICE_s_name     = GetVarString(GETVARNAME(varDEVICE_s_name));
     varWIFI_s_mode       = GetVarString(GETVARNAME(varWIFI_s_mode)); //STA or AP
     varWIFI_s_password   = GetVarString(GETVARNAME(varWIFI_s_password));
     varWIFI_s_ssid       = GetVarString(GETVARNAME(varWIFI_s_ssid));
    
     varSML_s_url         = GetVarString(GETVARNAME(varSML_s_url));
     varSML_s_user        = GetVarString(GETVARNAME(varSML_s_user));
     varSML_s_password    = GetVarString(GETVARNAME(varSML_s_password));

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

   }
};

WPFileVarStore varStore;

void setSGreadyOutput(uint8_t mode, uint8_t hour=24);
//////////////////////////////////////////////////////
/// @brief  expand Class "FileVarStore" with variables
//////////////////////////////////////////////////////
class SmartGridEPEX : public SmartGrid
{ 
  public:
  SmartGridEPEX(const char* epex) : SmartGrid(epex) {}

#ifndef M5_COREINK
  bool getAppRules() final
  {
    for (size_t i = 0; i < SG_HOURSIZE; i++)
    {
        setHourVar1(i, DEFAULT_SGREADY_MODE);
    }
    calcSmartGridfromConfig(varStore.varSG_s_rule1.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule2.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule3.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule4.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule5.c_str());
    calcSmartGridfromConfig(varStore.varSG_s_rule6.c_str());  
   return true;;
  }

  /// @brief set hour output for Application
  /// @param hour 
  void setAppOutputFromRules(uint8_t hour) final
  {
#ifdef SG_READY
    setSGreadyOutput(this->getHourVar1(hour));
#endif
  }
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

#ifdef SG_READY
/// @brief  Post a REST message to a web-device instaed of switching a Relais-Output-Pin
/// @param s  REST url
/// @return 
bool sendSGreadyURL(String s)
{
  HTTPClient http;
  
  if (s.startsWith("http:") == false)
  {
    AsyncWebLog.println("No HTTP sendSGreadString:" + s);
     return false;   
  }

  AsyncWebLog.println("sendSGreadURL:" + s);
  int getlength;
  s.replace('#','%'); // because % % is used as html insert marker
  http.begin(wificlient, s);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) 
  {
    getlength = http.getSize();
    if (getlength > 0)
    {
      AsyncWebLog.println("send OK: SG ready url:" + s);
      http.end();
      return true;
    }
  }
  else 
  {
    AsyncWebLog.println("ERROR: SG ready url:" + s);
    debug_printf("httpResponseCode: %d\r\n", httpResponseCode);
  }
  // Free resources
  http.end();

  return false;
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
  smartgrid.setHourVar1(hour, mode); // override if value has changed
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
   
    setRelay(1,1);
    setRelay(2,0);
    
    break;
  case 2:
    setcolor('g');
    sendSGreadyURL(varStore.varSG_s_sg2);
   
    setRelay(1,0);
    setRelay(2,0);
    break;
  case 3:
    setcolor('y');
    sendSGreadyURL(varStore.varSG_s_sg3);
  
    setRelay(1,0);
    setRelay(2,1);
    break;
  case 4:
    setcolor('r');
    sendSGreadyURL(varStore.varSG_s_sg4);
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
   if (varStore.varWIFI_s_mode == "AP")
   {
    delay(100);
    Serial.println("INFO-WIFI:AP-Mode");
    WiFi.softAP(varStore.varDEVICE_s_name.c_str());   
    Serial.print("IP Address: ");
    SYS_IP = WiFi.softAPIP().toString();
    Serial.println(SYS_IP);
   }
   else
   {
    debug_printf("INFO-WIFI:STA-Mode\r\n");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(varStore.varDEVICE_s_name.c_str());
    WiFi.begin(varStore.varWIFI_s_ssid.c_str(), varStore.varWIFI_s_password.c_str());
    #if defined ESP32_S3_ZERO || defined MINI_32 || defined M5_COREINK
    WiFi.setTxPower(WIFI_POWER_7dBm);// brownout problems with some boards or low battery load for M5_COREINK
    //WiFi.setTxPower(WIFI_POWER_15dBm);// Test 15dB
    #endif
    #if defined DEBUG_PRINT && (defined ESP32_RELAY_X4 || defined ESP32_RELAY_X2)
    //WiFi.setTxPower(WIFI_POWER_MINUS_1dBm); // decrease power over serial TTY-Adapter
    #endif
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
    delay(300);
    debug_println("CONNECTED!");
    debug_printf("WiFi-Power:%d\r\n",WiFi.getTxPower())
    debug_printf("WiFi-RSSI:%d\r\n",WiFi.RSSI());
      
    SYS_IP = WiFi.localIP().toString();
    debug_print("IP Address: ");
    debug_println(SYS_IP);
    if (!ntpclient.begin(TimeZone, TimeServerLocal, TimeServer))
    {
     ESP.restart();
    }
   }
  return;
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


/// @brief replace placeholder "%<variable>%" in HTML-Code
/// @param var 
/// @return String
String setHtmlVar(const String& var)
{
  
  debug_print("func:setHtmlVar: ");
  debug_println(var);
  sFetch = "";
 
  if (var == "CONFIG") // read config.txt
  {
    if (!LittleFS.exists("/config.txt")) 
    {
     return String(F("Error: File 'config.txt' not found!"));
    }
    // read "config.txt" 
    fs::File configfile = LittleFS.open("/config.txt", "r");   
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
     sFetch =    "Version    :";
     sFetch += SYS_Version;  
     sFetch += "\nPlatform   :";
     sFetch +=  ESP.getChipModel();
     sFetch += "\nBuild-Date :";
     sFetch +=  F(__DATE__);
     sFetch += "\nIP-Addr    :";
     sFetch += SYS_IP;
     sFetch += "\nRSSI       :";
     sFetch += WiFi.RSSI();
     sFetch += "\nTemp.      :";
     sFetch += temperatureRead();   
     sFetch += "\nFreeHeap   :";
     sFetch += ESP.getFreeHeap();
     sFetch += "\nMinFreeHeap:";
     sFetch += ESP.getMinFreeHeap();
#ifdef SG_READY
     sFetch += "\n\nEPEX NEXT DATE:";
     sFetch +=  smartgrid.getWebDate(true); //sEPEXdateNext;
     sFetch +=  "\n";
     sFetch += smartgrid.getWebHourValueString(true);

     sFetch += "\n\nEPEX TODAY:";
     sFetch +=  "\n";
     sFetch += smartgrid.getWebHourValueString(false); //sEPEXPriceToday;
     //sFetch += "\n---\n\n";
#endif
     return sFetch;
  }
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
    switch (smartgrid.getHourVar1(ntpclient.getTimeInfo()->tm_hour))
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
      sFetch += smartgrid.getHourVar1(i); //String(smartgrid_hour[i].var1);
      if (i < SG_HOURSIZE-1)
      {
        sFetch += ',';
      }
    }
    //sFetch += '0';
    return sFetch;
  }
#ifdef CALC_HOUR_ENERGYPRICE
  else
  if (var == "COSTINFO")
  { 
     sFetch =      "Flex(ct):";
    sFetch += String((smartgrid.getUserkWhPrice(ntpclient.getTimeInfo()->tm_hour)),1);
    sFetch +=   "  Fix (cnt):";
    sFetch += String(smartgrid.getUserkWhFixPrice(),1);
   
    sFetch +=  " Hour-kWh : ";
    sFetch +=  String((smldecoder.getInputkWh() /* valSML_kwh_in */ - smartgrid.hourprice_kwh_start),3);

    sFetch += "\r\n\r\nHour-flex:";
    sFetch += String(smartgrid.getFlexprice_hour(ntpclient.getTimeInfo()->tm_hour),2);
  
    sFetch +=   "  Day-flex :";
    sFetch += String(smartgrid.getFlexprice_monthday(ntpclient.getTimeInfo()->tm_mday),2);
    
    sFetch +=    " Month-flex:";
    sFetch += String(smartgrid.getFlexprice_month(ntpclient.getTimeInfo()->tm_mon),2);

    //sFetch +=     "\r\nSum-flex     : ";
    //sFetch += String(smartgrid.Flexprice_sum + smartgrid.getAktFlexprice(),2);

    sFetch +=    "\r\nHour-fix: ";
    sFetch += String(smartgrid.getFixprice_hour(ntpclient.getTimeInfo()->tm_hour),2);

    sFetch +=     "  Day-fix  :";
    sFetch += String(smartgrid.getFixprice_monthday(ntpclient.getTimeInfo()->tm_mday),2);

    sFetch +=     "  Month-fix:";
    sFetch += String(smartgrid.getFixprice_month(ntpclient.getTimeInfo()->tm_mon) ,2);

    //sFetch +=     "\r\nSum-fix     : ";
    //sFetch += String(smartgrid.fixprice_sum + smartgrid.akt_fixprice,2);
    sFetch += "";

    return sFetch;
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
      //sFetch += String(smartgrid.flexprice_hour[i],2);
      sFetch += String(smartgrid.getFlexprice_hour(i),2);
      if (i < 23)
      {
       sFetch += ", ";
      }
     }
     debug_println(sFetch);
     return sFetch;
  }
  else
  if (var == "PRICE_HOUR_FIX")
  {
     for (size_t i = 0; i <24; i++)
     {
      //sFetch += String(smartgrid.fixprice_hour[i],2);
      sFetch += String(smartgrid.getFixprice_hour(i),2);
      if (i < 23)
      {
       sFetch += ", ";
      }
     }
     return sFetch;
  }
  else
  if (var == "PRICE_MONTHDAY_FLEX")
  {
     for (size_t i = 1; i < 32; i++)
     {
      //sFetch += String(smartgrid.flexprice_monthday[i],2);
      sFetch += String(smartgrid.getFlexprice_monthday(i),2);
      if (i < 31)
      {
       sFetch += ", ";
      }
     }
     return sFetch;
  }
  else
  if (var == "PRICE_MONTHDAY_FIX")
  {
     for (size_t i = 1; i < 32; i++) // begin with "1" !!
     {
      //sFetch += String(smartgrid.fixprice_monthday[i],2);
      sFetch += String(smartgrid.getFixprice_monthday(i),2);
      if (i < 31)
      {
       sFetch += ", ";
      }
     }
     return sFetch;
  }
  else
  if (var == "PRICE_MONTH_FLEX")
  {
     for (size_t i = 0; i < 12; i++)
     {
      //sFetch += String(smartgrid.flexprice_month[i],2);
      sFetch += String(smartgrid.getFlexprice_month(i),2);
      if (i < 11)
      {
       sFetch += ", ";
      }
     }
     return sFetch;
  }
  else
  if (var == "PRICE_MONTH_FIX")
  {
     for (size_t i = 0; i < 12; i++)
     {
      //sFetch += String(smartgrid.fixprice_month[i],2);
      sFetch += String(smartgrid.getFixprice_month(i),2);
      if (i < 11)
      {
       sFetch += ", ";
      }
     }
     return sFetch;
  }
  else
  if (var == "COST_AKT_MONTH") // read config.txt
  {
    if (!LittleFS.exists("/cost_akt_month.txt")) 
    {
     return String(F("Error: File 'cost_akt_month.txt' not found!"));
    }
    // read "config.txt" 
    fs::File cfile = LittleFS.open("/cost_akt_month.txt", "r");   
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
    if (!LittleFS.exists("/cost_akt_year.txt")) 
    {
     return String(F("Error: File 'cost_akt_year.txt' not found!"));
    }
    // read "config.txt" 
    fs::File cfile = LittleFS.open("/cost_akt_year.txt", "r");   
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
  
  return "";
}



void notFound(AsyncWebServerRequest *request) 
{
    request->send(404, "text/plain", "Not found");
}

void initWebServer()
{ 
  // --------------- Basis Seiten:----------------------------------
  //Route for root / web page
  webserver.on("/",          HTTP_GET, [](AsyncWebServerRequest *request)
  {https://
   request->send(LittleFS, "/index.html", String(), false, setHtmlVar);
  });
  //Route for root /index web page
  webserver.on("/index.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/index.html", String(), false, setHtmlVar);
  });
 
  //Route for setup web page
  webserver.on("/setup.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/setup.html", String(), false, setHtmlVar);
  });
  //Route for config web page
  webserver.on("/config.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/config.html", String(), false, setHtmlVar);
  });
  //Route for Info-page
  webserver.on("/info.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/info.html", String(), false, setHtmlVar);
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

  //Route for Luxtronik values
  webserver.on("/luxtronik.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   luxws.setpolling(true); // POLLING ON !
   request->send(LittleFS, "/luxtronik.html", String(), false, setHtmlVar);
  });

  //Route for setup web page
  webserver.on("/meter.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/meter.html", String(), false, setHtmlVar);
  });
  

  //.. some code for the navigation icons
  webserver.on("/home.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/home.png", String(), false);
  });

  webserver.on("/file-list.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/file-list.png", String(), false);
  });

  webserver.on("/settings.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/settings.png", String(), false);
  });
#ifdef SG_READY
  webserver.on("/smartgrid.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/smartgrid.html", String(), false, setHtmlVar);
  });
  webserver.on("/sgready.html",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/sgready.html", String(), false, setHtmlVar);
  });
  webserver.on("/reload.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/reload.png", String(), false);
  });
#endif
  
  webserver.on("/fetch", HTTP_GET, [](AsyncWebServerRequest *request)
  {
                                                                      // Index:
    sFetch = ntpclient.getTimeString();                               // 0 = Time: 00:00
    sFetch += ",";
#ifdef SG_READY
    sFetch += smartgrid.getHourVar1(ntpclient.getTimeInfo()->tm_hour);// 1 = sg_status
#else
    sFetch += "-";
#endif

     sFetch += ',';

#ifdef DS100
    sFetch += "--"                                                     // 2 todo: DS100 Power L1
#else 
    sFetch += luxws.getval(LUX_VAL_TYPE::POWER_IN, true);              // 2 Power In
#endif

    sFetch += luxws.getCSVfetch(true);                                 // 3...
    sFetch += "end";     

    //debug_printf("server.on fetch: %s",sFetch.c_str());
    request->send(200, "text/plain", sFetch);  
  });


  // Route for style-sheet
  webserver.on("/style.css",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/style.css", String(), false);
  });

  // App-Icons
  webserver.on("/current.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/current.png", String(), false);
  });
  webserver.on("/sg.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/sg.png", String(), false);
  });
  webserver.on("/sgready.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/sgready.png", String(), false);
  });
  webserver.on("/heatpump.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/heatpump.png", String(), false);
  });
   webserver.on("/luxtronik.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/luxtronik.png", String(), false);
  });
  webserver.on("/homeauto.png",          HTTP_GET, [](AsyncWebServerRequest *request)
  {
   request->send(LittleFS, "/homeauto.png", String(), false);
  });

  // fetch GET
  webserver.on("/fetchmeter", HTTP_GET, [](AsyncWebServerRequest *request)
  {
  
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
    sFetch+= ',';
    sFetch+= smldecoder.getWatt();  // 7
    sFetch+= ',';
    sFetch+= smldecoder.getInputkWh(); // 8
    sFetch+= ',';
    sFetch+= smldecoder.getOutputkWh(); // 9
    
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
    AsyncWebLog.printf("sgready POST: arg: %s  value:%d\r\n",sArg.c_str(), iVal);
 
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

    request->send(LittleFS, "/sgready.html", String(), false, setHtmlVar); 
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
     request->send(LittleFS, "/config.html", String(), false, setHtmlVar);
    });
       
  // init Webserver 
  sFetch.reserve(150);
  AsyncWebLog.begin(&webserver);
  AsyncWebOTA.begin(&webserver);
  webserver.onNotFound(notFound);
  webserver.begin();
  }
#endif

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
  initLittleFS();
  initFileVarStore(); 
  setcolor('b'); // blue
  initWiFi();
  initWebServer(); 
  setLED(0);
  delay(500);
  smartgrid.init();
#ifdef SML_TIBBER
  smldecoder.init(varStore.varSML_s_url.c_str(), varStore.varSML_s_user.c_str(), varStore.varSML_s_password.c_str());
#endif
 #ifdef DS100_MODBUS
  DS100Init();
  #endif
  luxws.init(varStore.varLUX_s_url.c_str(), varStore.varLUX_s_password.c_str());
  delay(1000);
}

///////////////////////////////////// TimePageSprite.drawString(20,10,buf, &Font0);///////////
/// @brief loop
////////////////////////////////////////////////
void loop() 
{
   luxws.loop();
   if (millis() - TimerSlowDuration > TimerSlow) 
   {
    TimerSlow = millis();                      // Reset time for next event
    testWiFiReconnect();
    ntpclient.update();
    tm* looptm = ntpclient.getTimeInfo();
    debug_printf("\r\nTIME: %s:%02d\r\n", ntpclient.getTimeString(), ntpclient.getTimeInfo()->tm_sec);
    AsyncWebLog.printf("TIME: %s:%02d\r\n", ntpclient.getTimeString(), ntpclient.getTimeInfo()->tm_sec);
#ifdef SML_TIBBER
    smldecoder.read(); 
#endif  
#ifdef DS100_MODBUS
    DS100read();
#endif
#ifdef SG_READY
    time_t tt = ntpclient.getUnixTime();
    smartgrid.loop(&tt);
#endif
    if (looptm->tm_hour == 0 && looptm->tm_min ==0 && looptm->tm_sec < 7)
    { 
      luxws.poll(true); // new Day
    }
    else
    {
      luxws.poll(false);
    }
  
    if (luxws.isConnected())
    {
      AsyncWebLog.printf("WS-POLL-State:        \t %s\r\n", luxws.getval(LUX_VAL_TYPE::STATUS_POLL,     false));
      AsyncWebLog.printf("WP-State:             \t %s\r\n", luxws.getval(LUX_VAL_TYPE::STATUS_HEATPUMP, false));
      AsyncWebLog.printf("Watt                  \t %d \r\n",smldecoder.getWatt());
      AsyncWebLog.printf("Leistung OUT-HEAT:    \t %s\r\n", luxws.getval(LUX_VAL_TYPE::ENERGY_OUT_HE, false));
  
    }
    else
    {
       AsyncWebLog.printf("...wait Luxtronik connecting\r\n");
    }
  }

  
  // fast blink
  static int c4 = 0;
  if (millis() - TimerFastDuration > TimerFast)
  {
#ifdef ESP32_S3_ZERO
  // useless color change ;-)
   c4++;
   if (c4 > 4) {c4 = 0;}
   switch (c4)
  {
    case 0: 
      neopixel_color = 'r';
      break;
    case 1:
      neopixel_color = 'g';
      break;
    case 2:
      neopixel_color = 'b';
      break;
    case 3:
      neopixel_color = 'y';
      break;
    case '4':
      neopixel_color = 'w';
      break;
    default:
       break;
    }
#endif
    TimerFast = millis();
    blinkLED();
  }

}

