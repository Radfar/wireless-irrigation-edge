
// set to UNO board refurbished
#include <SoftwareSerial.h> 
//#include <RTClib.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <RCSwitch.h>
#include <SoftReset.h>

#define VALVE                   4 //Number of valves in use: default 128
#define WATERING_DURATION       2000 //msec
#define OFF                     LOW
#define ON                      HIGH

#define RX          10
#define TX          11
#define Pump        13 // Main Valve relay after Pump
#define RF_T        12
//LCD pin to Arduino
const int pin_RS = 8;
const int pin_EN = 9;
const int pin_d4 = 4;
const int pin_d5 = 5;
const int pin_d6 = 6;
const int pin_d7 = 7;

LiquidCrystal lcd( pin_RS, pin_EN, pin_d4, pin_d5, pin_d6, pin_d7);
RCSwitch mySwitch = RCSwitch();
SoftwareSerial sim800l(RX, TX); // RX,TX for Arduino and for the module it's TXD RXD, they should be inverted

//volatile int flow_frequency; // Measures flow sensor pulses
byte Times[VALVE]; //user defined plan for a 24 hours schadual and repeat every day 
byte Watering_Plan[VALVE]; // 
byte Rows=0;          // row counter
byte Start_Time=0;    // 258 eeprom address
byte Stop_Time=0;     // 259 eeprom
byte Valve_Code=0;    // 257 eeprom
unsigned int Watering_Time=0;// Start_time - Stop_Time *60
unsigned int Total_Hour=0;// should be less than 1440 minute for all 128 valves
unsigned int Total_Min=0;// should be less than 1440 minute for all 128 valves
unsigned int Sum=0;
byte Hourr=0;
boolean Set=0;
boolean d24=1;
unsigned int Transmit_Code;
float l_minute;// litre per minute
byte cntt=0,ii=0;
String textSms;//,numberSms;
unsigned int x,y;
////////////////////////////////////////////////////////////////////////////// Wire protection action ////////////////
void WireProtectionAction(void)
{
  //DateTime now = rtc.now();
//  mySerial.println("AT+CSMP=17,167,2,25\r");  // set this parameter if empty SMS received
//  updateSerial();
//  delay(100);
//  mySerial.println("AT+CMGF=1"); // text mode sms
//  updateSerial();
//  mySerial.println("AT+CMGS=\"+phone number\"");
//  updateSerial();
//  mySerial.println("!!! DANGER !!! "); // sms text
//  updateSerial();
//  mySerial.println("Wire disconnected "); // sms text
//  updateSerial();
//  mySerial.print(now.hour()); // sms text
//  updateSerial();
//  mySerial.print(":"); // sms text
//  updateSerial();
//  mySerial.print(now.minute()); // sms text
//  updateSerial();
//  mySerial.write(26);
//  Serial.println("WIRE SMS SENT!"); 
}
////////////////////////////////////////////////////////////////////////////// Total Time Update ///////////////////////
void TotalTimeUpdate(void)
{ 
  lcd.setCursor(5,1);
  lcd.print (Times[Valve_Code]);
  if(Times[Valve_Code]<10){  lcd.print (" ");}
  lcd.print ("M ");
  Total_Hour=0;
  for(cntt=0;cntt<VALVE;cntt++)
    {Total_Hour+=EEPROM.read(cntt);}
  Total_Min=Total_Hour%60;
  Total_Hour/=60;
  lcd.print (Total_Hour);
  lcd.print ("H");
  lcd.print (Total_Min);
  lcd.print ("M  ");
  lcd.setCursor(5,1);
}
////////////////////////////////////////////////////////////////////////////// Time Plan Update ///////////////////////
void TimePlanUpdate(void)
{
  lcd.setCursor(0,1);
  lcd.print(Start_Time);
  lcd.print(" to ");
  lcd.print(Stop_Time);
  lcd.print("  ");
  if(Stop_Time > Start_Time) { Watering_Time=Stop_Time-Start_Time; }
  else  {Watering_Time=(24-Start_Time)+Stop_Time;}
  lcd.print(" ");
  lcd.print(Watering_Time);    
  lcd.print("H ");   
}

/////////////////////////////////////////////////////////////////////////////
void WaitForS(void)
{
  //mySerial.println("AT+CMGD=1,4"); //delet sms in location 1
  lcd.setCursor(0,1);
  lcd.print("Wait.");    
  for(ii=0;ii<11;ii++){lcd.print(".");delay(100);}
}
/////////////////////////////////////////////////////////////////////////////
void SendSMS1()
{
  Serial.println("Sending SMS...");               //Show this message on serial monitor
  //sim800l.print("AT+CSMP=17,167,2,25\r");delay(100);
  sim800l.print("AT+CMGF=1\r");                   //Set the module to SMS mode
  delay(100);
  sim800l.print("AT+CMGS=\"+phonenumberhere\"\r");  //Your phone number don't forget to include your country code, example +xxxxx"
  delay(500);
  sim800l.print("SYSTEM READY TO USE!");       //This is the text to send to the phone number, don't make it too long or you have to modify the SoftwareSerial buffer
  delay(500);
  sim800l.print((char)26);// (required according to the datasheet)
  delay(500);
  sim800l.print((char)26);// (required according to the datasheet)
  delay(500);
  sim800l.println();
  Serial.println("Text Sent.");
  delay(500);
}

void Serialcom()
{
    delay(500);
    while (Serial.available())
    {
        sim800l.write(Serial.read());                 //Transférer les données reçues par le port série au port série du logiciel
    }
    while(sim800l.available())
    {
       Serial.write(sim800l.read());                  //Transférer au port série ce que le logiciel a reçu en série
    }
}
////////////////////////////////////////////////////////////////////////////// SETUP  ////////////////////////////////////////////////////
void setup () {
  lcd.begin(16, 2);                   // set the lcd type: 16-character by 2-lines
  lcd.clear();                        // LCD screen clear
  sim800l.begin(9600);   //Module baude rate, this is on max, it depends on the version
  Serial.begin(9600);   
  lcd.print("TARAVAB  COMPANY");      // displaying the message
  lcd.setCursor(0,1);                 // 1st position at 2nd line
  lcd.print("Initializing");    
  Serial.print("Initializing"); 
  Serial.println("...");
  mySwitch.enableTransmit(RF_T);//RF Transmitter is connected to Arduino Pin #12 MEGA 2560, uno
  Serial.println("RF set"); 
  for(ii=0;ii<4;ii++){lcd.print(".");delay(500);}
  pinMode(Pump, OUTPUT);
  digitalWrite(Pump, OFF);
  y=1023;
  #ifndef ESP8266
    while (!Serial); // wait for serial port to connect. Needed for native USB
  #endif
  Serial.print("serial ok"); 
  Serial.println("..."); 
  Serial.println("OK"); 
  lcd.setCursor(0,1);
  lcd.print("BOOTING OK      ");
//  SendSMS();     
//  Serialcom();
//  for(ii=0;ii<4;ii++){lcd.print(".");
  delay(800);
//////////////////////////////////////////////////////////////////////////// PAUSE ////////////////////////////////////////////// 
  lcd.clear();
  lcd.setCursor(0,0);              
  lcd.print("Enter Parameter:");
  lcd.setCursor(0,1);
  lcd.print("V");
  Valve_Code=EEPROM.read(257);//EEPROM.write(addr, val); restore last used Valve number from eeprom
  if(Valve_Code == VALVE-1 || Valve_Code>VALVE-1){Valve_Code =0;}
  lcd.setCursor(1,1);
  lcd.print (Valve_Code);
  if(Valve_Code<10){  lcd.print ("  :");}
  if(Valve_Code>=10 && Valve_Code<100){  lcd.print (" :");}
  Times[Valve_Code]=EEPROM.read(Valve_Code); 
  TotalTimeUpdate();
  lcd.blink();
}
/////////////////////////////////////////////////////////////////////////////  main loop     /////////////////////////////////////////////////////////////////////////////////////
void loop () 
{
x = analogRead (0);
//y = analogRead (1); //anti theaf
// Serial.println(y);
Serialcom();
/////////////////////////////////////////////////////////////////////// SET BUTTON  (1st time set button pressed)  //////////////////////////////////////////////
if(y < 600)    // wire protection
  {
  WireProtectionAction();
  while(y <600);
  }
if (x < 60) //|| textSms.indexOf("START")!=-1) // SET/RUN BUTTON
  {
  while(x<60 ){x = analogRead (0);} // waiting for releasing the button
  WaitForS();
//////////////////////////////////////////////////////////// SYSTEM READY SMS SENDING ///////////////////////////////
  Serial.println("Button pressed");   //Shows this message on the serial monitor
  delay(200);                         //Small delay to avoid detecting the button press many times
  SendSMS1();     
  Serialcom();
  lcd.clear();
  lcd.setCursor(0,0);              
  lcd.print("Start Stop Time:");
  Stop_Time=EEPROM.read(259);//EEPROM.write(addr, val); restore stop hour of watering during 24hour
  if(Stop_Time == 25 || Stop_Time>25){Stop_Time=1;}
  Start_Time=EEPROM.read(258);//EEPROM.write(addr, val); restore start hour of watering during 24hour
  if(Start_Time == 25 || Start_Time>25){Start_Time=1;}
  TimePlanUpdate();
  Set=0; // watering plan start flag
  ///////////////////////////////////////////////////////// Enter watering start and finish time  /////////////////////////////////// 
  while(!Set)// watering get started!
    {
    x = analogRead (0);
    //y = analogRead (1);
    Serialcom();
    if(y < 600)
      {
      WireProtectionAction();
      while(y <600);
      }
    ///////////////////////////////////////////////////// SET BUTTON  (2nd time set button pressed)  ///////////////////////////////////
    if(x<60)// || textSms.indexOf("TURNON")!=-1) // SET/RUN BUTTON
       {
       while(x<60 ){x = analogRead (0);} // waiting for releasing the button
       WaitForS();
//////////////////////////////////////////////////////////////// START SMS SENDING ///////////////////////////////
       lcd.clear();
       lcd.setCursor(0,0);              
       lcd.print("Watering started");// start time: ?? finish :??
       lcd.setCursor(0,1);
       Set=1; 
       lcd.noBlink();
       break;
       }
      ////////////////////////////// INCREASE STOP TIME  ///////////////////////////////////
      else if (x < 200) //UP
        {while(x<200){x = analogRead (0);}
        Stop_Time=EEPROM.read(259);//EEPROM.write(addr, val); restore stop hour of watering during 24hour
        Stop_Time++;
        if(Stop_Time == 25 || Stop_Time>25){Stop_Time=1;}
        Start_Time=EEPROM.read(258);//EEPROM.write(addr, val); restore start hour of watering during 24hour
        TimePlanUpdate();
        EEPROM.write(259,Stop_Time);//EEPROM.write(addr, val);
        }
      
      /////////////////////////////// DECREASE STOP TIME///////////////////////////  
      else if (x < 400)//DOWN
        {while(x<400){x = analogRead (0);}
        Stop_Time=EEPROM.read(259);//EEPROM.write(addr, val); restore stop hour of watering during 24hour
        Stop_Time--;
        if(Stop_Time == 0 || Stop_Time>25){Stop_Time=24;}
        Start_Time=EEPROM.read(258);//EEPROM.write(addr, val); restore start hour of watering during 24hour
        TimePlanUpdate();
        EEPROM.write(259,Stop_Time);//EEPROM.write(addr, val);
        }
      
      ////////////////////////////// //INCREASE START TIME ////////////////////////////
      else if (x < 600)//LEFT
        {while(x<600){x = analogRead (0);}
        Start_Time=EEPROM.read(258);//EEPROM.write(addr, val); restore start hour of watering during 24hour
        Start_Time++;
        if(Start_Time == 25 || Start_Time>25){Start_Time=1;}
        Stop_Time=EEPROM.read(259);//EEPROM.write(addr, val); restore last used Valve number from eeprom
        TimePlanUpdate();
        EEPROM.write(258,Start_Time);//EEPROM.write(addr, val);
        }
      
      ///////////////////////////// DECREASE START TIME ///////////////////////
      else if (x < 800)//SELECT
        {while(x<800){x = analogRead (0);}
        Start_Time=EEPROM.read(258);//EEPROM.write(addr, val); restore start hour of watering during 24hour
        Start_Time--;
        if(Start_Time == 0 || Start_Time>25){Start_Time=24;}
        TimePlanUpdate();
        EEPROM.write(258,Start_Time);//EEPROM.write(addr, val);
        }
      }// while(!Set)
      ///////////////////////////////////////////////////// WATERING PLAN LOOP  ////////////////////////////////////////////////////////////////////////////////
      while(1)
        {
        Start_Time=EEPROM.read(258);//EEPROM.write(addr, val); restore start hour of watering during 24hour
        Stop_Time=EEPROM.read(259);//EEPROM.write(addr, val); restore start hour of watering during 24hour
        if(Stop_Time > Start_Time)
          {Watering_Time=(24-Start_Time)+Stop_Time;}
        for(Valve_Code=0; Valve_Code<VALVE; Valve_Code++)
          {Watering_Plan[Valve_Code]=EEPROM.read(Valve_Code);
  //          Serial.print(Valve_Code);
  //          Serial.print(':');
  //          Serial.println(Watering_Plan[Valve_Code]);   
          }
        lcd.setCursor(0,1);
        d24=1;// watering flag set 
        digitalWrite(Pump, ON);
        delay(2000);// waiting for pipe lines to be full of water before start watering process
 ///////////////////////////////////////////// watering start ///////////////////////////////////////////
        while(d24 != 0)
          {
          if (textSms.indexOf("TURNOFF")!=-1)
            {
//            mySerial.println("AT+CMGD=1,4"); //delet sms in location 1
//            updateSerial();
//            delay(150);
//            SendSMS1(); // PAUSE SMS //
//            Serial.println("PAUSE SMS SENT!");
//            delay(1000);
    
              soft_restart();
              d24=0;
              break;
            }
          //y = analogRead (1);
          if(y < 600)
            {
            WireProtectionAction();
            while(y <600);
            }
        //Hourr=now.hour();// current hour
        for(Valve_Code=0;Valve_Code<VALVE;Valve_Code++)
          {
          lcd.clear();
          lcd.setCursor(0,0);
          //lcd.print("Flow:");
          lcd.print(l_minute);
          lcd.print("L/M ");
          lcd.print("T:");
          //lcd.print(rtc.getTemperature());
          lcd.print("C");
          lcd.setCursor(9,1);
          lcd.print(Hourr);
//          lcd.print(now.hour(), DEC);
//          lcd.print(':');
//          lcd.print(now.minute(), DEC);
//          lcd.print(':');
//          lcd.print(now.second(), DEC);
        
        Sum=0;
        if(Watering_Plan[Valve_Code] != 0)
          {
          Transmit_Code=2560+Valve_Code;
          lcd.setCursor(0,1);
          lcd.print("V");
          lcd.print(Valve_Code);
          lcd.print(":ON");
          lcd.print(" ");
          lcd.print(Watering_Plan[Valve_Code]);
                    
          Serial.print("V");
          Serial.print(Valve_Code);
          Serial.print(":ON->");
          Serial.print(Transmit_Code);
          mySwitch.send(Transmit_Code, 24);  
          delay(200);
          mySwitch.send(Transmit_Code, 24);  
          delay(200);
          //mySwitch.send(Transmit_Code, 24);  
          //delay(200);
          Watering_Plan[Valve_Code]--;  
          delay(WATERING_DURATION);// one minute watering
          
          Transmit_Code=1280+Valve_Code;
          lcd.setCursor(0,1);
          lcd.print("V");
          lcd.print(Valve_Code);
          lcd.print(":OFF");
          lcd.print(" ");
          lcd.print(Watering_Plan[Valve_Code]);
          Serial.print(" , V");
          Serial.print(Valve_Code);
          Serial.print(":OFF->");
          Serial.print(Transmit_Code);
          mySwitch.send(Transmit_Code, 24);  
          delay(200);
          mySwitch.send(Transmit_Code, 24);  
          delay(200);
          //mySwitch.send(Transmit_Code, 24);  
          //delay(200);  
          }
        for(cntt=0;cntt<VALVE;cntt++)
          {Sum+=Watering_Plan[cntt];}
        if(Sum == 0)
          {
          d24=0;
          digitalWrite(Pump, OFF);
          delay(2000);// waiting for pipe lines to be full of water before start watering process
          break;
          }
          Serial.print(" , Sum:");
          Serial.println(Sum);
          delay(500);
          }//for
        }//while(d24 != 0)
      }//while(1)
    }//if x<60
                      //////////////////////////// INCREASE Time in Minutes +  ///////////////////////////////////
else if (x < 200) //UP
  {while(x<200){x = analogRead (0);}
  Valve_Code=EEPROM.read(257);//EEPROM.write(addr, val); restore last used Valve number from eeprom
  Rows=EEPROM.read(Valve_Code);
  Rows++;
  if(Rows == 21 || Rows>21){Rows=0;}
  Times[Valve_Code]=Rows; 
  EEPROM.write(Valve_Code,Times[Valve_Code]);//EEPROM.write(addr, val); Save updated time to EEPROM
  TotalTimeUpdate();
  }

                       /////////////////////////////// DECREASE Time in Minutes -///////////////////////////  
else if (x < 400)//DOWN
  {while(x<400){x = analogRead (0);}
  Valve_Code=EEPROM.read(257);//EEPROM.write(addr, val); restore last used Valve number from eeprom
  Rows=EEPROM.read(Valve_Code);
  Rows--;
  if(Rows>21){Rows=20;}
  Times[Valve_Code]=Rows; 
  EEPROM.write(Valve_Code,Times[Valve_Code]);//EEPROM.write(addr, val);
  TotalTimeUpdate();
  }

                     /////////////////////////////////////////// //VALVE INC V+ ////////////////////////////
else if (x < 600)//LEFT
  {while(x<600){x = analogRead (0);}
  Valve_Code=EEPROM.read(257);//EEPROM.write(addr, val); restore last used Valve number from eeprom
  Valve_Code++;
  if(Valve_Code == VALVE || Valve_Code>VALVE){Valve_Code =0;}
  EEPROM.write(257,Valve_Code);//EEPROM.write(addr, val);
  lcd.setCursor(1,1);
  lcd.print (Valve_Code);
  if(Valve_Code<10){  lcd.print ("  :");}
  if(Valve_Code>=10 && Valve_Code<100){  lcd.print (" :");}
  Times[Valve_Code]=EEPROM.read(Valve_Code); 
  TotalTimeUpdate();
  }

                  ///////////////////////////////////////////////////// VALVE DEC V- ///////////////////////
else if (x < 800)//SELECT
  {while(x<800){x = analogRead (0);}
  Valve_Code=EEPROM.read(257);//EEPROM.write(addr, val); restore last used Valve number from eeprom
  Valve_Code--;
  if(Valve_Code>VALVE-1){Valve_Code =VALVE-1;}
  EEPROM.write(257,Valve_Code);//EEPROM.write(addr, val);
  lcd.setCursor(1,1);
  lcd.print (Valve_Code);
  if(Valve_Code<10){  lcd.print ("  :");}
  if(Valve_Code>=10 && Valve_Code<100){  lcd.print (" :");}
  Times[Valve_Code]=EEPROM.read(Valve_Code); 
  TotalTimeUpdate();
  }
}// void loop
