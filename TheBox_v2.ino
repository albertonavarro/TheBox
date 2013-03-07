
//#include <VarSpeedServo.h>
#include <Servo.h>
#include <LiquidCrystal.h>
#include <TinyGPS.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>


TinyGPS gps;
// rx = 7 (yellow), tx = 6 (white)
// vcc = black, gnd = green
SoftwareSerial nss(6,7);

/*
 * LCD RS pin to A1
 * LCD Enable pin to A0
 * LCD Vo to digitail pin 3
 * LCD D4 pin to digital pin 12
 * LCD D5 pin to digital pin 11
 * LCD D6 pin to digital pin 10
 * LCD D7 pin to digital pin 9
 * LCD R/W pin to ground
*/
LiquidCrystal lcd(A1, A0, 12, 11, 10, 9);
//VarSpeedServo myservo;
Servo myservo;
long previousMillis;
int interval = 1000;

int SERVOPIN = 8;
long int servostart;

// reed switch is attached to this pin
int BACKDOORPIN = 4;
// polulu power switch is on this pin
int POLULUPIN = 2;

// scale stuff for backdoor timer
// used both for backdoor and reset-functionality
float v = 1; // init
float scalea = 0.99;
float scaleb = 0.01;
// timer for backdoor and reset 
typedef long int longjohn;  // Riiiis er fjollet 
longjohn unsigned backdoortimeout1 = 2000; // efter den her tid toggler servoen til åben og tilbage til lukket
longjohn unsigned backdoortimeout2 = 25000; // efter den her tid nulstilles gamestatus - skal nok være 25 sekunder eller noget
                                       // så det er lettest at gøre det 'inde fra'
longjohn unsigned backdoortimerstart;

int backdoortimerrunning = 0;
int boxopen = 0;
int gamereset = 0;

longjohn unsigned mastertimerstart;
// mastertimer = timeout
long unsigned int timeout = 70000; // how long will we wait for the GPS (should be 1½ minute or so)?
int timeoutreached = 0;
int powermessage = 0;

int debug = 0;
int debug2 = 0;
/*
***** storyline-array
*/
char* storyline[] = {"Follow the instruc- tions from this box",
  " ",
};


/* GPS-related variables */
float flat, flon;
unsigned long age, date, time, chars = 0;
int year;
byte month, day, hour, minute, second, hundredths;
char datechar[10];
unsigned short sentences = 0, failed = 0;
static const float LONDON_LAT = 51.508131, LONDON_LON = -0.128002;
static const float NNIT_LAT = 55.73558, NNIT_LON = 12.47504;
static const float LABI_LAT = 55.676235, LABI_LON = 12.54561;
static const float HOME_LAT = 55.91461, HOME_LON = 12.49487;

// only one can be 'attached' at the time
bool servoattached = 0;
bool gpsattached = 1;

/* declare gamestate and tasknr */
int gamestate;
int tasknr;

/* setup */
void setup() 
{ 
 nss.begin(9600);
 if (debug)
   Serial.begin(115200);
/*
 // the contrast is run by software - OUTDATED
 delay(100); 
 pinMode(3,OUTPUT);
 analogWrite(3,50);
digitalWrite(3,HIGH); 
*/
 // set up the LCD's number of columns and rows: 
 lcd.begin(20, 4);
 // Print a message to the LCD.
 // to be moved when the box goes live
 // or maybe print gamestatus + welcome (back)
 lcd.setCursor(0,0);
 stringToLCD("Welcome!");
 delay(1000);
 stringToLCD("Initializing...");

 /*
  read Gamestate, etc. from EEPROM
  gamestate = pos 0
  tasknr = pos 1
 */
 gamestate = EEPROM.read(0);
 // if gamestate is bigger than a certain number, it has not been initiated, so we set it to '1' ('0' would cause an endless loop)
 if (gamestate > 200 || gamestate == 0) { gamestate = 1; EEPROM.write(0,gamestate); }

 tasknr = EEPROM.read(1);
 // if tasknr is bigger than a certain number, it has not been initiated, so we write it back
 if (tasknr > 200) { tasknr = 0; EEPROM.write(1,tasknr); }

 if (debug) {
   Serial.print("Initial gamestate: ");
   Serial.println(gamestate);
 }

 /* other settings */
 // we're using the internal PULLUP-resistor
 // when the pin is pulled LOW (to GND) things happen..
 pinMode(BACKDOORPIN,INPUT_PULLUP);
 digitalWrite(BACKDOORPIN,HIGH);
 
 // debug-LED. Should probably be disabled when we go "live"
 pinMode(13,OUTPUT);
 digitalWrite(13,LOW);

 /* polulu-pin - pull this one high, and the battery power is cut */
 pinMode(POLULUPIN,OUTPUT);
 digitalWrite(POLULUPIN,LOW);

 // mastertimer
 mastertimerstart = millis();
} 
 
void loop() 
{ 
  // feed the GPS-object - we're going to do this a few times down the line
  // re-attach the GPS-module if it's been detached
  if (!servoattached && !gpsattached) { 
      nss.begin(9600);
      gpsattached = 1;
  }
  if (gpsattached) {
          // feed a few times, to get a good fix.
          feedgps(); 
          gps.f_get_position(&flat, &flon, &age);
  }


  // detach the servo if timeout after movement is reached
   if (myservo.attached() && millis()-servostart>4000) { 
    myservo.detach();
    servoattached = 0;
    nss.begin(9600); // the servo has been detached, now we can re-enable GPS
  } 

  // listen for backdoor and do various stuff 
  int sensorVal = digitalRead(BACKDOORPIN);
  v = scalea*v + scaleb*sensorVal;
  //Serial.println(v);
  // start timer if backdoor-pin is activated
  // disable timer if v isn't under threshold
  if (v <= 0.1 && backdoortimerrunning == 0) {
    backdoortimerstart = millis();
    backdoortimerrunning = 1;
  } else if (v >= 0.1) { backdoortimerrunning = 0; boxopen = 0; }     
  // test for threshold 1 - open and close door
  if (backdoortimerrunning && (millis()-backdoortimerstart >= backdoortimeout1) && (millis()-backdoortimerstart <= backdoortimeout2) && !boxopen) {
    boxopen = 1;
    if (debug)
      Serial.println("box is opened");
    nss.end(); // stop talking to the GPS
    delay(250);
    gpsattached = 0;
    unlockbox();
    delay(5000); // I have this much time to open the box
    lockbox();
  } else if (backdoortimerrunning && (millis()-backdoortimerstart >= backdoortimeout2) && !gamereset) { // test for threshold 2
    gamereset = 1;
    // reset gamestatus to '1'
    // reset tasknr to '0'
    EEPROM.write(0,1); // we're actually writing '1' as GS = 0 is just used to trigger a lock
    EEPROM.write(1,0); // tasknr = 0
    // change gamestate to 3 for shutdown (since we're already running on aux power, there should be no need to test? naa)
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Game has been reset");
    delay(1000);
    gamestate = 0; tasknr = 1;
    if (debug) 
      Serial.println("box has been reset");
      if (debug) {
      Serial.print("Gamestate changed: ");
      Serial.println(gamestate);
    }
    digitalWrite(POLULUPIN,HIGH);
    delay(100);
  } 

  if (debug) {
     Serial.print("Gamestate (pre gs0): ");
     Serial.println(gamestate);
  }


   switch(gamestate) {
    case 0: // game has just been reset - lock the box and shut down - states have been written above
       if (debug) {
         Serial.print("Gamestate (gs0): ");
         Serial.println(gamestate);
       }
       lcd.clear();
       lcd.print("box reset and will ");
       lcd.print("lock in 3 seconds");
       lcd.setCursor(0,3);lcd.print(gamestate);lcd.print(",");lcd.print(tasknr);lcd.print(millis());
       nss.end(); // "detach" the GPS before operating the servo
       gpsattached = 0;
       delay(250);
       lockbox();
       delay(3000); 
       digitalWrite(POLULUPIN,HIGH);
       delay(100);
       gamestate = 3;   
       break; 
    case 1: // the game is running
        if (millis()-mastertimerstart <= timeout) {
          // print current millis on the bottom of the LCD
          // timeout-millis()
          // only if we're not timed out
//          unsigned long remaindertime = timeout-millis();
          unsigned long remaindertime = timeout-millis()+mastertimerstart;
          long h,m,s,ms;
          unsigned long over;
          unsigned long elapsed=remaindertime;
          h=int(elapsed/3600000);
          over=elapsed%3600000;
          m=int(over/60000);
          over=over%60000;
          s=int(over/1000);
          ms=over%1000;
          // .. && gamestate = "running" skal der tilføjes .. eller noed
          if (millis()-previousMillis >= interval && remaindertime >= 0 && !timeoutreached) {
            lcd.setCursor(0,3);
            if (m < 10) { lcd.print("0"); }
            lcd.print(m); Serial.print(m);
            lcd.print(":"); 
            if (s < 10) { lcd.print("0"); }
            lcd.print(s); 
            previousMillis = millis();
          } else if (remaindertime <= 0) { lcd.setCursor(0,3); lcd.print("      "); }
        }
        if (millis()-mastertimerstart >= timeout) {
          if (!timeoutreached) {
            lcd.clear();
            lcd.setCursor(0,1);
            lcd.print("No signal - Good Bye");
            if (debug)
              Serial.println("Timeout reached - good bye!");
            delay(2000);
            // pull polulu high etc.
            digitalWrite(POLULUPIN,HIGH);
            // set gamestate to the limbo-state where we just wait for power to go (or the reset)
            timeoutreached = 1;
          } else if (timeoutreached && !powermessage){
            // wait a while then ask to have power removed - we have to set a flag so this step is detected alone
            gamestate = 3;
            if (debug) {
               Serial.print("Gamestate changed: ");
               Serial.println(gamestate);
            }
          }
        }
        // feed the GPS-object - we're going to do this a few times down the line
        // re-attach the GPS-module if it's been detached
        if (!servoattached && !gpsattached) { 
            nss.begin(9600);
            gpsattached = 1;
        }

        if (gpsattached) {
                // feed a few times, to get a good fix.
                feedgps(); 
                gps.f_get_position(&flat, &flon, &age);
        }        
        //if (age < 1000), then we probably have a fix
          switch(tasknr) {
            case 0: // welcome message and first mission - we're not showing this unless we actually have a GPS fix
                  if (age < 1000) { 
                      unsigned long distance = gps.distance_between(flat,flon,LABI_LAT,LABI_LON);
                      if (distance < 1000) {
                         lcd.clear();
                         stringToLCD("You made it to Labitat!"); 
                         delay(5000);
                         stringToLCD("Stand by for your next mission");
                         delay(5000);
                         tasknr++;
                         EEPROM.write(1,tasknr);
                         mastertimerstart = millis(); // resetting time just in case the GPS-signal dies for a bit
                      } else {
                           stringToLCD("Go to Labitat and   hack");
                           delay(10000);
                           lcd.clear();
                           lcd.setCursor(0,2);
                           lcd.print("Good Bye");
                           delay(3000);
                           digitalWrite(POLULUPIN,HIGH);
                           delay(100);
                           gamestate = 3; // switch to message if running on external power
                      }
                  }
            break; 
            case 1: // Second mission
                  if (age < 1000) { 
                      unsigned long distance = gps.distance_between(flat,flon,HOME_LAT,HOME_LON);
                      if (distance < 500) {
                         lcd.clear();
                         stringToLCD("You made it home!"); 
                         delay(10000);
                         stringToLCD("Stand by for your next mission");
                         delay(5000);
                         tasknr++;
                         EEPROM.write(1,tasknr);
                         mastertimerstart = millis(); // resetting time just in case the GPS-signal dies for a bit
                      } else {
                           stringToLCD("Go home");
                           delay(2000);
                           lcd.clear();
                           lcd.setCursor(0,2);
                           lcd.print("Good Bye");
                           delay(3000);
                           digitalWrite(POLULUPIN,HIGH);
                           delay(100);
                           gamestate = 3; // switch to message if running on external power
                      }
                  }
            break; 
            case 2: // Third mission
              stringToLCD("You've made it through..");
              delay(2000);
              stringToLCD("The box will open...");
              EEPROM.write(0,2); // setting gs to 2
              delay(3000);
              gamestate = 2;
            break; 
            case 3: // 4th mission
            break; 
        }
        break;
    case 2: 
         if (debug) {
           Serial.print("Gamestate (gs2): ");
           Serial.println(gamestate);
        }
        nss.end(); //stop talking to the GPS
        delay(250);
        gpsattached = 0;  
        unlockbox();
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("The box is open");
        lcd.setCursor(0,2);
        lcd.print("Good Bye");
        delay(5000);

        digitalWrite(POLULUPIN,HIGH);
        delay(100);
        gamestate = 3; // switch to message if running on external power
        break;

    case 3: // we're waiting for power to be removed
     if (!powermessage) {
       delay(1000);
       lcd.clear();
       lcd.setCursor(0,1);
       lcd.print("please remove power");
       powermessage = 1;
        if (debug) {
           Serial.print("Gamestate (gs3): ");
           Serial.println(gamestate);
        }
        
       if (debug)
         Serial.println("Please remove power");

     }
     break;
   }
 
 

} 

/* 
   various functions 
*/

void lockbox() {
 servoattached = 1;
  if (!myservo.attached()) { myservo.attach(SERVOPIN); myservo.write(10); }

// myservo.slowmove(10,70);
 myservo.write(10);
 servostart = millis();
 delay(500);
 digitalWrite(13,LOW);
}

void unlockbox() {
 servoattached = 1;
  if (!myservo.attached()) { myservo.attach(SERVOPIN); }

// myservo.slowmove(90,70);
 myservo.write(90);
 servostart = millis();
 delay(500);
 digitalWrite(13,HIGH);
}

void stringToLCD(char *stringIn) {
    int lineCount = 0;
    int lineNumber = 0;
    byte stillProcessing = 1;
    byte charCount = 1;
    lcd.clear();
    lcd.setCursor(0,0);

    while(stillProcessing) {
         if (++lineCount > 20) {    // have we printed 20 characters yet (+1 for the logic)
              lineNumber += 1;
              lcd.setCursor(0,lineNumber);   // move cursor down
              lineCount = 1;
         }

         lcd.print(stringIn[charCount - 1]);

         if (!stringIn[charCount]) {   // no more chars to process?
              stillProcessing = 0;
         }
         charCount += 1;
          delay(100);
    }
}

static bool feedgps()
{
  while (nss.available())
  {
    if (gps.encode(nss.read()))
      return true;
  }
  return false;
}
