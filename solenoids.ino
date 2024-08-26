/*
  simple_example

  This code connects and initializes one shield (with address set to 000, otherwise change the SHIELD_ADRESS value to 1, 2, 3, etc...)
  It sets the solenoid outputs states to LOW, and once setup is done, it cycles through the outputs and sets them to HIGH sequentially
*/

// Make sure you installed the Adafruit MCP23017 library 
// (Sketch > Include Library > Manage Libraries... > *search for MCP23017, select Adafruit one* > Install)
#include <Wire.h>
#include <MCP23017.h>
#include <string.h>

#define MCP23017_ADDR 0x21 // SHIELD/MCP23017 address (change to 1-8 if soldered differently)
#define BAUD_RATE 9600
#define PUMP_MIN 100 // Minimum duration for the "on" state of the Lee Co. LPM pumps


String inString = String(100);  
char strValue[12];


MCP23017 mcp = MCP23017(MCP23017_ADDR);

// State variables
word mcpstate = 0;

// PUMP structure:
struct PUMP
{
    int pin;
    long period = 200; // in ms, 200 or more
    int channel;
    bool active = 0;
    unsigned long lastswitch = 0; // The time will overflow after about 50 days...
};
// For now it is just an empty array. The array will be updated dynamically during runtime.
PUMP* PUMParr = 0;
int PUMParrsize = 0;



// Valve structure:
struct VALVE
{
    int pin;
    int channel;
    bool ison = 0;
    bool PWMon = 0;
    int PWMperc = 50; // PWM percentage
    long period = 10000; // in ms, 10 or more (preferrably on the order of seconds)
    unsigned long lastswitch = 0; // The time will overflow after about 50 days...
};

// For now it is just an empty array. The array will be updated dynamically during runtime.
VALVE* VALVEarr = 0;
int VALVEarrsize = 0;

int indcmd = -1;
int i;

void setup()
{
  Wire.begin();
  // Intialize serial connection and LED pin
  Serial.println("Starting up...");
  Serial.begin(BAUD_RATE);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);

  // Connect to MCP
  Serial.println("Initializing i2c for mcp23017 comm...");
  mcp.init();

  
  // Set MCP pins as outputs
  Serial.println("Setting up MCP pins to outputs...");
  mcp.portMode(MCP23017Port::A, 0x00); // Set all pins on port A as output
  mcp.portMode(MCP23017Port::B, 0x00);
  // Write init state to MCP
  Serial.println("Setting up MCP pins init state...");
  applystate();

  // Initialize valves:
  for (int i = 0; i<8; i++)
  {
    createnewValve(i, i+1, 0, 0, 50, 5000);
  }
  
  // Initialize pumps:
  for (int i = 0; i<8; i++)
  {
    createnewPump(8+i, 200, i+1, 0);
  }

  Serial.println("Setup done!");
}

void loop() {

  
  indcmd = 0;
  inString = String(100);

  updatestate();

  // Anything on the serial?
  while (Serial.available()) 
  {
    delay(10);  //small delay to allow input buffer to fill
    if (Serial.available() >0) 
    {
        char c = Serial.read();  //gets one byte from serial buffer
        if (c == ';') {break;}  //breaks out of capture loop to print readstring
        inString += c; //makes the string inString
    }
  }

  // VAL command? (Set the valves state) (Note: If the valve is in PWM state, this will be overidden by the PWM subroutine)
  //VALXXXSX (XXX = valve channel, X = state)
  indcmd = inString.indexOf("VAL"); // Finds the VAL command start sequence (set valve)
  if(indcmd >= 0)
  {
      int i = findValve((int)inString.substring(indcmd+3,indcmd+6).toInt());

      if(i>=0)
      {
        VALVEarr[i].ison = (bool)inString.substring(indcmd+7,indcmd+8).toInt();
      } 
  }

  

  // PUM command? (Set the pump state)
  //PUMXXXSXP[XXX] (XXX = valve channel, X = state, [XXX] = period (ms))
  indcmd = inString.indexOf("PUM"); // Finds the PUM command start sequence (set pump)
  if(indcmd >= 0)
  {
      
      int i = findPump((int)inString.substring(indcmd+3,indcmd+6).toInt());

      if(i>=0)
      {
        PUMParr[i].active = (bool)inString.substring(indcmd+7,indcmd+8).toInt();
        PUMParr[i].period = inString.substring(indcmd+9).toInt();
        if(PUMParr[i].period<200) PUMParr[i].period = 200; // Period must be > 200ms
      }
  }


  // PWM command? (Set valve in pwm mode)
  //PWMXXXSX%XXXP[XXX] (XXX = pump channel, X = state, XXX = duty cycle percentage, [XXX] = period (ms))
  indcmd = inString.indexOf("PWM"); // Finds the PUM command start sequence (set pump)
  if(indcmd >= 0)
  {
      int i = findValve((int)inString.substring(indcmd+3,indcmd+6).toInt());

      if(i>=0)
      {
        VALVEarr[i].PWMon = (bool)inString.substring(indcmd+7,indcmd+8).toInt();
        VALVEarr[i].PWMperc = (int)inString.substring(indcmd+9,indcmd+12).toInt();
        VALVEarr[i].period = inString.substring(indcmd+13).toInt();
        if(VALVEarr[i].period<10) VALVEarr[i].period = 10; // Period must be >= 10ms
      }
  }


}


void updatestate()
{
    word newstate = mcpstate;
    unsigned long now = millis();
    long elapsed;

    // Run through pumps:
    for (int i = 0; i<PUMParrsize;i++)
    {
      elapsed = now - PUMParr[i].lastswitch;
      if (!PUMParr[i].active)
      {
        bitClear(newstate,PUMParr[i].pin);
        PUMParr[i].lastswitch = now;
      }
      else if (elapsed > PUMParr[i].period)
      {
        bitSet(newstate,PUMParr[i].pin);
        PUMParr[i].lastswitch = now;
      }
      else if (elapsed < 0) // if the millis() long overflows, reset
      {
        bitSet(newstate,PUMParr[i].pin);
        PUMParr[i].lastswitch = now;
      }
      else if (elapsed < PUMP_MIN)
      {
        bitSet(newstate,PUMParr[i].pin);
      }
      else
      {
        bitClear(newstate,PUMParr[i].pin);
      }
    }

    // Run through valve PWMs:
    for (int i = 0; i<VALVEarrsize;i++)
    {
      elapsed = now - VALVEarr[i].lastswitch;
      if (!VALVEarr[i].PWMon)
      {
        VALVEarr[i].lastswitch = now;
      }
      else if (elapsed > VALVEarr[i].period)
      {
        VALVEarr[i].ison = 1;
        VALVEarr[i].lastswitch = now;
      }
      else if (elapsed < 0) // if the millis() long overflows, reset
      {
        VALVEarr[i].ison = 1;
        VALVEarr[i].lastswitch = now;
      }
      else if (elapsed < (VALVEarr[i].period*VALVEarr[i].PWMperc)/100)
      {
        VALVEarr[i].ison = 1;
      }
      else
      {
        VALVEarr[i].ison = 0;
      }
      
    }
    
    // Run through the valves states:
    for (int i = 0; i<VALVEarrsize;i++)
    {
        if (VALVEarr[i].ison)
        {
          bitSet(newstate,VALVEarr[i].pin);
        }
        else
        {
          bitClear(newstate,VALVEarr[i].pin);
        }
    }
          
    if (newstate!=mcpstate) // only update if necessary
    {
        mcpstate = newstate;
        applystate();
    }
}


void applystate()
{
  //printstate(mcpstate); // For debugging
  mcp.writeRegister(MCP23017Register::GPIO_A, (mcpstate >> 8) & 0xFF);
  mcp.writeRegister(MCP23017Register::GPIO_B, (mcpstate << 8) & 0xFF);
}


void printstate(word state)   
{ 
    for (word mask = 0x8000; mask; mask >>= 1)
    {
      Serial.print(mask&state?'1':'0');
    }
    Serial.println();
}   

void createnewValve(int pin, int channel, bool ison, bool PWMon, int PWMperc, long period)
{
    VALVEarrsize++;
    // Allocation or re-allocation
    if (VALVEarr != 0)
    {
        VALVEarr = (VALVE*) realloc(VALVEarr, VALVEarrsize * sizeof(VALVE));
    }
    else
    {
        VALVEarr = (VALVE*) malloc(VALVEarrsize * sizeof(VALVE));
    }

    Serial.print("Creating valve number: ");
    Serial.println(VALVEarrsize);

    VALVEarr[VALVEarrsize-1].pin = pin ;
    VALVEarr[VALVEarrsize-1].channel = channel ;
    VALVEarr[VALVEarrsize-1].ison = ison ;
    VALVEarr[VALVEarrsize-1].PWMon = PWMon;
    VALVEarr[VALVEarrsize-1].PWMperc = PWMperc ;
    VALVEarr[VALVEarrsize-1].period = period ;
    VALVEarr[VALVEarrsize-1].lastswitch = millis();

}

int findValve(int channel)
{
    // Find the VALVE struct to update in the array. (Only one)
    for (int i = 0; i<VALVEarrsize; i++)
    {
        if (VALVEarr[i].channel==channel)
        {
            return i;
        }
    }
    return -1;
}

void createnewPump(int pin, long period, int channel, bool active)
{
    PUMParrsize++;
    // Allocation or re-allocation
    if (PUMParr != 0)
    {
        PUMParr = (PUMP*) realloc(PUMParr, PUMParrsize * sizeof(PUMP));
    }
    else
    {
        PUMParr = (PUMP*) malloc(PUMParrsize * sizeof(PUMP));
    }

    Serial.print("Creating pump number: ");
    Serial.println(PUMParrsize);

    PUMParr[PUMParrsize-1].pin = pin;
    PUMParr[PUMParrsize-1].period = period;
    PUMParr[PUMParrsize-1].channel = channel ;
    PUMParr[PUMParrsize-1].active = active;
    PUMParr[PUMParrsize-1].lastswitch = millis();
}

int findPump(int channel)
{
    // Find the PUMP struct to update in the array. (Only one)
    for (int i = 0; i<PUMParrsize; i++)
    {
        if (PUMParr[i].channel==channel)
        {
            return i;
        }
    }
    return -1;
}