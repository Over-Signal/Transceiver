/*
 * Gemini CLI - Transceiver Firmware (User Style)
 * Protocol: TDMA with Timestamp Sync
 */
#include <SoftwareSerial.h>

// HW연결
SoftwareSerial myLoRa(2, 3); // RX, TX
#define M0_PIN 7
#define M1_PIN 6

// TDMA 슬롯 설정
const long SLOT_DURATION = 400;
const long TOTAL_NODES = 4;
const long CYCLE_DURATION = SLOT_DURATION * TOTAL_NODES; // 1600 ms

long localOffset = 0; // 동기화용 오프셋
int nodeID = -1;      // 초기값 -1 (PC로부터 할당 대기)

// 챔버(Queue) 설정
char send_q[5][128]; // 128byte * 5
uint8_t send_q_head = 0; // Pop 위치
uint8_t send_q_tail = 0; // Push 위치

// 함수 원형
void receiver();
void chamber();
void trySend();
void command(char com[]);
bool isQueueEmpty();
bool isQueueFull();
uint8_t get_next_pointer(uint8_t pointer);
void processSync(char* packet, unsigned long arrivalTime, char** dataStartPtr);

void setup() {
  Serial.begin(9600);
  myLoRa.begin(9600);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);

  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);
  
  delay(1000); // 모듈 안정화
  
  Serial.setTimeout(50); // PC 입력 대기 타임아웃
  myLoRa.setTimeout(50); // LoRa 수신 타임아웃
  
  randomSeed(analogRead(A0));
}

void loop() {
  receiver(); // RF 수신 & 동기화
  chamber();  // PC 입력 -> 큐 저장
  trySend();  // TDMA 슬롯 체크 -> 큐 송신
}

// ==========================================
// 1. 수신 및 동기화 (RF -> PC)
// ==========================================
void receiver() {
  if (myLoRa.available() > 0) { 
    delay(10); // 데이터 수신 대기
    char buff[128];
    // 라인 단위 수신 (타임아웃 50ms)
    int readLen = myLoRa.readBytesUntil('\n', buff, 127);
    if (readLen <= 0) return;
    buff[readLen] = '\0';

    unsigned long arrivalTime = millis();

    // [RSSI 처리] (유저 로직 유지: 마지막 바이트가 RSSI라고 가정 시)
    // 실제 문자열 패킷이라면 마지막 바이트가 RSSI가 아닐 수 있으니 주의가 필요합니다.
    // 여기서는 안전하게 문자열 파싱 후 별도 RSSI 로직을 적용하거나, 
    // 유저분 코드처럼 마지막 바이트를 RSSI로 간주합니다.
    int rssi_dbm = -50; // 기본값
    // 만약 모듈이 RSSI를 마지막 바이트에 붙여준다면 아래 주석 해제:
    // char raw_rssi = buff[readLen - 1];
    // rssi_dbm = (uint8_t)raw_rssi - 256;
    // buff[readLen - 1] = '\0'; // RSSI 바이트 제거 후 문자열 처리
    
    // [동기화 및 데이터 분리]
    // 패킷 포맷: "Timestamp$ID$Route$Seq$Data"
    char* dataStart = NULL;
    processSync(buff, arrivalTime, &dataStart);

    // [PC 전송] 
    // 포맷: "ID$Route$Seq$Data/RSSI"
    if (dataStart != NULL) {
      Serial.print(dataStart);
      Serial.print('/');
      Serial.println(rssi_dbm);
    }
  }
}

// 타임스탬프 파싱 및 시간 동기화 함수
void processSync(char* packet, unsigned long arrivalTime, char** dataStartPtr) {
  // 첫 번째 '$' 찾기
  char* firstDollar = strchr(packet, '$');
  
  if (firstDollar != NULL) {
    // 1. 타임스탬프 추출
    *firstDollar = '\0'; // 잠시 문자열 분리
    long receivedTime = atol(packet);
    *firstDollar = '$'; // 복구
    
    // 2. 실제 데이터 시작 위치 (Timestamp$ 다음)
    *dataStartPtr = firstDollar + 1;

    // 3. 동기화 로직 (나는 Node 0이 아닐 때만)
    if (nodeID != 0) {
      // 현재 내 기준의 가상 시간
      // long myVirtualTime = (arrivalTime + localOffset) % CYCLE_DURATION;
      
      // 목표: (arrivalTime + localOffset) % 1600 == receivedTime
      // 즉, localOffset = receivedTime - (arrivalTime % 1600)
      long targetOffset = receivedTime - (long)(arrivalTime % CYCLE_DURATION);
      
      // 급격한 변화 방지를 위해 점진적 보정(옵션) 혹은 즉시 적용
      localOffset = targetOffset;
    }
  } else {
    // 형식이 안 맞으면 전체를 데이터로 취급
    *dataStartPtr = packet;
  }
}

// ==========================================
// 2. 큐 관리 및 PC 입력 (PC -> Queue)
// ==========================================
void chamber() {
  if (Serial.available() > 0 && !isQueueFull()) {
    char temp_buffer[128];
    // PC 데이터 읽기
    int readLen = Serial.readBytesUntil('\n', temp_buffer, 127);
    if (readLen <= 0) return;
    temp_buffer[readLen] = '\0';

    // 커맨드 처리 (C$...)
    if (temp_buffer[0] == 'C') {
      command(temp_buffer);
      return;
    }

    // 큐에 저장 (Push)
    // 현재 tail 위치에 복사 후 tail 증가
    strcpy(send_q[send_q_tail], temp_buffer);
    send_q_tail = get_next_pointer(send_q_tail);
  }
}

void command(char com[]) {
  // 예: C$0$1 (커맨드$0$노드ID)
  // strtok는 원본을 자르므로 복사본을 쓰거나 주의해서 사용
  char* token = strtok(com, "$"); // 'C'
  token = strtok(NULL, "$");      // '0' (Type)
  
  if (token != NULL && atoi(token) == 0) {
    token = strtok(NULL, "$"); // NodeID
    if (token != NULL) {
      nodeID = atoi(token);
      // Serial.println("ID SET OK"); // 디버깅용
    }
  }
}

// ==========================================
// 3. 송신 로직 (Queue -> RF)
// ==========================================
void trySend() {
  if (nodeID == -1) return; // ID 설정 전에는 송신 불가
  if (isQueueEmpty()) return;

  unsigned long currentMillis = millis();
  
  // 1. 현재 가상 시간 계산
  long virtualTime;
  if (nodeID == 0) {
    virtualTime = currentMillis % CYCLE_DURATION;
  } else {
    virtualTime = (currentMillis + localOffset) % CYCLE_DURATION;
    if (virtualTime < 0) virtualTime += CYCLE_DURATION;
  }

  // 2. 내 슬롯 확인
  long mySlotStart = nodeID * SLOT_DURATION;
  long mySlotEnd = (nodeID + 1) * SLOT_DURATION;

  if (virtualTime >= mySlotStart && virtualTime < mySlotEnd) {
    // 송신 가능 구간!
    
    // 큐에서 데이터 꺼내기 (Pop)
    char* data = send_q[send_q_head];
    
    // 타임스탬프 붙여서 전송: [TIME]$[DATA]
    String rfPacket = String(virtualTime) + "$" + String(data);
    myLoRa.println(rfPacket);
    
    // 전송 후 처리
    send_q_head = get_next_pointer(send_q_head); // head 이동
    delay(30); // 패킷 간 충돌 방지 및 슬롯 내 과다 전송 방지 딜레이
  }
}

// ==========================================
// 유틸리티 함수
// ==========================================
uint8_t get_next_pointer(uint8_t pointer) {
  return (pointer + 1) % 5;
}

bool isQueueEmpty() {
  return send_q_head == send_q_tail;
}

bool isQueueFull() {
  return get_next_pointer(send_q_tail) == send_q_head;
}
