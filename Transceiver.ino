#include <SoftwareSerial.h>

// RX: 2번, TX: 3번 (E22의 TX, RX와 교차 연결)
SoftwareSerial myLoRa(2, 3); 
#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 4

// ================= [TDMA 설정] =================
#define TOTAL_NODES 4           // 전체 노드 수
#define SLOT_TIME 3000          // 각 노드당 할당 시간 (ms)
#define GUARD_TIME 500          // 앞뒤 여유 시간 (ms)
#define CYCLE_TIME (TOTAL_NODES * SLOT_TIME) // 전체 주기 (12초)

unsigned long lastSyncTime = 0; // 마지막으로 동기화된 시간
// ===============================================

char send_q[5][128]; //128*5 = 640byte
uint8_t send_q_head_pointer = 0; //1byte
uint8_t send_q_tail_pointer = 0;

bool coked = false;
unsigned long backOffEndTime = 0; // CSMA용 변수 남겨둠 (필요시 사용)

uint8_t nodeID = 0; 

void receiver();
void chamber();
void trySend();
void command(char com[]);
uint8_t get_pointer(uint8_t* pointer);
void sendSyncPacket();

void setup() {
  Serial.begin(9600);
  myLoRa.begin(9600);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);

  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);
  
  delay(1000); 
  Serial.setTimeout(50);
  myLoRa.setTimeout(50);
  
  randomSeed(analogRead(A0));

  // [TEST] 시스템 시작 메시지 활성화
  Serial.println(); 
  Serial.println(F("[SYSTEM] Arduino LoRa Node Started"));
  Serial.println(F("[SYSTEM] Mode: TDMA (Sync Relay)"));
  Serial.println(F("[SYSTEM] Waiting for input..."));
}

void loop() {
  // Node 0번은 주기적으로 SYNC 패킷 방송
  if (nodeID == 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastSyncTime >= CYCLE_TIME) {
      sendSyncPacket();
      lastSyncTime = currentMillis; 
    }
  }

  receiver();
  chamber();
  trySend();
}

void command(char com[]){
  char perfix_command;
  int id_command;

  perfix_command = *strtok(com, "$"); 
  id_command = atoi(strtok(NULL, "$"));

  switch(id_command){
    case 0:
      nodeID = atoi(strtok(NULL, "$"));
      // [TEST] 노드 ID 설정 확인
      Serial.print("[SYSTEM] Set Node ID: ");
      Serial.println(nodeID);
      break;
  }
}

void receiver()
{
  if(myLoRa.available() > 0){ 
    delay(10); 
    char buff[128];
    uint8_t readbyte = myLoRa.readBytesUntil('\n', buff, 127);
    buff[readbyte] = '\0';
    
    // SYNC 패킷 확인
    if (strncmp(buff, "SYNC", 4) == 0) {
      if (nodeID != 0) { 
        lastSyncTime = millis(); 
        // [TEST] 동기화 수신 확인
        Serial.println("[SYSTEM] Synced with Master");
      }
      return; 
    }

    // [TEST] 수신 성공 메시지
    Serial.print("[SYSTEM] get, success: ");
    
    // 데이터 PC로 전송
    for(int i=0; i < readbyte; i++){ 
      Serial.write(buff[i]); 
    }
    Serial.println(); // 줄바꿈

    // RSSI 출력 (기존 로직)
    /*
    char raw_rssi = buff[readbyte - 1]; 
    int rssi_dbm = (uint8_t)raw_rssi - 256;
    Serial.print('/');
    Serial.println(rssi_dbm);
    */
  }
}

void sendSyncPacket() {
  if(digitalRead(AUX_PIN) == HIGH) {
    myLoRa.println("SYNC"); 
    // [TEST] 마스터 동기화 패킷 전송 확인
    Serial.println("[SYSTEM] Master sent SYNC");
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
  char temp_buffer[128];

  if(Serial.available()>0 && !isQueueFull()){
    delay(10); 
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
      
      // [TEST] 큐 적재 확인
      Serial.print("[COKED] Queue Added. Head: ");
      Serial.print(send_q_head_pointer);
      Serial.print(" Tail: ");
      Serial.println(send_q_tail_pointer);
    }
  }
}

void trySend()
{
  if(isQueueEmpty()) { return; }
  
  unsigned long currentMillis = millis();

  // 동기화 대기 (Node 0 제외)
  if (nodeID != 0 && lastSyncTime == 0) {
     // [TEST] 동기화 대기 중 알림 (너무 자주 뜨면 주석 처리)
     // Serial.println("[WAIT] Waiting for SYNC..."); 
     return; 
  }

  unsigned long timeSinceSync = currentMillis - lastSyncTime;
  unsigned long timeInCycle = timeSinceSync % CYCLE_TIME; 
  
  unsigned long mySlotStart = nodeID * SLOT_TIME; 
  unsigned long mySlotEnd = (nodeID + 1) * SLOT_TIME;

  // 내 슬롯 + 가드타임 체크
  if (timeInCycle > (mySlotStart + GUARD_TIME) && 
      timeInCycle < (mySlotEnd - GUARD_TIME)) {
      
      if(digitalRead(AUX_PIN) == HIGH) { 
        uint8_t headPointer = get_pointer(&send_q_head_pointer);
        myLoRa.println(send_q[headPointer]); 
        coked = false;
        
        // [TEST] 전송 성공 확인
        Serial.print("[SEND_SUCCESS] Time: ");
        Serial.println(currentMillis);
      }
  }
}

uint8_t get_pointer(uint8_t* pointer){
  uint8_t returnValue = *pointer;
  *pointer = (*pointer + 1) % 5;
  return returnValue;
}