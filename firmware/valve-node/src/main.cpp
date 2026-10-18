//RF receiver logic for a valve node, using RCSwitch and output pins to operate a valve actuator.
// 0:D3
// 1:TX,
// 2:D4(on board LED) ,
// 3:RX RF dosen't work on this pin
// 4:D2
// 5:D1
// 12:D6
// 13:D7
// 14:D5
// 15:D8 
// 16:D0
//static const uint8_t D0   = 16;
//static const uint8_t D1   = 5;
//static const uint8_t D2   = 4;
//static const uint8_t D3   = 0;
//static const uint8_t D4   = 2;
//static const uint8_t D5   = 14;
//static const uint8_t D6   = 12;
//static const uint8_t D7   = 13;
//static const uint8_t D8   = 15;
//static const uint8_t D9   = 3;
//static const uint8_t D10  = 1;

#include <RCSwitch.h>
#include <ESP8266WiFi.h>

#define VAL_NO  1

////////D1 mini////////
#define   VAL1  0//D3
#define   VAL2  5//D1
#define   PCON  14//D5
#define   RFin  12//D6   // Receiver on interrupt pin D6

/////// UNO /////////
//#define  VAL1 9//D1
//#define  VAL2 10//D3
//#define PCON  13//D4 next to the GND pin
//#define  RFin 0 //Receiver on interrupt 0 => that is pin #2  UNO 

RCSwitch mySwitch = RCSwitch();
void setup() {
  Serial.begin(115200);
  WiFi.mode( WIFI_OFF );
  WiFi.forceSleepBegin();  
  Serial.println("init... ");
  pinMode(VAL1, OUTPUT); 
  pinMode(VAL2, OUTPUT); 
  pinMode(PCON, OUTPUT); 
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);// on cheap led off
  ////////////////////// VALVE ON ////////////////
  digitalWrite(PCON, HIGH);     
  delay(100); 
  Serial.print("...");
  digitalWrite(VAL2, HIGH);
  digitalWrite(VAL1, LOW);
  delay(25);
  Serial.print("...");
  digitalWrite(VAL1, LOW);
  digitalWrite(VAL2, LOW);
  digitalWrite(PCON, LOW);  
  delay(500); 
  //////////////////////VALVE OFF //////////////////////
  digitalWrite(PCON, HIGH);     
  delay(100);  
  Serial.print("....");
  digitalWrite(VAL2, LOW);
  digitalWrite(VAL1, HIGH);
  delay(25);
  Serial.print("...");
  digitalWrite(VAL1,LOW);
  digitalWrite(VAL2, LOW);
  digitalWrite(PCON, LOW);   
  delay(500);  
  Serial.println("OK");
   
  mySwitch.enableReceive(RFin);               
}

void loop() {
if(mySwitch.getReceivedValue() == 1280+VAL_NO){
    digitalWrite(PCON, HIGH);     
    //delay(100);  
    digitalWrite(VAL2, LOW);
    digitalWrite(VAL1, HIGH);
    delay(25);
    digitalWrite(VAL1,LOW);
    digitalWrite(VAL2, LOW);
    digitalWrite(PCON, LOW);   delay(500);  
    //Serial.println( mySwitch.getReceivedValue()  );
    }
  //  delay(1000);
else if(mySwitch.getReceivedValue() == 2560+VAL_NO){
    digitalWrite(PCON, HIGH);     
    //delay(100);  
    digitalWrite(VAL2, HIGH);
    digitalWrite(VAL1, LOW);
    delay(25);
    digitalWrite(VAL1, LOW);
    digitalWrite(VAL2, LOW);
    digitalWrite(PCON, LOW);  delay(500);   
    //Serial.println( mySwitch.getReceivedValue()  );
    }
}
