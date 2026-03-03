#include <SoftwareSerial.h>
#include <RTClib.h>

// RX: 2번, TX: 3번 (E22의 TX, RX와 교차 연결)

//PIN
#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 5
#define SQW_PIN 2
#define LED_PIN 13

//tdma properties
#define SLOT_SIZE_MS 250
#define GUARD_TIME_MS 25

//sync properties
#define RF_PROP_DELAY_US 33
#define EMA_ALPHA_X100 20
#define OFFSET_APPLY_THR 3
#define RESYNC_INTERVAL 15 //초
#define REQ_RETRY_INTERVAL_MS 3000 
#define REQ_RETRY_MAX 20 

//packet type
#define SYNC_PACKET_ID 'S'
#define CMD_PACKET_ID 'C'
#define REQ_PACKET_ID 'R'

SoftwareSerial myLoRa(3, 4); 
RTC_DS3231 rtc;

//for master
bool isMaster = false;
unsigned long lastResyncSec = 0;

//for slave
bool  isSynced = false;
long  smoothedOffset = 0;
long  lastRawOffset = 0;
long  syncOffset = 0; 


//buffer
char send_q[5][128]; //128*5 = 640byte
uint8_t send_q_head_pointer = 0; //1byte
uint8_t send_q_tail_pointer = 0;

//led properties
#define LED_FLASH_MS 30
#define LED_SEARCH_PERIOD 200
unsigned long ledOffTime = 0;
bool ledFlashActive = false;

//slave sync req
unsigned long lastReqMillis = 0;   // 마지막 REQ 전송 시각
uint8_t reqRetryCount = 0;   // 재시도 횟수
bool reqGiveUp = false;

bool coked = false;
unsigned long backOffEndTime = 0; //for CSMA 4byte

volatile unsigned long lastSqwMillis = 0;
volatile bool sqwFlag = false;

uint8_t nodeID = 0;

void handleSqw();
void receiver();
void chamber();
void trySend();
bool isQueueEmpty();
bool isQueueFull();
void command(char com[], unsigned long rxMillis = 0);
bool waitAUX(uint16_t timeoutMs);
uint16_t getElapsed();
uint8_t get_pointer(uint8_t* pointer);

inline bool isReady() {return isMaster ? true : isSynced;}

void sqw_down(){
  lastSqwMillis = millis();
  sqwFlag = true;
  if (isReady()) digitalWrite(LED_PIN, HIGH);
}

void setup() {
  Serial.begin(9600);
  myLoRa.begin(9600);
  rtc.begin();

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);
  pinMode(SQW_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  delay(1000); // 모듈 안정화 대기
  Serial.setTimeout(50);//입력버퍼 대기 default 500ms
  myLoRa.setTimeout(50);//LoRa버퍼 50ms
  rtc.writeSqwPinMode(DS3231_SquareWave1Hz);

  attachInterrupt(digitalPinToInterrupt(SQW_PIN), sqw_down, FALLING);
}

void loop() {
  if (sqwFlag) {
    sqwFlag = false;
    handleSqw();
  }
  updateLED();
  receiver();
  chamber();
  //trySend();
   if (!isMaster && !isSynced) trySyncRequest();  // 미동기화 슬레이브: 재요청
  if (isReady()) trySend();
}

void trySyncRequest() {
  if (reqGiveUp) return;  // 최대 재시도 초과 → 브로드캐스트 대기 중

  if (millis() - lastReqMillis < REQ_RETRY_INTERVAL_MS) return;

  if (reqRetryCount >= REQ_RETRY_MAX) {
    reqGiveUp = true;
    return;
  }

  sendSyncRequest();
}

void sendSyncRequest() {
  if (!waitAUX(100)) return;
  char buf[16];
  snprintf(buf, sizeof(buf), "%c,0,%u\n", REQ_PACKET_ID, nodeID);
  myLoRa.print(buf);
  lastReqMillis = millis();
  reqRetryCount++;
}

void handleSqw() {
  ledFlashActive = true;
  ledOffTime     = millis() + LED_FLASH_MS;

  if (isMaster) {
    // 마스터: 재동기화 간격마다 브로드캐스트
    unsigned long nowSec = millis() / 1000;
    if (lastResyncSec == 0 || (nowSec - lastResyncSec) >= RESYNC_INTERVAL) {
      sendSyncPacket();
      lastResyncSec = nowSec;
    }
  }
  // 슬레이브: SQW 시 별도 동작 없음
}

void sendSyncPacket() {
  if (!waitAUX(100)) {
    return;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "C$%c$%u$%u",SYNC_PACKET_ID, getElapsed(), nodeID);
  myLoRa.println(buf);
}

void command(char com[], unsigned long rxMillis = 0){
  char perfix_command, data, buff[15];
  int id_command;

  perfix_command = *strtok(com, "$");
  id_command = *strtok(NULL, "$");

  switch(id_command){
    case '0':{
      //node id
      nodeID = atoi(strtok(NULL, "$"));
      if (nodeID == 0){
        isMaster = true;
        //Serial.println("true");
      }
      // Serial.println(nodeID);
      break;
    }
  
    case SYNC_PACKET_ID:{
      //Serial.println("true S");
      uint8_t packetElapsed = (uint8_t)atoi(strtok(NULL, "$"));
      uint8_t senderID = (uint8_t)atoi(strtok(NULL, "$"));
      
      if (!isMaster && senderID != nodeID) //추후 조건검증 변경 필요 / 원 구문 :if (!isMaster() && senderID == MASTER_ID)
        applySyncPacket(rxMillis, packetElapsed);
      break;
    }

    case REQ_PACKET_ID:{
      //sSerial.println("true R");
       if (isMaster) {
        sendSyncPacket();
      }
      break;

    }
  }
}

void applySyncPacket(unsigned long rxMillis, uint16_t masterElapsed) {
  unsigned long estimatedMasterSqw =
    rxMillis - (unsigned long)masterElapsed - (RF_PROP_DELAY_US / 1000UL);

  // syncOffset을 적용한 슬레이브 기준점과 마스터 기준점의 차이를 계산
  long currentBase = (long)lastSqwMillis + syncOffset;
  long offset = (long)estimatedMasterSqw - currentBase;

  if (offset >  500) offset -= 1000;
  if (offset < -500) offset += 1000;

  lastRawOffset = offset;

  if (abs(offset) >= OFFSET_APPLY_THR) {
    syncOffset    += offset;  // lastSqwMillis 대신 syncOffset에 누적
    smoothedOffset = 0;
  }

  bool wasUnsynced = !isSynced;
  if (wasUnsynced) syncOffset = offset;  // 첫 동기화: 누적 없이 직접 설정
  isSynced       = true;
  reqRetryCount  = 0;
  reqGiveUp      = false;
}


void receiver() {
  if (myLoRa.available() <= 0) return;

  unsigned long rxMillis = millis();
  //delay(10); // 데이터가 전송되는 동안 살짝 기다림 (안정성)
  char buff[128];
  uint8_t readbyte = myLoRa.readBytesUntil('\n', buff, 127);
  buff[readbyte] = '\0';

  if (buff[0] != '1'){
      command(buff, rxMillis);
      return;
  }

  for(int i=0; i < strlen(buff) - 1; i++){
    Serial.write(buff[i]); // PC로 한 글자씩 보냄
  }

  char raw_rssi = buff[readbyte - 1];
  int rssi_dbm = (uint8_t)raw_rssi - 256;
  Serial.print('/');
  Serial.println(rssi_dbm);
  
}

void updateLED() {
  // 동기화 전 슬레이브: 빠른 점멸
  if (!isMaster && !isSynced) {
    bool s = ((millis() / (LED_SEARCH_PERIOD / 2)) % 2) == 0;
    if (s != (bool)digitalRead(LED_PIN)) digitalWrite(LED_PIN, s);
    return;
  }

  uint16_t elapsed   = getElapsed();
  uint16_t slotStart = (uint16_t)nodeID * SLOT_SIZE_MS + GUARD_TIME_MS;
  uint16_t slotEnd   = ((uint16_t)nodeID + 1) * SLOT_SIZE_MS - GUARD_TIME_MS;
  bool     inTxSlot  = (elapsed >= slotStart && elapsed < slotEnd);

  if (ledFlashActive) {
    if (millis() >= ledOffTime) {
      ledFlashActive = false;
      digitalWrite(LED_PIN, inTxSlot ? HIGH : LOW);
    }
  } else {
    digitalWrite(LED_PIN, inTxSlot ? HIGH : LOW);
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

  if (current_ms_slot > slot_start && current_ms_slot < slot_end && sqwFlag == true && coked == true){
    myLoRa.print(send_q[get_pointer(&send_q_head_pointer)]);
    sqwFlag = false;
    if(isQueueEmpty()){
      coked = false;
    }
  }
}

bool waitAUX(uint16_t timeoutMs) {
  unsigned long start = millis();
  while (digitalRead(AUX_PIN) == LOW)
    if (millis() - start > timeoutMs) return false;
  return true;
}

uint16_t getElapsed() {
  long e = (long)(millis() - lastSqwMillis) + syncOffset;
  e = e % 1000;
  if (e < 0) e += 1000;
  return (uint16_t)e;
}

uint8_t get_pointer(uint8_t* pointer){
  uint8_t returnValue = *pointer;
  *pointer = (*pointer + 1) % 5;
  return returnValue;
}