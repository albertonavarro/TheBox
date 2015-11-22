/**
 * Version adapted by Alberto Navarro for his own box, using different LCD and less backdoor logic (none)
 * Thanks Mikal Hart for sharing your code and idea.
 * */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include <Servo.h>
#include <TinyGPS.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Time.h>

#include <MemoryFree.h>
#include <pgmStrToRAM.h>

TinyGPS gps;

SoftwareSerial nss(3,2);

LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

Servo myservo;
long previousMillis;
int interval = 1000;

int SERVOPIN = 8; 
long int servostart; // timer to keep track of the servo

// polulu power switch is on this pin
int POLULUPIN = 4;

// scale stuff for backdoor timer
// used both for backdoor and reset-functionality
// with scales, I am sure that the switch is triggered constantly when I start the backdoor timers
float v = 1; // init
float scalea = 0.99;
float scaleb = 0.01;
// timer for backdoor and reset 
typedef long int longjohn;  // declaring my own long int, just because we joke around with the name 'John' in Labitat.. 

// set to 1 when the box is open
int boxopen = 0;
// set to 1 when the backdoor timer reaches backdoor timer2 and this is still 0 - is then being set to 1 to trigger the actual reset
int gamereset = 0;

long int start; // timer for letting the gps-object 'feed' more than once
longjohn unsigned mastertimerstart; // the master timer is used to see if we're timing out on the GPS signal
// mastertimer = timeout
long unsigned int timeout = 70000; // how long will we wait for the GPS (should be 1½ minute or so)?
int timeoutreached = 0;
int powermessage = 0; // keeps track of wether or not we've displayed a message after timeout and shutdown should have occured (displayed when on external power)

int debug = 1; // enables serial debug + other things

/* GPS-related variables */
float flat, flon;
unsigned long age, date, time;
int Year;
byte Month, Day, Hour, Minute, Second;
const int offset = 2; // we're in UTC+2 now

// coordinates for the various locations for the puzzle box to go
static const float MILESTONE_1_LAT = 51.4878248, MILESTONE_1_LON = 0.063191;
static const int MILESTONE_1_THRESHOLD = 1000; // required minimum distance to Faaborg havn

static const float DAD_LAT = 51.4878248, DAD_LON = 0.063191;
static const int DAD_THRESHOLD = 500; // required minimum distance to DAD 

static const float MOM_LAT = 51.4878248, MOM_LON = 0.063191;
static const int MOM_THRESHOLD = 500;

// to prevent usage of the old Servo library, I am instead making sure, that I only talk to one of 
// Software Serial or the Servo at a time. These two flags keep check of that.
// only one can be 'attached' at the time
bool servoattached = 0;
bool gpsattached = 1;

/* declare gamestate and tasknr */
int gamestate; // RUNNING, etc.
int tasknr; // which location will we go to next?

/* setup */
void setup() 
{ 
  
                         
  lcd.init();   
  lcd.backlight();

  if (debug) {
    Serial.begin(115200);
    Serial.print(getPSTR("Free RAM = ")); // forced to be compiled into and read from Flash
    Serial.println(freeMemory(), DEC);  // print how much RAM is available. 
  }
  
  myservo.detach();
  delay(10); // small delay added for safety although tests so far have been good
  
  gpsattached = 1;
  nss.begin(9600); // initiate SoftwareSerial, which we use to talk to the GPS

  // set up the LCD's number of columns and rows: 
  //lcd.begin(16, 2);
  /*
   read Gamestate, etc. from EEPROM
   gamestate = pos 0
   tasknr = pos 1
  */
  gamestate = EEPROM.read(0);
 
  // if gamestate is bigger than a certain number, it has not been initiated, so we set it to '1' ('0' would cause an endless loop)
  // the manual states, that it's set to 255 if not initially set. You can usually just make a small sketch to set the
  // values instead of guessing like I do.
  if (gamestate > 200 || gamestate == 0) { 
    gamestate = 1; 
    EEPROM.write(0,gamestate); 
  }


  tasknr = EEPROM.read(1);
  // if tasknr is bigger than a certain number, it has not been initiated, so we write it back - see above
  if (tasknr > 200) { 
    tasknr = 0; 
    EEPROM.write(1,tasknr); 
  }

  if (debug) {
    Serial.print(getPSTR("Initial gamestate: "));
    Serial.println(gamestate);
    Serial.print(getPSTR("Initial tasknr: "));
    Serial.println(tasknr);
  }

  // Print a message to the LCD.
  // to be moved when the box goes live
  // or maybe print gamestatus + welcome (back)
  // for now I am making sure only to print welcome when the game is in the running state
  if (gamestate == 1) {
    // stringToLcd is explained below.
    if (tasknr == 0) { 
      stringToLCD(getPSTR("Bienvenidos!"), 2000); 
    } else { 
      stringToLCD(getPSTR("Otra vez por aqui?"), 2000); 
    }
    
    if (debug) {
      stringToLCD(getPSTR("tasknr: "));lcd.print(tasknr); delay(2000);
    }

    stringToLCD(getPSTR("Getting signal..."));
  }
 
  // debug-LED. Should probably be disabled when we go "live" as noone can see it anyway
  /*pinMode(13,OUTPUT);
  if (debug) { 
    digitalWrite(13,LOW); 
  }*/

  /* polulu-pin - pull this one high, and the battery power is cut - has no effect on external power */
  pinMode(POLULUPIN,OUTPUT);
  digitalWrite(POLULUPIN,LOW);

  // mastertimer starts here
  mastertimerstart = millis();
} 
 
void loop() { 

   // re-attach the GPS-module if it's been detached, but only if the servo is detached
   // as the servo will start twitching like crazy if we start talking serial before it detaches
   if (!servoattached && !gpsattached) { 
     Serial.print("!servoattached && !gpsattached");
     nss.begin(9600);
     gpsattached = 1;
   }

   // detach the servo if timeout after movement is reached
   /*if (myservo.attached() && millis()-servostart > 4000) { 
     myservo.detach();
     delay(10); // small delay added for safety although tests so far have been good
     servoattached = 0;
     nss.begin(9600); // the servo has been detached, now we can re-enable GPS
     gpsattached = 1;
   } */
 
   if (gpsattached) {
         Serial.print("if (gpsattached)");
         // feed a few times, to get a good fix, but only if attached - we start feeding up to half a second to make sure we get a fix and 
         // date/time information
         feedgps();
         gps.f_get_position(&flat, &flon, &age);
   }
   
   if (debug) {
     Serial.print("Gamestate (pre gs0): ");
     Serial.println(gamestate);
   }

   // here we do a switch on the gamestate to decide what to do
   switch(gamestate) {
    
    case 1: // the game is running
          // the count down thingy while we're searching for signal
          // - should probably be disabled when we're changing locations.. 
          // maybe a 'showcountdown'-variable that is set to off just before the last step?
        if (millis()-mastertimerstart <= timeout) {
            // print current millis on the bottom of the LCD
            // timeout-millis()
            // only if we're not timed out
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
            if (millis()-previousMillis >= interval && remaindertime >= 0 && !timeoutreached) {
              lcd.setCursor(0,3);
              if (m < 10) { lcd.print("0"); }
              lcd.print(m); 
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
              stringToLCD(getPSTR("No signal - Good Bye"));
              if (debug)
                Serial.println(getPSTR("Timeout reached - good bye!"));
              delay(2000);
              // pull polulu high etc.
              digitalWrite(POLULUPIN,HIGH);
              // set gamestate to the limbo-state where we just wait for power to go (or the reset)
              timeoutreached = 1;
            } else if (timeoutreached && !powermessage){
              // wait a while then ask to have power removed - we have to set a flag so this step is detected alone
              gamestate = 3;
              if (debug) {
                 Serial.print(getPSTR("Gamestate changed: "));
                 Serial.println(gamestate);
              }
            }
        }
          
        // re-attach the GPS-module if it's been detached
        if (!servoattached && !gpsattached) { 
            nss.begin(9600);
            gpsattached = 1;
        }
        
        if (gpsattached) {
            // feed a few times, to get a good fix, but only if attached - we start feeding up to half a second to make sure we get a fix and 
            // date/time information
            feedgps();
            gps.f_get_position(&flat, &flon, &age);
            // this is the last run of feedgps before we check for fix etc..
            // if age is < 1000, we have a fix - so we run feedgps for another 1000ms, to make sure we have
            // date and time as well (it won't work otherwise)
            //if (age < 1000), then we probably have a fix and so we find the current time etc.
            if (age < 1000) {
              updatedatetime(); // runs for 1000ms to make sure we have date-time correct
              gps.crack_datetime(&Year, &Month, &Day, &Hour, &Minute, &Second, NULL, &age); 
              setTime(Hour, Minute, Second, Day, Month, Year);
              adjustTime(offset * SECS_PER_HOUR);
            }
        }
        
        switch(tasknr) {
            case 0: // welcome message and first mission - we're not showing this unless we actually have a GPS fix
                  if (age < 1000) { 
                      unsigned long distance = gps.distance_between(flat,flon,MILESTONE_1_LAT,MILESTONE_1_LON);
                      // are we within 1000m? (could probably be set lower to make it more exciting)
                      // are we within threshold?
                      if (distance < MILESTONE_1_THRESHOLD) {
                         stringToLCD(getPSTR("Enhorabuena, habeis llegado al hito 1."), 1000);
                         tasknr++;
                         EEPROM.write(1,tasknr);
                         mastertimerstart = millis(); // resetting time just in case the GPS-signal dies for a bit
                      } else {
                           stringToLCD(getPSTR("Id al hito 1, si quereis que la caja se abra."), 3000);
                           stringToLCD(getPSTR("Apagandome..."), 3000);
                           digitalWrite(POLULUPIN,HIGH);
                           delay(100);
                           gamestate = 3; // switch to message if running on external power
                      }
                  }
            break; 
            case 1: // Second mission
                  if (age < 1000) { 
                      unsigned long distance = gps.distance_between(flat,flon,DAD_LAT,DAD_LON);
                      if (distance < DAD_THRESHOLD) {
                         stringToLCD(getPSTR("Hito 2"), 1000); 
                         tasknr++;
                         EEPROM.write(1,tasknr);
                         mastertimerstart = millis(); // resetting time just in case the GPS-signal dies for a bit
                      } else {
                           stringToLCD(getPSTR("Id al hito 1, si quereis que la caja se abra."), 3000);
                           stringToLCD(getPSTR("Apagandome..."), 3000);
                           digitalWrite(POLULUPIN,HIGH);
                           delay(100);
                           gamestate = 3; // switch to message if running on external power
                      }
                  }
            break; 

            case 2: // 3rd mission
                  if (age < 1000) { 
                      unsigned long distance = gps.distance_between(flat,flon,MOM_LAT,MOM_LON);
                      if (distance < MOM_THRESHOLD) {
                         stringToLCD(getPSTR("Hito 3"), 1000); 
                         tasknr++;
                         gamestate = 2;
                         EEPROM.write(0, 2);
                         mastertimerstart = millis(); // resetting time just in case the GPS-signal dies for a bit
                      } else {
                           stringToLCD(getPSTR("Go and visit mom -  The one in the same country as you are in"), 3000);
                           stringToLCD(getPSTR("Good Bye"), 3000);
                           digitalWrite(POLULUPIN,HIGH);
                           delay(100);
                           gamestate = 3; // switch to message if running on external power
                      }
                  }
            break; 
        }
        break;
    case 2: // the game is over - time to open the box 
         if (debug) {
           Serial.print(getPSTR("Gamestate (gs2): "));
           Serial.println(gamestate);
        }
        nss.end(); //stop talking to the GPS
        delay(250);
        gpsattached = 0;  
        unlockbox();
                
        stringToLCD(getPSTR("La caja se ha abierto."));
        stringToLCD(getPSTR("Felisitasiones!!"));
        delay(3000);
        digitalWrite(POLULUPIN,HIGH);
        delay(100);
        gamestate = 3; // switch to message if running on external power
        
        EEPROM.write(0,0);
        EEPROM.write(1,0);
  
        break;
    case 3: // we're waiting for power to be removed
     if (!powermessage) {
       delay(1000);
       lcd.clear();
       stringToLCD(getPSTR("please remove power"));
       powermessage = 1;
        if (debug) {
           Serial.print(getPSTR("Gamestate (gs3): "));
           Serial.println(gamestate);
        }
        
       if (debug)
         Serial.println(getPSTR("Please remove power"));

     }
     break;
   }
 
} 

/* 
   various functions 
*/

void lockbox() {
  servoattached = 1;
  if (!myservo.attached()) { 
    myservo.attach(SERVOPIN); 
    myservo.write(10); 
  }

  // myservo.slowmove(10,70);
  myservo.write(0);
  servostart = millis();
  delay(500);
  
  myservo.write(0);
 
}

void unlockbox() {
  servoattached = 1;
  if (!myservo.attached()) { 
    myservo.attach(SERVOPIN); 
  }

  myservo.write(90);
  servostart = millis();
  delay(500);
  if (debug) { 
    digitalWrite(13,HIGH); 
  }
}

void stringToLCD(char *stringIn) {
  stringToLCD(stringIn, 500);
}

void stringToLCD(char *stringIn, int addedDelay) {
    int lineCount = 0;
    int lineNumber = 0;
    byte stillProcessing = 1;
    byte charCount = 1;
    lcd.clear();
    lcd.setCursor(0,0);

    while(stillProcessing) {
         if (++lineCount > 16) {    // have we printed 20 characters yet (+1 for the logic)
              if (lineNumber == 0) {
                lineNumber = 1;
              } else {
                lineNumber = 0;
                delay(500);
                lcd.clear(); //clear screen
              }              
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
    delay(addedDelay);
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

// this functions takes a second to complete, but is only called when the 'age' paramater returned GPS is less than 1000 
// (when we have a fix)
void updatedatetime() {
  start = millis();
  while (millis() - start < 1000) {
    feedgps();
  }
}
