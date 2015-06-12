#define BUILD 7
/*  
  Copyright (C) 2009, 2010 Matt Reba, Jeremiah Dillingham

    This file is part of BrewTroller.

    BrewTroller is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    BrewTroller is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with BrewTroller.  If not, see <http://www.gnu.org/licenses/>.


BrewTroller - Open Source Brewing Computer
Software Lead: Matt Reba (matt_AT_brewtroller_DOT_com)
Hardware Lead: Jeremiah Dillingham (jeremiah_AT_brewtroller_DOT_com)

Documentation, Forums and more information available at http://www.brewtroller.com
*/

/*
Compiled on Arduino-1.0.5 (http://arduino.cc/en/Main/Software) modified for ATMEGA1284P
  Using the following libraries:
    PID  v0.6 (Beta 6) (http://www.arduino.cc/playground/Code/PIDLibrary)
    OneWire 2.0 (http://www.pjrc.com/teensy/arduino_libraries/OneWire.zip)
    Encoder by CodeRage ()
    FastPin and modified LiquidCrystal with FastPin by CodeRage (http://www.brewtroller.com/forum/showthread.php?t=626)
    Menu
*/



//*****************************************************************************************************************************
// BEGIN CODE
//*****************************************************************************************************************************

#include <avr/pgmspace.h>
#include <PID_Beta6.h>
#include <pin.h>
#include <menu.h>
#include <ModbusMaster.h>
#include <Wire.h>

#include "HWProfile.h"
#include "Config.h"
#include "Enum.h"
#include "Outputs.h"
#include "Trigger.h"
#include "UI_LCD.h"
#include "UI_Lang.h"
#include <avr/eeprom.h>
#include <EEPROM.h>
#include "wiring_private.h"
#include <encoder.h>
#ifdef RGBIO8_ENABLE
  #include "RGBIO8.h"
#endif
#include "Vol_Bubbler.h"

void(* softReset) (void) = 0;

const char BT[] PROGMEM = "BrewTroller";
const char BTVER[] PROGMEM = "2.7";

//**********************************************************************************
// Compile Time Logic
//**********************************************************************************

//Enable Mash Avergaing Logic if any Mash_AVG_AUXx options were enabled
#if defined MASH_AVG_AUX1 || defined MASH_AVG_AUX2 || defined MASH_AVG_AUX3
  #define MASH_AVG
#endif

#ifdef USEMETRIC
  #define SETPOINT_MULT 50
  #define SETPOINT_DIV 2
#else
  #define SETPOINT_MULT 100
  #define SETPOINT_DIV 1
#endif

#ifndef STRIKE_TEMP_OFFSET
  #define STRIKE_TEMP_OFFSET 0
#endif

#if COM_SERIAL0 == BTNIC || defined BTNIC_EMBEDDED
  #define BTNIC_PROTOCOL
#endif


#ifdef USEMETRIC
  #define EvapRateConversion 1000
#else
  #define EvapRateConversion 100
#endif


#if TS_ONEWIRE_RES == 12
  #define PID_UPDATE_INTERVAL 750
#elif TS_ONEWIRE_RES == 11
  #define PID_UPDATE_INTERVAL 375
#elif TS_ONEWIRE_RES == 10
  #define PID_UPDATE_INTERVAL 188
#elif TS_ONEWIRE_RES == 9
  #define PID_UPDATE_INTERVAL 94
#else
  // should not be this value, fail the compile
  #ERROR
#endif

struct ProgramThread {
  byte activeStep;
  byte recipe;
};

struct TriggerConfiguration {
  byte type                    :3;
  byte index                   :4;
  boolean activeLow            :1;
  unsigned long threshold      :24;
  unsigned long profileFilter;
  unsigned long disableMask;
  byte releaseHysteresis;
};

struct BrewStepConfiguration {
  boolean fillSpargeBeforePreheat       :1;
  boolean autoStartFill                 :1;
  boolean autoExitFill                  :1;
  boolean autoExitPreheat               :1;
  boolean autoStrikeTransfer            :1;
  byte autoExitGrainInMinutes           :8;
  boolean autoExitMash                  :1;
  boolean autoStartFlySparge            :1;
  boolean autoExitSparge                :1;
  byte autoBoilWhirlpoolMinutes         :8;
  byte boilAdditionSeconds              :8;
  byte preBoilAlarm                     :8;
  unsigned int mashTunHeatCapacity      :16;
};

//**********************************************************************************
// Globals
//**********************************************************************************
//Vessel PWM Output Pin Array
analogOutput_SWPWM* pwmOutput[3] = {NULL, NULL, NULL};

#ifdef ESTOP_PIN
  pin *estopPin = NULL;
#endif

#ifdef HEARTBEAT
  pin hbPin;
#endif

//Volume Sensor Pin Array
byte vSensor[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};

Trigger *trigger[USERTRIGGER_COUNT];

//8-byte Temperature Sensor Address x9 Sensors
byte tSensor[9][8];
int temp[9];

//Volume in (thousandths of gal/l)
unsigned long tgtVol[3], volAvg[3], calibVols[3][10];
unsigned int calibVals[3][10];
#ifdef SPARGE_IN_PUMP_CONTROL
unsigned long prevSpargeVol[2] = {0, 0};
#endif

#ifdef HLT_MIN_REFILL
unsigned long SpargeVol = 0;
#endif

#ifdef FLOWRATE_CALCS
//Flowrate in thousandths of gal/l per minute
long flowRate[3] = {0, 0, 0};
#endif

Bubbler *bubbler = NULL;

//Create the appropriate 'LCD' object for the hardware configuration (4-Bit GPIO, I2C)
#if defined UI_LCD_4BIT
  #include <LiquidCrystalFP.h>
  
  #ifndef UI_DISPLAY_SETUP
    LCD4Bit LCD(LCD_RS_PIN, LCD_ENABLE_PIN, LCD_DATA4_PIN, LCD_DATA5_PIN, LCD_DATA6_PIN, LCD_DATA7_PIN);
  #else
    LCD4Bit LCD(LCD_RS_PIN, LCD_ENABLE_PIN, LCD_DATA4_PIN, LCD_DATA5_PIN, LCD_DATA6_PIN, LCD_DATA7_PIN, LCD_BRIGHT_PIN, LCD_CONTRAST_PIN);
  #endif
  
#elif defined UI_LCD_I2C
  LCDI2C LCD(UI_LCD_I2CADDR);
#endif
 
boolean autoValve[NUM_AV];
OutputSystem* outputs = NULL;

#ifdef RGBIO8_ENABLE
  RGBIO8* rgbio[RGBIO8_MAX_BOARDS];
#endif

//Shared buffers
char buf[20];

//Output Globals
double PIDInput[3], PIDOutput[3], setpoint[3];
byte hysteresis[3];
boolean heatStatus[3];
byte boilPwr;

PID pid[3] = {
  PID(&PIDInput[VS_HLT], &PIDOutput[VS_HLT], &setpoint[VS_HLT], 3, 4, 1),
  PID(&PIDInput[VS_MASH], &PIDOutput[VS_MASH], &setpoint[VS_MASH], 3, 4, 1),
  PID(&PIDInput[VS_KETTLE], &PIDOutput[VS_KETTLE], &setpoint[VS_KETTLE], 3, 4, 1),
};

//Timer Globals
unsigned long timerValue[2], lastTime[2];
boolean timerStatus[2], alarmStatus;

//Brew Step Logic Globals
boolean preheated[4];
ControlState boilControlState = CONTROLSTATE_OFF;

struct ProgramThread programThread[PROGRAMTHREAD_MAX];

struct BrewStepConfiguration brewStepConfiguration;

//Bit 1 = Boil; Bit 2-11 (See Below); Bit 12 = End of Boil; Bit 13-15 (Open); Bit 16 = Preboil (If Compile Option Enabled)
unsigned int hoptimes[11] = { 105, 90, 75, 60, 45, 30, 20, 15, 10, 5, 0 };
byte pitchTemp;

//Log Strings
const char LOGCMD[] PROGMEM = "CMD";
const char LOGDEBUG[] PROGMEM = "DEBUG";
const char LOGSYS[] PROGMEM = "SYS";
const char LOGCFG[] PROGMEM = "CFG";
const char LOGDATA[] PROGMEM = "DATA";

//**********************************************************************************
// Setup
//**********************************************************************************

void setup() {
  #ifdef ADC_REF
	analogReference(ADC_REF);
  #endif
  
  #ifdef USE_I2C
    Wire.begin(BT_I2C_ADDR);
  #endif
  
  #ifdef HEARTBEAT
    hbPin.setup(HEARTBEAT_PIN, OUTPUT);
  #endif

  for (byte i = 0; i < PROGRAMTHREAD_MAX; i++) {
    programThread[i].activeStep = INDEX_NONE;
    programThread[i].recipe = INDEX_NONE;
  }
  
  for (byte i = 0; i < USERTRIGGER_COUNT; i++)
    trigger[i] = NULL;
  
  initializeBrewStepConfiguration();

  //We need some object for UI in case setup is not loaded due to missing config
  //This will get thrown away after setup is loaded
  outputs = new OutputSystem();
  outputs->init();

  tempInit();  
  comInit();
  
  //Check for cfgVersion variable and update EEPROM if necessary (EEPROM.ino)
  if (!checkConfig())
    loadSetup();
  
  //User Interface Initialization (UI.ino)
  //Moving this to last of setup() to allow time for I2CLCD to initialize
  #ifndef NOUI
    uiInit();
  #endif
}


//**********************************************************************************
// Loop
//**********************************************************************************

void loop() {
  //User Interface Processing (UI.ino)
  #ifndef NOUI
    uiUpdate();
  #endif
  
  //Core BrewTroller process code (BrewCore.ino)
  brewCore();
}

