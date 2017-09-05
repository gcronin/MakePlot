/**********************************************************************
      
      sim.ino - Sunlight Intensity Monitoring project
      
      This project monitors sunlight intensity typically over a ten 
      hour day as measured by a solar cell connected to pins A0 and 
      Gnd on an Arduino UNO microcontroller. It uses the MakerPlot 
      Software Toolkit to implement a user display and control panel. 
      v 1.7.0) 
      
      The start time of a future plot period is configurable, and 
      the system is designed to run unattended, saving a screen 
      capture of the resulting plot at the end of the selected x-axis
      time span.
      
      Last update: 3/5/17
      K. Fisher
      
**********************************************************************/

#include <Arduino.h> 
#include <EEPROM.h>
#include <Time.h>
#include <TimeLib.h>
#include <avr/pgmspace.h>  // Required for F-macro. 

// defines
#define FIELD_WAS_CHANGED     1    // pTime field at lower right
#define SYNC_TIME_INTERVAL   15    // minutes
#define SEC_CORRECTION        1    // Extra sec added when set clock

// Machine states
enum states {IDLE, COUNTDOWN, PLOTTING, STOPPED}; 

// Global Variables
states state;   // Current state         

//============================ Supporting Functions ===========================

// The PC/MakerPlot buffer may not always flush completely. Therefore 
// finish emptying it manually. (See MakerPlot User Guide, p 108)              
 void flushBuffer(){  
   Serial.flush();     
   while (Serial.available()) 
     Serial.read();
   return;
}

// Update MakerPlot display of Arduino time and date
void updateMPlotTime() {
    char aTime[25];
    static int lastMin;
    // Update/time field when minute increments.
    if(minute() != lastMin){
      sprintf(aTime, "%02d/%02d/%d  %02d:%02d",
             month(), day(), year(), hour(), minute());      
      Serial.print("!pobj aTime.text = "); Serial.println(aTime);      
      lastMin = minute();
   }
}

// Returns true if user clicked "30 sec start button"
bool thirtySecStartReq(){  
  flushBuffer();
  Serial.println(F("!READ (startIn30.value)")); 
  int state = Serial.parseInt();
  if(state) return true;
  else return false;
}

// Returns true if user clicked "Start as per below" button
bool startAsPer(){  
  flushBuffer();
  Serial.println(F("!READ (startAsPer.value)")); 
  int state = Serial.parseInt();
  if(state) return true;
  else return false;
}

 // Append timestamped line of text to the Event Log 
void stext(String str) { 
   char ts[10];
   sprintf(ts,  "%02d:%02d:%02d", hour(), minute(), second());
   Serial.print(F("!POBJ stxt.add = \\n ")); Serial.print(ts); 
   Serial.print("  "); Serial.println(str);
}

// Determine next day date string to offer as default for the 
// next plot start event (mm/dd/yyyy)
char* nextDay(){
   static char buf[11]; 
   time_t t;
   t = now() + SECS_PER_DAY;
   sprintf(buf, "%02d/%02d/%4d", month(t), day(t), year(t));
   return buf;
}

// Sync the Time lib s/w clock to the PC h/w clock via MakerPlot link
void syncSWClock() {
   flushBuffer();     
   Serial.println("!Send [(DATE),Trim] ");
   String dateStr = Serial.readString();
   int yr   = dateStr.substring(6,8).toInt();
   int mo   = dateStr.substring(0,2).toInt(); 
   int day  = dateStr.substring(3,5).toInt();  
   flushBuffer();
   Serial.println("!Send [(RTIME),Trim] ");
   String timeStr = Serial.readString();
   int hr   = timeStr.substring(0,2).toInt(); 
   int min  = timeStr.substring(3,5).toInt(); 
   int sec  = timeStr.substring(6,8).toInt(); 

   // Set the Arduino s/w clock == PC clock
   setTime(hr, min, sec + SEC_CORRECTION, day, mo, yr);
}

// Debug - View string content. Prints hexidecimal contents of ASCII string 
// for number of locations specified - in the MPlot DEBUG WINDOW.   
void dbgStr(char* str, int numLoc){ 
   Serial.print("!DBUG ");   
	for(int i =0; i < numLoc; i++) {
		if(str[i] < 0x10)	Serial.print('0');			
		Serial.print(0x7f & str[i], HEX); Serial.print(" ");
	}
	Serial.println(); Serial.println();
}

void setup() {   
   char buf[10];   
   Serial.begin(115200); 
   
   // The first statement below after Serial.begin does not execute
   // consistently when communicating with MakerPlot. This behavior 
   // appears to be an initiaization issue only, and not a cause 
   // for concern.
   Serial.println(F("!STAT Throw-away text to prime the pump"));  
   Serial.println(F("!STAT Solar Intensity Monitor  v1.0"));  // This one does  
   
   // Initial software clock sync
   syncSWClock();

   // Start new event log    
   Serial.println(F("!POBJ stxt.clear"));   
   stext(F("EVENT LOG opened")); 
   stext(F("Arduino s/w clock synced"));
   stext(F("Please enter plot start time"));   

   // Update offered plot start DATE field to specify tomorrow's date
   Serial.print(F("!POBJ pDate.text = ")); Serial.println(nextDay());  
   
   // Update offered start TIME field from last entered value as 
   // stored in EEPROM. Format HHMM, no colon.
   for(int i = 0; i < 4; i++){
      buf[i] = EEPROM.read(i);
   }
   buf[4] = '\0';   
   Serial.print(F("!POBJ pTime.text =  ")); Serial.println(buf);

   // MakerPlot initialization
   Serial.println(F("!POBJ untilStart.text =           ---"));
   Serial.println(F("!span 0, 5.0"));
   Serial.println(F("!PLOT OFF"));   
   Serial.println(F("!YLBL VOLTAGE     "));   
   Serial.println(F("!RSET"));
   Serial.println(F("!POBJ startIn30.value = 0 "));
   Serial.println(F("!POBJ startAsPer.value = 0 "));  
   Serial.println(F("!POBJ pTimeEvent.value = 0 "));  
   Serial.println(F("!POBJ dField.value = "));  
   Serial.println(F("!POBJ tField.value = "));  
   Serial.println(F("!POBJ dpoint.text =  "));
   Serial.println(F("!POBJ ptsPct.text =  "));
   
   // Start up in the IDLE state
   state = IDLE;       
 }
 
void loop(){  

   static time_t     plotStartTime;
   static time_t     nextCheckTime;   // Re Arduino/PC clock tracking
   static int        pTimeFlag;       // Plot start time was changed
   char              buf[50];         // General use

   // Update MakerPlot display of Arduino date and time
   updateMPlotTime(); /////////////////////////////////////////////////////////
   
  // Periodically sync the Arduino software clock. (Because the compiler 
  // initializes the nextCheckTime variable to zero, this routine runs at 
  // startup, and then every SYNC_TIME_INTERVAL seconds later 
   if(now() > nextCheckTime){
      nextCheckTime = (now() + (SYNC_TIME_INTERVAL * SECS_PER_MIN)-1);
      flushBuffer();
      // Get PC time & report any existing error, then sync
      Serial.println(F("!Send [(RTIME),Trim] "));
      String timeStr = Serial.readString();
      int pcSec  = timeStr.substring(6,8).toInt(); 
      sprintf(buf, "Software Clock Error: %d sec", (second() - pcSec));      
      stext(buf);  
      syncSWClock();
      stext(F("S/w Clock synced"));
   }

   // Check the flag that indicates that a new future plot start 
   // time was entered. pTimeEvent is a hidden LED behind the plot grid. 
   // It is configured as an ON-OFF type and used as S/R flip-flop. Here 
   // we check if the pTime field has been changed, reset the flag, and 
   // save the new entry to EEPROM    
   flushBuffer();        
   Serial.println(F("!READ [(pTimeEvent.value),Trim]"));  
   // Serial.println(F("!READ (pTimeEvent.value)"));     
   pTimeFlag = Serial.parseInt();   
   if(pTimeFlag == FIELD_WAS_CHANGED) {      
      flushBuffer();
      Serial.println(F("!READ (pTime.text)")); 
      String pTimeText = Serial.readString();
      Serial.println(F("!POBJ pTimeEvent.value = 0")); // Reset 
      sprintf(buf, "New start time: %s", pTimeText.c_str());
      stext(buf);           
      for(int addr = 0; addr < 4; addr++) {      
         EEPROM.write(addr, pTimeText.charAt(addr));
      }  
   }
 
   //============ Machine state related code here to end ======================
   
   // Take action depending on current machine state
   switch(state) {
      case IDLE: {
         // Update System Status indicator
         Serial.println(F("!POBJ sStatus.Back Color = 12")); // Red
         Serial.println(F("!POBJ sStatus.Text Color = 15")); // White
         Serial.println(F("!POBJ sStatus.Text =          IDLE")); 

         // If START PLOT IN 30 SEC button was pressed, start 
         // counting down, target: now +30 
         if(thirtySecStartReq()) {       
            plotStartTime = now() + 30UL; 
            state = COUNTDOWN;                              
            stext(F("Starting plot in 30 sec "));                
            Serial.println(F(" !POBJ startIn30.value = 0 "));   
         }  
         
         // Or if a specific future plot start time has been requested via the 
         // "Start as per below..." PB switch, process it.       
         if((startAsPer())) {
            flushBuffer();
            Serial.println(F("!READ (pDate.text)")); 
            String pDate = Serial.readString(); // mm/dd/yyyy
            flushBuffer();      
            Serial.println(F("!READ (pTime.text)")); // hhmm
            String pTime = Serial.readString();  
            
            int mo = pDate.substring(0,2).toInt();  
            int dy = pDate.substring(3,5).toInt(); 
            int yr = pDate.substring(6,10).toInt();  
            int hr = pTime.substring(0,2).toInt();  
            int mn = pTime.substring(2,4).toInt(); 
            
            // Compute seconds at requested plot start time                           
            tmElements_t pstElem;
 
            pstElem.Second = 0; 
            pstElem.Hour   = hr;
            pstElem.Minute = mn;
            pstElem.Day    = dy;
            pstElem.Month  = mo;
            pstElem.Year   = yr - 1970;
            plotStartTime  = makeTime(pstElem);

            state = COUNTDOWN;    

            // Reset pushbutton            
            Serial.println(F(" !POBJ startAsPer.value = 0 "));      
            // Update event log
            sprintf(buf, "Plot start: %02d/%02d/%02d  %02d:%02d", mo,dy,yr,hr,mn);
            stext(buf); 
            stext(F("Entered COUNTDOWN State")); 
                            
         }        
      } break;
         
      case COUNTDOWN:{
         Serial.println(F("!POBJ sStatus.Back Color = 9")); // Blue
         Serial.println(F("!POBJ sStatus.Text Color = 15")); // White
         Serial.println(F("!POBJ sStatus.Text =  COUNTDOWN"));
         
         // check countdown progress     
         time_t remainingSec = plotStartTime - now();
         if(remainingSec > 0) {     // Not there yet
            time_t rMin = remainingSec/60UL;
            time_t rSec = remainingSec%60UL;   
            sprintf(buf, "%2d:%02d", (int)rMin, (int)rSec);      
            Serial.print(F("!POBJ untilStart.text =         ")); Serial.println(buf);      
         }
         else {
            // Start plotting
            state = PLOTTING; 
            stext(F("Plotting started "));           
            Serial.println(F("!PLOT ON"));     // Start plotting command to MakerPlot
            Serial.println(F("!RSET"));  
            Serial.println(F("!POBJ untilStart.text =           ---"));            
         }
      } break;
         
      case PLOTTING:  {
         Serial.println(F("!POBJ sStatus.Back Color = 10")); // Green
         Serial.println(F("!POBJ sStatus.Text Color = 0")); // Black
         Serial.println(F("!POBJ sStatus.Text =     PLOTTING"));  

         //  service the plot grid and meter
         float v1 = (5.0 * analogRead(A2))/1024; 
         Serial.println(v1); // Bare numeric data goes directly to the plot grid
         Serial.print(F("!POBJ voltMeter.value =  ")); Serial.println(v1);  
         
         // Update DPOINT display (Data points currently stored in memory)
         flushBuffer();
         Serial.println(F("!Send [(DPOINT),Trim] "));
         int dpoint = Serial.parseInt();
         Serial.print(F("!POBJ dpoint.text =  ")); Serial.println(dpoint);

         // Determine DPOINT max & percent full
         flushBuffer();
         Serial.println(F("!Send (txtDpoints.text)")); 
         int dpointMax = Serial.parseInt();
         Serial.print(F("!POBJ ptsPct.text =  ")); Serial.print((100*dpoint)/dpointMax); 
         Serial.println(F(" %")); 
               
        // Change system state to STOPPED if plot is inactive and stopped at max time
        // Wait until (dpoint > 2) so we get past startup
        static int oldDpoint;
        if((dpoint > 2) && (dpoint == oldDpoint)){      
            flushBuffer();
            Serial.println(F("!SNAP SIM"));   
            stext(F("Screen snapshot saved"));   
            stext(F("Plot STOPPED at max time "));
            state = STOPPED;             

        }
        oldDpoint = dpoint;
      } break;
         
      case STOPPED:
         Serial.println(F("!POBJ sStatus.Back Color = 14")); // Yell0w
         Serial.println(F("!POBJ sStatus.Text Color = 0")); // Black
         Serial.println(F("!POBJ sStatus.Text =     STOPPED"));   
   } // switch    
  } // loop   


 
   

   

     
   
   
   

   
   
   
   
   
   
   
   
   
   
 
















