#include <Arduino.h>

#ifdef DS100_MODBUS
// DS-100 Modbus Energy-Meter
#define MODBUS_RXD_GPIO 2
#define MODBUS_TXT_GPIO 4   
HardwareSerial Serial_Modbus(1);

// Modbus Commands
// Volt (value in mV)
const char*  DS100_TX_GET_L1_V   = "\x01\x04\x04\x00\x00\x02\x70\xFB";
// Watt
const char*  DS100_TX_GET_L1_W   = "\x01\x04\x04\x1A\x00\x02\x51\x3C";  
const char*  DS100_TX_GET_L2_W   = "\x01\x04\x04\x1C\x00\x02\xB1\x3D"; 
const char*  DS100_TX_GET_L3_W   = "\x01\x04\x04\x1E\x00\x02\x10\xFD";  
// kW/h total activ Energy
const char*  DS100_TX_GET_L1_KWH = "\x01\x04\x05\x00\x00\x02\x71\x07";
const char*  DS100_TX_GET_L2_KWH = "\x01\x04\x05\x64\x00\x02\x30\xD8"; 
const char*  DS100_TX_GET_L3_KWH = "\x01\x04\x05\xC8\x00\x02\xF0\xF9";                               

uint32_t valDS100_mV   = 0 ;
uint32_t valDS100_L1_W = 0;
uint32_t valDS100_L2_W = 0;
uint32_t valDS100_L3_W = 0;
uint32_t valDS100_L1_KWH = 0; 
uint32_t valDS100_L2_KWH = 0;
uint32_t valDS100_L3_KWH = 0;

// read the values direct with an RS485-converter from die "DS100" meter with modbus interface
// REMARK: write your own routine or addapt this, if you need this values !!!
// NEEDS: extra Hardware RS485-converter !!!
/// @brief Init Electrical Meter communication over RS485
inline void DS100Init()
{
    Serial_Modbus.begin(9600, SERIAL_8N1, MODBUS_RXD_GPIO, MODBUS_TXT_GPIO);
    delay(1);
    while(Serial_Modbus.available())
    {Serial_Modbus.read();}
    debug_println("Modbus init OK!");
}
    
//char HexStr[03];
//String sHex = "";

/// @brief read values from Modbus
uint32_t DS100GetValue(const char* tx)
{
 uint8_t rxValue[10];
 for (int i =0; i < 8; i++)
 { 
  //sprintf(HexStr,"%02X ",tx[i]);
  //sHex+=HexStr;
  Serial_Modbus.write(tx[i]);
 }
 Serial_Modbus.flush(true);
 //AsyncWebLog.println("TX:" + sHex);
 delay(150);
 uint8_t rxix=0;
 rxix = 0;
 while (Serial_Modbus.available()) 
 {
   if (rxix < 10)
   {
    rxValue[rxix]= (uint8_t) Serial_Modbus.read();
    //debug_printf("%02X",rxValue[rxix]);
    rxix++;
   }
 } 

 uint32_t val = (((uint64_t)(rxValue[3])<<32)) +  (((uint32_t)rxValue[4]<<16)) + ((uint16_t)(rxValue[5]<<8)) + rxValue[6];
  //uint32_t val = 0;
  //Rx: 01 04 04 00 03 8C E4 6F 0F
  //             -----------
  //             value in mV 
  
  //AsyncWebLog.printf("DS100Value:%s\r\n" , String(val));
 return val;     
}

/// @brief read DS100 values
inline void DS100read()
{
    valDS100_mV     = DS100GetValue(DS100_TX_GET_L1_V);
    valDS100_L1_W   = DS100GetValue(DS100_TX_GET_L1_W);
    valDS100_L1_KWH = DS100GetValue(DS100_TX_GET_L1_KWH); 
    valDS100_L2_W   = DS100GetValue(DS100_TX_GET_L2_W);
    valDS100_L2_KWH = DS100GetValue(DS100_TX_GET_L2_KWH); 
}
#endif
