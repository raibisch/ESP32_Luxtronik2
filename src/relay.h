
#include <Arduino.h>

#ifdef ESP32_RELAY_X2
#pragma message("Info : ESP32_RELAY_X2")
#define LED_GPIO 23
#define RELAY_1  16
#define RELAY_2  17
#endif

#ifdef ESP32_RELAY_X4
#pragma message("Info : ESP32_RELAY_X4")
#define LED_GPIO 23
#define RELAY_1  26
#define RELAY_2  25
#define RELAY_3  33
#define RELAY_4  32     
#endif

void inline initRelay()
{
#ifdef ESP32_RELAY_X2
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  digitalWrite(RELAY_1, LOW);
  digitalWrite(RELAY_2, LOW);
#endif

#ifdef ESP32_RELAY_X4
  // ESP_RELAY_X4
  // we need only RELAY_1 and RELAY_2 for SGready
  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);

  pinMode(RELAY_3, OUTPUT);
  pinMode(RELAY_4, OUTPUT);

  digitalWrite(RELAY_1, LOW);
  digitalWrite(RELAY_2, LOW);

  digitalWrite(RELAY_3, LOW);
  digitalWrite(RELAY_4, LOW);
#else
 /* for other boards without relais output */
#endif
}

/// @brief set Relay
/// @param no 1..2
/// @param on_off true..false
void setRelay(uint8_t no, bool on_off)
{
#if (defined ESP32_RELAY_X2) || (defined ESP32_RELAY_X4)
  switch (no)
  {
   case 1:
    digitalWrite(RELAY_1,on_off);
    break;
   case 2:
    digitalWrite(RELAY_2,on_off);
    break; 
   default:
     break;
  }
#endif
}