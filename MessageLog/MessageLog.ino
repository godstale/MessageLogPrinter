/*
    RetroWatch Arduino is a part of open source smart watch project.
    Copyright (C) 2014  Suh Young Bae

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see [http://www.gnu.org/licenses/].
*/
/*
Retro Watch Arduino v1.0

  Get the latest version, android host app at 
  ------> https://github.com/godstale/retrowatch
  ------> or http://www.hardcopyworld.com

Written by Suh Young Bae (godstale@hotmail.com)
All text above must be included in any redistribution
*/

//#include <avr/pgmspace.h>
#include <SoftwareSerial.h>
//#include <math.h>
#include "bitmap.h"
#include "Adafruit_Thermal.h"

//----- Serial for BT (HC-06)
#define BTSerial Serial1

//----- Serial for thermal printer
#define TX_PIN 3 // Arduino transmit  YELLOW WIRE  labeled RX on printer
#define RX_PIN 2 // Arduino receive   GREEN WIRE   labeled TX on printer
SoftwareSerial mySerial(RX_PIN, TX_PIN); // Declare SoftwareSerial obj first
Adafruit_Thermal printer(&mySerial);     // Pass addr to printer constructor

//----- Bluetooth transaction parsing
#define TR_MODE_IDLE 1
#define TR_MODE_WAIT_CMD 11
#define TR_MODE_WAIT_MESSAGE 101
#define TR_MODE_WAIT_TIME 111
#define TR_MODE_WAIT_ID 121
#define TR_MODE_WAIT_COMPLETE 201

#define TRANSACTION_START_BYTE 0xfc
#define TRANSACTION_END_BYTE 0xfd

#define CMD_TYPE_NONE 0x00
#define CMD_TYPE_RESET_EMERGENCY_OBJ 0x05
#define CMD_TYPE_RESET_NORMAL_OBJ 0x02
#define CMD_TYPE_RESET_USER_MESSAGE 0x03

#define CMD_TYPE_ADD_EMERGENCY_OBJ 0x11
#define CMD_TYPE_ADD_NORMAL_OBJ 0x12
#define CMD_TYPE_ADD_USER_MESSAGE 0x13

#define CMD_TYPE_DELETE_EMERGENCY_OBJ 0x21
#define CMD_TYPE_DELETE_NORMAL_OBJ 0x22
#define CMD_TYPE_DELETE_USER_MESSAGE 0x23

#define CMD_TYPE_SET_TIME 0x31
#define CMD_TYPE_REQUEST_MOVEMENT_HISTORY 0x32
#define CMD_TYPE_SET_CLOCK_STYLE 0x33
#define CMD_TYPE_SET_INDICATOR 0x34

#define CMD_TYPE_PING 0x51
#define CMD_TYPE_AWAKE 0x52
#define CMD_TYPE_SLEEP 0x53
#define CMD_TYPE_REBOOT 0x54

byte TRANSACTION_POINTER = TR_MODE_IDLE;
byte TR_COMMAND = CMD_TYPE_NONE;

//----- Message item buffer
#define MSG_COUNT_MAX 7
#define MSG_BUFFER_MAX 19
unsigned char msgBuffer[MSG_COUNT_MAX][MSG_BUFFER_MAX];
char msgParsingLine = 0;
char msgParsingChar = 0;
char msgCurDisp = 0;

//----- Emergency item buffer
#define EMG_COUNT_MAX 3
#define EMG_BUFFER_MAX 19
char emgBuffer[EMG_COUNT_MAX][EMG_BUFFER_MAX];
char emgParsingLine = 0;
char emgParsingChar = 0;
char emgCurDisp = 0;

//----- Time
#define UPDATE_TIME_INTERVAL 60000
byte iMonth = 1;
byte iDay = 1;
byte iWeek = 1;    // 1: SUN, MON, TUE, WED, THU, FRI,SAT
byte iAmPm = 0;    // 0:AM, 1:PM
byte iHour = 0;
byte iMinutes = 0;
byte iSecond = 0;
unsigned long prevClockTime = 0;

#define TIME_BUFFER_MAX 6
char timeParsingIndex = 0;
char timeBuffer[6] = {-1, -1, -1, -1, -1, -1};
PROGMEM const char* weekString[] = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
PROGMEM const char* ampmString[] = {"AM", "PM"};

unsigned long nextEmgPrintTime = 0xfffffffe;
unsigned long nextMsgPrintTime = 0xfffffffe;

//----- Etc.
#define ICON_WIDTH 16
#define ICON_HEIGHT 16


void setup()   {
  Serial.begin(9600);    // for debug
  
  init_emg_array();
  init_msg_array();
  
  //----- Init serial
  BTSerial.begin(9600);  // set the data rate for the BT port
  mySerial.begin(19200);  // Initialize SoftwareSerial
  printer.begin();        // Init printer (same regardless of serial type)
  init_printer();        // Set basic parameters
}


void loop() {
  boolean isReceived = false;
  unsigned long current_time = 0;
  
  // Receive data from remote and parse
  isReceived = receiveBluetoothData();
  
  // Update clock time
  current_time = millis();
  updateTime(current_time);
  
  // Display routine
  onDraw(current_time);
  
  // If data doesn't arrive, wait for a while to save battery
  if(!isReceived)
    delay(200);
}




///////////////////////////////////
//----- Utils
///////////////////////////////////
void init_msg_array() {
  for(int i=0; i<MSG_COUNT_MAX; i++) {
    for(int j=0; j<MSG_BUFFER_MAX; j++) {
      msgBuffer[i][j] = 0x00;
    }
  }
  msgParsingLine = 0;
  msgParsingChar = 0;    // First 2 byte is management byte
  msgCurDisp = 0;
}

void init_emg_array() {
  for(int i=0; i<EMG_COUNT_MAX; i++) {
    for(int j=0; j<EMG_BUFFER_MAX; j++) {
      emgBuffer[i][j] = 0x00;
    }
  }
  emgParsingLine = 0;
  emgParsingChar = 0;    // First 2 byte is management byte
  emgCurDisp = 0;
}

void init_printer() {
  printer.setDefault(); // Restore printer to defaults
  printer.setSize('S');  // Set text size as small
  printer.justify('L');  // left align
  printer.setLineHeight(50);  // normal line spacing
}

char _int2str[7];
char* int2str( register int i ) {
  register unsigned char L = 1;
  register char c;
  register boolean m = false;
  register char b;  // lower-byte of i
  // negative
  if ( i < 0 ) {
    _int2str[ 0 ] = '-';
    i = -i;
  }
  else L = 0;
  // ten-thousands
  if( i > 9999 ) {
    c = i < 20000 ? 1
      : i < 30000 ? 2
      : 3;
    _int2str[ L++ ] = c + 48;
    i -= c * 10000;
    m = true;
  }
  // thousands
  if( i > 999 ) {
    c = i < 5000
      ? ( i < 3000
          ? ( i < 2000 ? 1 : 2 )
          :   i < 4000 ? 3 : 4
        )
      : i < 8000
        ? ( i < 6000
            ? 5
            : i < 7000 ? 6 : 7
          )
        : i < 9000 ? 8 : 9;
    _int2str[ L++ ] = c + 48;
    i -= c * 1000;
    m = true;
  }
  else if( m ) _int2str[ L++ ] = '0';
  // hundreds
  if( i > 99 ) {
    c = i < 500
      ? ( i < 300
          ? ( i < 200 ? 1 : 2 )
          :   i < 400 ? 3 : 4
        )
      : i < 800
        ? ( i < 600
            ? 5
            : i < 700 ? 6 : 7
          )
        : i < 900 ? 8 : 9;
    _int2str[ L++ ] = c + 48;
    i -= c * 100;
    m = true;
  }
  else if( m ) _int2str[ L++ ] = '0';
  // decades (check on lower byte to optimize code)
  b = char( i );
  if( b > 9 ) {
    c = b < 50
      ? ( b < 30
          ? ( b < 20 ? 1 : 2 )
          :   b < 40 ? 3 : 4
        )
      : b < 80
        ? ( i < 60
            ? 5
            : i < 70 ? 6 : 7
          )
        : i < 90 ? 8 : 9;
    _int2str[ L++ ] = c + 48;
    b -= c * 10;
    m = true;
  }
  else if( m ) _int2str[ L++ ] = '0';
  // last digit
  _int2str[ L++ ] = b + 48;
  // null terminator
  _int2str[ L ] = 0;  
  return _int2str;
}

///////////////////////////////////
//----- Time functions
///////////////////////////////////
void setTimeValue() {
  iMonth = timeBuffer[0];
  iDay = timeBuffer[1];
  iWeek = timeBuffer[2];    // 1: SUN, MON, TUE, WED, THU, FRI,SAT
  iAmPm = timeBuffer[3];    // 0:AM, 1:PM
  iHour = timeBuffer[4];
  iMinutes = timeBuffer[5];
}

void updateTime(unsigned long current_time) {
  if(iMinutes >= 0) {
    if(current_time - prevClockTime > UPDATE_TIME_INTERVAL) {
      // Increase time
      iMinutes++;
      if(iMinutes >= 60) {
        iMinutes = 0;
        iHour++;
        if(iHour > 12) {
          iHour = 1;
          (iAmPm == 0) ? iAmPm=1 : iAmPm=0;
          if(iAmPm == 0) {
            iWeek++;
            if(iWeek > 7)
              iWeek = 1;
            iDay++;
            if(iDay > 30)  // Yes. day is not exact.
              iDay = 1;
          }
        }
      }
      prevClockTime = current_time;
    }
  }
}

///////////////////////////////////
//----- BT, Data parsing functions
///////////////////////////////////

// Parsing packet according to current mode
boolean receiveBluetoothData() {
  int isTransactionEnded = false;
  while(!isTransactionEnded) {
    if(BTSerial.available()) {
      byte c = BTSerial.read();
      
      if(c == 0xFF && TRANSACTION_POINTER != TR_MODE_WAIT_MESSAGE) return false;
      
      if(TRANSACTION_POINTER == TR_MODE_IDLE) {
        parseStartSignal(c);
      }
      else if(TRANSACTION_POINTER == TR_MODE_WAIT_CMD) {
        parseCommand(c);
      }
      else if(TRANSACTION_POINTER == TR_MODE_WAIT_MESSAGE) {
        parseMessage(c);
      }
      else if(TRANSACTION_POINTER == TR_MODE_WAIT_TIME) {
        parseTime(c);
      }
      else if(TRANSACTION_POINTER == TR_MODE_WAIT_ID) {
        parseId(c);
      }
      else if(TRANSACTION_POINTER == TR_MODE_WAIT_COMPLETE) {
        isTransactionEnded = parseEndSignal(c);
      }
      
    }  // End of if(BTSerial.available())
    else {
      isTransactionEnded = true;
    }
  }  // End of while()
  return true;
}  // End of receiveBluetoothData()

void parseStartSignal(byte c) {
  //drawLogChar(c);
  if(c == TRANSACTION_START_BYTE) {
    TRANSACTION_POINTER = TR_MODE_WAIT_CMD;
    TR_COMMAND = CMD_TYPE_NONE;
  }
}

void parseCommand(byte c) {
  if(c == CMD_TYPE_RESET_EMERGENCY_OBJ || c == CMD_TYPE_RESET_NORMAL_OBJ || c == CMD_TYPE_RESET_USER_MESSAGE) {
    TRANSACTION_POINTER = TR_MODE_WAIT_COMPLETE;
    TR_COMMAND = c;
    processTransaction();
  }
  else if(c == CMD_TYPE_ADD_EMERGENCY_OBJ || c == CMD_TYPE_ADD_NORMAL_OBJ || c == CMD_TYPE_ADD_USER_MESSAGE) {
    TRANSACTION_POINTER = TR_MODE_WAIT_MESSAGE;
    TR_COMMAND = c;
    if(c == CMD_TYPE_ADD_EMERGENCY_OBJ) {
      emgParsingChar = 0;
      if(emgParsingLine >= MSG_COUNT_MAX || emgParsingLine < 0)
        emgParsingLine = 0;
    }
    else if(c == CMD_TYPE_ADD_NORMAL_OBJ) {
      msgParsingChar = 0;
      if(msgParsingLine >= MSG_COUNT_MAX || msgParsingLine < 0)
        msgParsingLine = 0;
    }
  }
  else if(c == CMD_TYPE_DELETE_EMERGENCY_OBJ || c == CMD_TYPE_DELETE_NORMAL_OBJ || c == CMD_TYPE_DELETE_USER_MESSAGE) {
    TRANSACTION_POINTER = TR_MODE_WAIT_COMPLETE;
    TR_COMMAND = c;
  }
  else if(c == CMD_TYPE_SET_TIME) {
    TRANSACTION_POINTER = TR_MODE_WAIT_TIME;
    TR_COMMAND = c;
  }
  else if(c == CMD_TYPE_SET_CLOCK_STYLE || c == CMD_TYPE_SET_INDICATOR) {
    TRANSACTION_POINTER = TR_MODE_WAIT_ID;
    TR_COMMAND = c;
  }
  else {
    TRANSACTION_POINTER = TR_MODE_IDLE;
    TR_COMMAND = CMD_TYPE_NONE;
  }
}

void parseMessage(byte c) {
  if(c == TRANSACTION_END_BYTE) {
    processTransaction();
    TRANSACTION_POINTER = TR_MODE_IDLE;
  }
  
  if(TR_COMMAND == CMD_TYPE_ADD_EMERGENCY_OBJ) {
    if(emgParsingChar < EMG_BUFFER_MAX - 1) {
      if(emgParsingChar > 1) {
        emgBuffer[emgParsingLine][emgParsingChar] = c;
      }
      emgParsingChar++;
    }
    else {
      TRANSACTION_POINTER = TR_MODE_IDLE;
      processTransaction();
    }
  }
  else if(TR_COMMAND == CMD_TYPE_ADD_NORMAL_OBJ) {
    if(msgParsingChar < MSG_BUFFER_MAX - 1) {
      if(msgParsingChar > 1) {
        msgBuffer[msgParsingLine][msgParsingChar] = c;
      }
      msgParsingChar++;
    }
    else {
      TRANSACTION_POINTER = TR_MODE_IDLE;
      processTransaction();
    }
  }
  else if(TR_COMMAND == CMD_TYPE_ADD_USER_MESSAGE) {
    // Not available yet.
    TRANSACTION_POINTER = TR_MODE_WAIT_COMPLETE;
  }
}

void parseTime(byte c) {
  if(TR_COMMAND == CMD_TYPE_SET_TIME) {
    if(timeParsingIndex >= 0 && timeParsingIndex < TIME_BUFFER_MAX) {
      timeBuffer[timeParsingIndex] = (int)c;
      timeParsingIndex++;
    }
    else {
      processTransaction();
      TRANSACTION_POINTER = TR_MODE_WAIT_COMPLETE;
    }
  }
}

void parseId(byte c) {
  if(TR_COMMAND == CMD_TYPE_SET_CLOCK_STYLE) {
    processTransaction();
  }
  else if(TR_COMMAND == CMD_TYPE_SET_INDICATOR) {
    processTransaction();
  }
  TRANSACTION_POINTER = TR_MODE_WAIT_COMPLETE;
}

boolean parseEndSignal(byte c) {
  if(c == TRANSACTION_END_BYTE) {
    TRANSACTION_POINTER = TR_MODE_IDLE;
    return true;
  }
  return false;
}

void processTransaction() {
  if(TR_COMMAND == CMD_TYPE_RESET_EMERGENCY_OBJ) {
    init_emg_array();//init_msg_array();
  }
  else if(TR_COMMAND == CMD_TYPE_RESET_NORMAL_OBJ) {
    init_msg_array();//init_emg_array();
  }
  else if(TR_COMMAND == CMD_TYPE_RESET_USER_MESSAGE) {
    // Not available yet.
  }
  else if(TR_COMMAND == CMD_TYPE_ADD_NORMAL_OBJ) {
    Serial.println("Add a message!!");
    msgBuffer[msgParsingLine][0] = 0x01;
    msgBuffer[msgParsingLine][MSG_BUFFER_MAX - 1] = 0x00;
    msgParsingChar = 0;
    msgParsingLine++;
    if(msgParsingLine >= MSG_COUNT_MAX)
      msgParsingLine = 0;
    setNextMsgPrint(millis() + 1000);  // reserve printing
  }
  else if(TR_COMMAND == CMD_TYPE_ADD_EMERGENCY_OBJ) {
    Serial.println("Add an emergency message!!");
    emgBuffer[emgParsingLine][0] = 0x01;
    emgBuffer[emgParsingLine][EMG_BUFFER_MAX - 1] = 0x00;
    emgParsingChar = 0;
    emgParsingLine++;
    if(emgParsingLine >= EMG_COUNT_MAX)
      emgParsingLine = 0;
    setNextEmgPrint(millis() + 2000);  // reserve printing
  }
  else if(TR_COMMAND == CMD_TYPE_ADD_USER_MESSAGE) {
  }
  else if(TR_COMMAND == CMD_TYPE_DELETE_EMERGENCY_OBJ || TR_COMMAND == CMD_TYPE_DELETE_NORMAL_OBJ || TR_COMMAND == CMD_TYPE_DELETE_USER_MESSAGE) {
    // Not available yet.
  }
  else if(TR_COMMAND == CMD_TYPE_SET_TIME) {
    setTimeValue();
    timeParsingIndex = 0;
  }
  if(TR_COMMAND == CMD_TYPE_SET_CLOCK_STYLE || CMD_TYPE_SET_INDICATOR) {
  }
}

///////////////////////////////////
//----- Drawing methods
///////////////////////////////////

// Main drawing routine.
void onDraw(unsigned long currentTime) {
  boolean bPrintEmg = false;
  boolean bPrintMsg = false;
  if(isEmgPrintTime(currentTime))
    bPrintEmg = true;
  if(isMsgPrintTime(currentTime))
    bPrintMsg = true;
    
  if(bPrintEmg || bPrintMsg) {
    // Print banner image
    printer.printBitmap(384, 40, IMG_banner);
  }

  if(bPrintEmg) {
    emgCurDisp = 0;
    printEmg();
    nextEmgPrintTime = 0xfffffffe;
  }
  if(bPrintMsg) {
    msgCurDisp = 0;
    printMsg();
    nextMsgPrintTime = 0xfffffffe;
  }
}  // End of onDraw()

void printEmg() {
  // print date
  printer.justify('R');  // right align
  printer.setLineHeight(); // Reset to default
  //printer.print(F("("));
  //printer.inverseOn();
  printer.setSize('M');
  printer.print((const char*)pgm_read_word(&(weekString[iWeek])));
  printer.print(F(" "));
  printer.print(int2str(iHour));
  printer.print(F(":"));
  printer.print(int2str(iMinutes));
  printer.print(F(" "));
  printer.println((const char*)pgm_read_word(&(ampmString[iAmPm])));
  printer.setSize('S');
  //printer.inverseOff();
  //printer.print(F(" "));
  //printer.print(F(") "));
  printer.justify('L');  // left align

  int pCount = 0;
  while(findNextEmerMessage()) {
    int icon_num = 60;
    if(emgBuffer[emgCurDisp][2] > -1 && emgBuffer[emgCurDisp][2] < ICON_ARRAY_SIZE) {
      icon_num = (int)(emgBuffer[emgCurDisp][2]);
    }
    // print message
    if(emgBuffer[emgCurDisp][3] < 0x21 || emgBuffer[emgCurDisp][3] >= 0xf0)
      continue;
    printer.print((char *)(emgBuffer[emgCurDisp] + 3));
    //printer.write(0xF9);printer.write(0xFA);
    printer.println();
    //printer.print(F(", "));
    // print image
    //printer.printBitmap(ICON_WIDTH, ICON_HEIGHT, (const unsigned char*)pgm_read_word(&(bitmap_array[icon_num])));
    pCount++;
    emgCurDisp++;
    if(emgCurDisp >= EMG_COUNT_MAX) {
      emgCurDisp = 0;
      break;
    }
  }  // End of while()
  
  if(pCount > 0) {
    //printer.print(F("Updated "));
    printer.println();
    printer.print(int2str(pCount));
    printer.print(F(" emergency message(s)."));
    // release paper
    printer.feed(3);
  }
}

void printMsg() {
  // print date
  printer.justify('R');  // right align
  printer.setLineHeight(); // Reset to default
  //printer.print(F("("));
  //printer.inverseOn();
  printer.setSize('M');
  printer.print((const char*)pgm_read_word(&(weekString[iWeek])));
  printer.print(F(" "));
  printer.print(int2str(iHour));
  printer.print(F(":"));
  printer.print(int2str(iMinutes));
  printer.print(F(" "));
  printer.println((const char*)pgm_read_word(&(ampmString[iAmPm])));
  printer.setSize('S');
  //printer.inverseOff();
  //printer.print(F(" "));
  //printer.print(F(") "));
  printer.justify('L');  // left align

  int pCount = 0;
  while(findNextNormalMessage()) {
    int icon_num = 0;
    if(msgBuffer[msgCurDisp][2] > -1 && msgBuffer[msgCurDisp][2] < ICON_ARRAY_SIZE) {
      icon_num = (int)(msgBuffer[msgCurDisp][2]);
    }
    // print message
    if(msgBuffer[msgCurDisp][3] < 0x21 || msgBuffer[msgCurDisp][3] >= 0xf0)
      continue;
    printer.print((char *)(msgBuffer[msgCurDisp] + 3));
    //printer.write(0xF9);printer.write(0xFA);
    printer.println();
    //printer.print(F(", "));
    // print image
    //printer.printBitmap(ICON_WIDTH, ICON_HEIGHT, (const unsigned char*)pgm_read_word(&(bitmap_array[icon_num])));
    pCount++;
    msgCurDisp++;
    if(msgCurDisp >= MSG_COUNT_MAX) {
      msgCurDisp = 0;
      break;
    }
  }  // End of while()
  
  if(pCount > 0) {
    //printer.print(F("Updated "));
    printer.println();
    printer.print(int2str(pCount));
    printer.print(F(" message(s)."));
    // release paper
    printer.feed(3);
  }
}

// To avoid printing on every drawing time
// wait for for the time interval
boolean isEmgPrintTime(unsigned long currentTime) {
  if(currentTime > nextEmgPrintTime) {
    return true;
  }
  return false;
}

boolean isMsgPrintTime(unsigned long currentTime) {
  if(currentTime > nextMsgPrintTime) {
    return true;
  }
  return false;
}

// Set next print time (emergency message)
void setNextMsgPrint(unsigned long nextUpdateTime) {
  nextMsgPrintTime = nextUpdateTime;
}

// Set next print time (normal message)
void setNextEmgPrint(unsigned long nextUpdateTime) {
  nextEmgPrintTime = nextUpdateTime;
}

// Check if available emergency message exists or not
boolean findNextEmerMessage() {
  if(emgCurDisp < 0 || emgCurDisp >= EMG_COUNT_MAX) emgCurDisp = 0;
  while(true) {
    if(emgBuffer[emgCurDisp][0] == 0x00) {  // 0x00 means disabled
      emgCurDisp++;
      if(emgCurDisp >= EMG_COUNT_MAX) {
        emgCurDisp = 0;
        return false;
      }
    }
    else {
      break;
    }
  }  // End of while()
  return true;
}

// Check if available normal message exists or not
boolean findNextNormalMessage() {
  if(msgCurDisp < 0 || msgCurDisp >= MSG_COUNT_MAX) msgCurDisp = 0;
  while(true) {
    if(msgBuffer[msgCurDisp][0] == 0x00) {
      msgCurDisp++;
      if(msgCurDisp >= MSG_COUNT_MAX) {
        msgCurDisp = 0;
        return false;
      }
    }
    else {
      break;
    }
  }  // End of while()
  return true;
}

// Count all available emergency messages
int countEmergency() {
  int count = 0;
  for(int i=0; i<EMG_COUNT_MAX; i++) {
    if(emgBuffer[i][0] != 0x00)
      count++;
  }
  return count;
}

// Count all available normal messages
int countMessage() {
  int count = 0;
  for(int i=0; i<MSG_COUNT_MAX; i++) {
    if(msgBuffer[i][0] != 0x00)
      count++;
  }
  return count;
}

