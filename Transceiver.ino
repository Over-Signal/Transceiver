#include <SoftwareSerial.h>
#include <RTClib.h>

// RX: 2번, TX: 3번 (E22의 TX, RX와 교차 연결)
#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 5

#define SQW_PIN 2

SoftwareSerial myLoRa(3, 4); 
RTC_DS3231 rtc;

char send_q[5][128]; //128*5 = 640byte
uint8_t send_q_head_pointer = 0; //1byte
uint8_t send_q_tail_pointer = 0;

bool coked = false;
unsigned long backOffEndTime = 0; //for CSMA 4byte

volatile unsigned long lastSqwMillis = 0;
volatile bool readBlock = false;

uint8_t nodeID = 0;

void receiver();
void chamber();
void trySend();
bool isQueueEmpty();
bool isQueueFull();
void command(char com[]);
uint8_t get_pointer(uint8_t* pointer);

void setup() {
  Serial.begin(9600);
  myLoRa.begin(9600);
  rtc.begin();

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);
  pinMode(SQW_PIN, INPUT_PULLUP);

  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);
  
  delay(1000); // 모듈 안정화 대기
  Serial.setTimeout(50);//입력버퍼 대기 default 500ms
  myLoRa.setTimeout(50);//LoRa버퍼 50ms
  rtc.writeSqwPinMode(DS3231_SquareWave1Hz);

  attachInterrupt(digitalPinToInterrupt(SQW_PIN), sqw_down, FALLING);
}

void loop() {
  receiver();
  chamber();
  trySend();
}

void sqw_down(){
  lastSqwMillis = millis();
  readBlock = true;
}

void command(char com[]){
  char perfix_command, data, buff[15];
  int id_command;

  perfix_command = *strtok(com, "$");
  id_command = atoi(strtok(NULL, "$"));

  switch(id_command){
    case 0:
      //node id
      nodeID = atoi(strtok(NULL, "$"));
      // Serial.println(nodeID);
      break;
  }
}

void receiver()
{
    if(myLoRa.available() > 0){ 
    delay(10); // 데이터가 전송되는 동안 살짝 기다림 (안정성)
    char buff[128];
    uint8_t readbyte = myLoRa.readBytesUntil('\n', buff, 127);
    buff[readbyte] = '\0';
    
    for(int i=0; i < strlen(buff) - 1; i++){
      Serial.write(buff[i]); // PC로 한 글자씩 보냄
    }

    char raw_rssi = buff[readbyte - 1];
    int rssi_dbm = (uint8_t)raw_rssi - 256;
    Serial.print('/');
    Serial.println(rssi_dbm);
  }
}

bool isQueueEmpty() {
  return send_q_head_pointer == send_q_tail_pointer;
}

bool isQueueFull() {
  return ((send_q_tail_pointer + 1) % 5) == send_q_head_pointer;
}

void chamber() 
{
  //unsigned long currentMillis = millis();
  char temp_buffer[128];

  if(Serial.available()>0 && !isQueueFull()){
    delay(10); //안정성
    //send_q[send_q_pointer] = Serial.readStringUntil('\n');

    uint8_t readbyte = Serial.readBytesUntil('\n', temp_buffer, 127);
    temp_buffer[readbyte] = '\0';

    if (temp_buffer[0] == 'C'){
      command(temp_buffer);
      return;
    }
    if(readbyte > 0) {
      uint8_t tailPointer = get_pointer(&send_q_tail_pointer);
      strcpy(send_q[tailPointer], temp_buffer);
      coked = true;
    }
  }
}

void trySend() {
  unsigned long current_ms_slot = millis() - lastSqwMillis;

  uint16_t slot_start = nodeID * 250;
  uint16_t slot_end = slot_start + 250;

  if (current_ms_slot > slot_start && current_ms_slot < slot_end && readBlock == true && coked == true){
    myLoRa.print(send_q[get_pointer(&send_q_head_pointer)]);
    readBlock = false;
    if(isQueueEmpty()){
      coked = false;
    }
  }
}

uint8_t get_pointer(uint8_t* pointer){
  uint8_t returnValue = *pointer;
  *pointer = (*pointer + 1) % 5;
  return returnValue;
}