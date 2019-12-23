#include <TheThingsNetwork.h>
#include <SoftwareSerial.h>

// General def for adafruit sensor
#include <Adafruit_Sensor.h>

// Includes for BME680 
#include <bme680.h>
#include <Adafruit_BME680.h>
#include <bme680_defs.h>

#include "Sds011.h"

// Includes for BME280pac
//#include <Adafruit_BME280.h>

// Set your DevAddr, NwkSKey, AppSKey and the frequency plan
const char *devAddr = "********";
const char *nwkSKey = "********************************";
const char *appSKey = "********************************";
#define SPREAD_FACTOR 11
#define freqPlan TTN_FP_EU868

#define loraSerial Serial1
#define debugSerial Serial

TheThingsNetwork ttn(loraSerial, debugSerial, freqPlan, SPREAD_FACTOR);


//Development / production profiles
//#define NO_CONNECTION_PROFILE 1
//#define DEBUG_PROFILE 1

#ifdef DEBUG_PROFILE
  #define NUM_MEASURE_SESSIONS 20
  #define CYCLE_DELAY 2000
  #define SH_DEBUG_PRINTLN(a) debugSerial.println(a)
  #define SH_DEBUG_PRINT(a) debugSerial.print(a)
  #define SH_DEBUG_PRINT_DEC(a,b) debugSerial.print(a,b)
  #define SH_DEBUG_PRINTLN_DEC(a,b) debugSerial.println(a,b)
#else
  #define NUM_MEASURE_SESSIONS 90
  #define CYCLE_DELAY 10000
  #define SH_DEBUG_PRINTLN(a) 
  #define SH_DEBUG_PRINT(a) 
  #define SH_DEBUG_PRINT_DEC(a,b) 
  #define SH_DEBUG_PRINTLN_DEC(a,b) 

#endif

//Sharp Dust Sensor Pins
#define SDS_RX_PIN 8
#define SDS_TX_PIN 5

//Init global objects
Adafruit_BME680 bme680; // I2C
//Adafruit_BME280 bme280; // I2C

//Noise sensor pins
#define NOISE_MEASURE_PIN A0
#define NUM_NOISE_SAMPLES 1000

SoftwareSerial sdsSerial(SDS_RX_PIN, SDS_TX_PIN); // RX, TX
sds011::Sds011 sdsSensor(sdsSerial);

//Buffers
unsigned char data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA,};
char buffer[256];

//Flags
boolean isOkSetup = false;
bool hasBME680 = false;
bool hasBME280 = false;

void setup(void)
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
    loraSerial.begin(57600);
    debugSerial.begin(57600);
    
    // Wait a maximum of 10s for Serial Monitor
    while (!debugSerial && millis() < 10000)
      ;

    SH_DEBUG_PRINTLN("Startup");
      #ifndef NO_CONNECTION_PROFILE

      debugSerial.println("-- PERSONALIZE");
      ttn.personalize(devAddr, nwkSKey, appSKey);
    
      debugSerial.println("-- STATUS");
      ttn.showStatus();
  
      // Give it some time to join the network
      // To prevent "LoRaWAN modem is busy" messages
      isOkSetup = true;
      SH_DEBUG_PRINTLN("Joined network, waiting for modem...");
    #else
      isOkSetup = true;
    #endif

    SH_DEBUG_PRINTLN("Init SDS sensor.");
    //Init the pm SENSOR
    sdsSerial.begin(9600);
    delay(2000);
    sdsSensor.set_sleep(true);

    //Init the temp/hum sensor
    // Set up oversampling and filter initialization
    //SH_DEBUG_PRINTLN("Looking for BMEx80....");
    hasBME680 = true;
    if (!bme680.begin(0x76)) {
      if (!bme680.begin(0x77)) {
        SH_DEBUG_PRINTLN("No BMEx80 found! Check wiring!");
        hasBME680 = false;
      }
    }
    if (hasBME680) {
      bme680.setTemperatureOversampling(BME680_OS_8X);
      bme680.setHumidityOversampling(BME680_OS_2X);
      bme680.setPressureOversampling(BME680_OS_4X);
      bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
      bme680.setGasHeater(320, 150); // 320*C for 150 ms
    }
  
//    if (!hasBME680) {
//      //No BME680 found, trying out BME280 instead
//      hasBME280 = true;
//      if (!bme280.begin(0x76)) {
//        if (!bme280.begin(0x77)) {
//          SH_DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
//          hasBME280 = false;
//        }
//      }
//    }
  
    if (hasBME680) {
      SH_DEBUG_PRINTLN("Found BME680");
    } else if (hasBME280) {
      SH_DEBUG_PRINTLN("Found BME280");
    }

    //wait a bit before your start
    delay(2000);
    digitalWrite(LED_BUILTIN, LOW);
}

byte packet[10];
char hexbuffer[3];

int loopCycleCount = 0;
long noiseTotal = 0;
int pm10 = 0;
int pm25 = 0;

void loop() {
  if (isOkSetup) {
    //wait
    delay(CYCLE_DELAY);

    //increase counter
    loopCycleCount++;
    
    //measure noise
    int noiseSessionMax = 0;
    int noiseSessionMin = 1024; 
    int currentSample = 0;
    int noiseMeasureLength = millis();
    for (int sample = 0; sample < NUM_NOISE_SAMPLES; sample++) {
      currentSample = analogRead(NOISE_MEASURE_PIN);
      if (currentSample >0 && currentSample < 1020) {
        if (currentSample > noiseSessionMax) {
          noiseSessionMax = currentSample;
        }
        if (currentSample < noiseSessionMin) {
          noiseSessionMin = currentSample;
        }
      }
    }

    int currentSessionNoise = noiseSessionMax - noiseSessionMin;
    if (currentSessionNoise < 0) {
      currentSessionNoise = 0;
      //something bad has happened, but rather send a 0.
    }
    

    //This will be different on Arduino and ESP. Probably need to attenuate the ESP a bit.
    //TODO: see up to which freq. is needed to sample in order to keep into 'noise' band.
    #ifdef NO_CONNECTION_PROFILE
      noiseMeasureLength = millis() - noiseMeasureLength;
      SH_DEBUG_PRINT("Noise measurement took: ");
      SH_DEBUG_PRINT_DEC(noiseMeasureLength, DEC);
      SH_DEBUG_PRINT("ms with ");
      SH_DEBUG_PRINT_DEC(NUM_NOISE_SAMPLES, DEC);
      SH_DEBUG_PRINT(" samples. Minumum = ");
      SH_DEBUG_PRINT_DEC(noiseSessionMin, DEC);
      SH_DEBUG_PRINT(", Maxiumum = ");
      SH_DEBUG_PRINT_DEC(noiseSessionMax, DEC);
      SH_DEBUG_PRINT(" samples. Value = ");
      SH_DEBUG_PRINT_DEC(currentSessionNoise, DEC);
      SH_DEBUG_PRINT(", normalized: ");
      SH_DEBUG_PRINTLN_DEC(currentSessionNoise / 4, DEC);
    #endif
    
    
    noiseTotal += (long)currentSessionNoise;
    
    if (loopCycleCount >= NUM_MEASURE_SESSIONS) {
      //done measuring
      //measure dust, temp, hum and send data.
      int countTempHumReadouts = 10;
      int temp = 0; 
      int humidity = 0;
      int pressure = 0;
      int gasResistance = 0;
      while (--countTempHumReadouts > 0) {
        if (hasBME680) {
          if (! bme680.performReading()) {
            SH_DEBUG_PRINTLN("BME read failure!");
            //return;
          } else {
            temp = bme680.temperature;
            humidity = bme680.humidity;
            pressure = bme680.pressure / 100;
            gasResistance = bme680.gas_resistance;
          }
//        } 
//        else if (hasBME280) {
//          temp = bme280.readTemperature();
//          humidity = bme280.readHumidity();
//          pressure = bme280.readPressure() / 100;
        } else {
          // No temp/hum sensor
          break;
        }
        if (humidity <= 0 || humidity > 100 || temp > 100 || temp < -100 || pressure <= 0) {
          //fake result, pause and try again.
          delay(3000);
        } else {
          // OK result
          break;
        }
      }

      int noise = ((long)noiseTotal / loopCycleCount) / 4; // mapped to 0-255
      
      sdsSerial.listen();
      sdsSensor.set_sleep(false);
      sdsSensor.set_mode(sds011::QUERY);
      //wait just enough for it to get back on its senses
      delay(15000);
      sdsSensor.query_data_auto(&pm25, &pm10, 10);
      delay(100);
      sdsSensor.set_sleep(true);

  
      SH_DEBUG_PRINT("pm25: ");
      SH_DEBUG_PRINT_DEC(pm25, DEC);
      SH_DEBUG_PRINT("ug/m3, pm10: ");
      SH_DEBUG_PRINT_DEC(pm10, DEC);
      SH_DEBUG_PRINT("ug/m3, noise: ");
      SH_DEBUG_PRINT_DEC(noise, DEC);
      SH_DEBUG_PRINT("units, pressure: ");
      SH_DEBUG_PRINT_DEC(pressure, DEC);
      SH_DEBUG_PRINT("hpa, temp: ");
      SH_DEBUG_PRINT_DEC(temp, DEC);
      SH_DEBUG_PRINT(", hum: ");
      SH_DEBUG_PRINTLN_DEC(humidity, DEC);
    
      int hextemp = min(max(temp + 127, 0), 255);
      int hexhum = min(max(humidity, 0), 255);
    
    //    dataframe
    //- 1 byte: version
    //- 1 byte: temperature (offsetted 127)
    //- 1 byte: humidity
    //- 1 byte: noise (scaled from 0 to 255)
    //- 2 bytes: pm10
    //- 2 bytes: pm25
    //- 2 bytes pressure
        
      packet[0]=1; //version to be changed to something else
      packet[1]=(byte)hextemp;
      packet[2]=(byte)hexhum;
      packet[3]=(byte)noise; //noise
      packet[4]=(byte)(pm10 / 256);
      packet[5]=(byte)(pm10 % 256);
      packet[6]=(byte)(pm25 / 256);
      packet[7]=(byte)(pm25 % 256);
      packet[8]=(byte)(pressure / 256);
      packet[9]=(byte)(pressure % 256);

      #ifndef NO_CONNECTION_PROFILE
        digitalWrite(LED_BUILTIN, HIGH);
        SH_DEBUG_PRINTLN("TXing: ");
        for(int i=0; i<10; i++) {
          sprintf(hexbuffer, "%02x", (int)packet[i]);
          SH_DEBUG_PRINT(hexbuffer);
          SH_DEBUG_PRINT(" ");
        }
        SH_DEBUG_PRINTLN("");

        // Send it off
        ttn.sendBytes(packet, sizeof(packet));
        
        digitalWrite(LED_BUILTIN, LOW);
      #endif

      //reset
      noiseTotal = 0;
      //noiseMax = 0;
      loopCycleCount = 0;

      
    }
      
  } else {
    delay(100);
  }

}

void bubbleSort(short A[],int len) {
  unsigned long newn;
  unsigned long n=len;
  short temp=0;
  do {
    newn=1;
    for(int p=1;p<len;p++){
      if(A[p-1]>A[p]){
        temp=A[p];           //swap places in array
        A[p]=A[p-1];
        A[p-1]=temp;
        newn=p;
      } //end if
    } //end for
    n=newn;
  } while(n>1);
}


short median(short sorted[],int m) //calculate the median
{
  //First bubble sort the values: https://en.wikipedia.org/wiki/Bubble_sort
  bubbleSort(sorted,m);  // Sort the values
  if (bitRead(m,0)==1) {  //If the last bit of a number is 1, it's odd. This is equivalent to "TRUE". Also use if m%2!=0.
    return sorted[m/2]; //If the number of data points is odd, return middle number.
  } else {    
    return (sorted[(m/2)-1]+sorted[m/2])/2; //If the number of data points is even, return avg of the middle two numbers.
  }
}



