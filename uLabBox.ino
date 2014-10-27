#include <DallasTemperature.h>
#include <OneWire.h>
#include <ChibiOS_AVR.h>

const uint8_t LED_PIN = 13;
const uint8_t TEMP_PIN = 2;

OneWire ds18b20(TEMP_PIN);
DallasTemperature sensors(&ds18b20);

#define SERIAL_BUF_SIZE 20
const size_t POOL_SIZE = 10;
struct PoolObject_t {
  int id;
  char msg[SERIAL_BUF_SIZE];
};
PoolObject_t PoolObjects[POOL_SIZE];
MEMORYPOOL_DECL(memPool, POOL_SIZE, 0);
msg_t letter[POOL_SIZE];
MAILBOX_DECL(mail, &letter, POOL_SIZE);

volatile uint32_t count = 0;
volatile uint32_t temp_delay = 2000;
volatile uint32_t analog_delay;

bool checkNumbers(char input[], int count){
  for (int i=0;i<count;i++){
    if (input[i] == '\0') return true;
    if (input[i] > '9' || input[i] < '0') return false;
  }
  return true;
}

//------------------------------------------------------------------------------
// thread 1 - high priority for blinking LED
// 64 byte stack beyond task switch and interrupt needs
static WORKING_AREA(waThread1, 64);

static msg_t Thread1(void *arg) {
  pinMode(LED_PIN, OUTPUT);
  
  // Flash led every 500 ms.
  while (1) {
    // Turn LED on.
    digitalWrite(LED_PIN, HIGH);
    
    // Sleep for 50 milliseconds.
    chThdSleepMilliseconds(50);
    
    // Turn LED off.
    digitalWrite(LED_PIN, LOW);
    
    // Sleep for 150 milliseconds.
    chThdSleepMilliseconds(450);
  }
  return 0;
}
//------------------------------------------------------------------------------
// thread 2 - print main thread count every second
// 100 byte stack beyond task switch and interrupt needs
static WORKING_AREA(waThread2, 100);
static msg_t ThreadSerialIO(void *arg) {
  
  char buf[100];
  for(int i=0;i<100;i++) buf[i]='.';
  int ptr = 0;
  
  // print count every second
  while (1) {
    
    if(!Serial){
      while (!Serial) {
        chThdSleepMilliseconds(10);
      }
      // read any input
      chThdSleepMilliseconds(200);
      while (Serial.read() >= 0) {}
      chThdSleepMilliseconds(500);
      Serial.println("Serial connected!");
    }else{
      if (Serial.available()){
        char c = Serial.read();
        
        if (c == '\r'){
          buf[ptr++] = '\0'; 

          PoolObject_t* p = (PoolObject_t*)chPoolAlloc(&memPool);
          if (!p) {
              Serial.println("Pool alloc failed");
              while(1);
          }
          
          for(int i=0;i<ptr;i++) p->msg[i]=buf[i];
          
          msg_t s = chMBPost(&mail, (msg_t)p, TIME_IMMEDIATE);
          if (s!=RDY_OK){
              Serial.println("MB post failed!");
              while(1);
          }
          
          ptr = 0;
        }else{
          buf[ptr++] = c;   
        }

      }else{
        chThdSleep(100);
      }
      chThdYield();
    }
  }
}

static WORKING_AREA(waThreadInput, 128);
static msg_t ThreadInput(void *arg){
  while(1){
    PoolObject_t *p;
    
    msg_t s = chMBFetch(&mail, (msg_t*)&p, TIME_IMMEDIATE);
    
    if (s != RDY_OK){
      chThdSleep(10);
    }else{
      char input[SERIAL_BUF_SIZE];
      for (int i=0;i<SERIAL_BUF_SIZE;i++) input[i] = p->msg[i];
      chPoolFree(&memPool, p);

      switch(input[0]) {
        case 'd':
        case 'D':{
            if (!(checkNumbers(input+1,2) && checkNumbers(input+3,1))){
              Serial.print("err ");
              Serial.println(input);
            }else{
              char pin[2];
              pin[0] = input[1];
              pin[1] = input[2];
              int pinNr = atoi(pin);
              if (input[3] == '0'){
                digitalWrite(pinNr, LOW);
              }else{
                digitalWrite(pinNr, HIGH);
              }
              Serial.print("ack ");
              Serial.println(input);
            }
            break;
          }
        case 't':
        case 'T':
          if (!checkNumbers(input+1,18)){
            Serial.print("err ");
            Serial.println(input);
          }else{
            temp_delay = atoi(input+1);
            Serial.print("ack ");
            Serial.println(input);   
          }     
          break;
        case 'a':
        case 'A':
          if (!checkNumbers(input+1,18)){
            Serial.print("err ");
            Serial.println(input);
          }else{
            analog_delay = atoi(input+1);
            Serial.print("ack ");
            Serial.println(input);   
         }     
          break;
        default:
          Serial.print("MB>");
          Serial.println(input);
        }
    }
    chThdYield();
  }
}

static WORKING_AREA(waThreadAnalog, 128);
static msg_t ThreadAnalog(void *arg){
  while(1){
    if (analog_delay > 0){
       for (int i=0;i<6;i++){
         Serial.print("a:");
         Serial.print(i);
         Serial.print(":");
         Serial.println(analogRead(i));  
       }
     }
    chThdSleep(analog_delay ? analog_delay : 100); 
  }
}

static WORKING_AREA(waThreadTemperature, 128);
static msg_t ThreadTemperature(void *arg){
  sensors.begin();
  
  while(1){
    while (temp_delay == 0) chThdSleep(100);
    sensors.requestTemperatures();
    double temperature = sensors.getTempCByIndex(0);
    
    #define CHARS 6
    char buf[CHARS+1];
    dtostrf(temperature,CHARS,2,buf);
    
    Serial.print("Temp: ");
    for(int i=0;i<CHARS;i++){
      if (buf[i] == ' ') continue;
      Serial.print(buf[i]);
    }
    Serial.println();
    chThdSleepMilliseconds(temp_delay);
  }
}

//------------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  pinMode(3,OUTPUT);
  pinMode(4,OUTPUT);
  pinMode(5,OUTPUT);

  chBegin(mainThread);
  // chBegin never returns, main thread continues with mainThread()
  while(1) {}
}
//------------------------------------------------------------------------------
// main thread runs at NORMALPRIO
void mainThread() {
  
  for (int i=0; i<POOL_SIZE; i++){
    chPoolFree(&memPool, &PoolObjects[i]);
  }

  // start blink thread
  chThdCreateStatic(waThread1, sizeof(waThread1),
                          NORMALPRIO + 3, Thread1, NULL);

  // start print thread
  chThdCreateStatic(waThread2, sizeof(waThread2),
                          NORMALPRIO + 2, ThreadSerialIO, NULL);
                          
  chThdCreateStatic(waThreadTemperature, sizeof(waThreadTemperature),
                          NORMALPRIO + 1, ThreadTemperature, NULL);
  chThdCreateStatic(waThreadAnalog, sizeof(waThreadAnalog),
                          NORMALPRIO + 1, ThreadAnalog, NULL);
                          
                          
  chThdCreateStatic(waThreadInput, sizeof(waThreadInput),
                          NORMALPRIO + 1, ThreadInput, NULL);

  // increment counter
  while (1) {
    // must insure increment is atomic in case of context switch for print
    // should use mutex for longer critical sections
    noInterrupts();
    count++;
    interrupts();
  }
}
//------------------------------------------------------------------------------
void loop() {
 // not used
}
