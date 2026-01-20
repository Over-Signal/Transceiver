#include <SoftwareSerial.h>

// ===== 핀 설정 =====
SoftwareSerial myLoRa(2, 3); // RX, TX
#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 4

// ===== 노드 설정 =====
#define NODE_ID 1          // 노드마다 다르게
#define MAX_RETRY 5
#define ACK_TIMEOUT 200    // ms

// ===== 상태 변수 =====
String ammo = "";
bool coked = false;

int seqNum = 0;
int retryCount = 0;
unsigned long backOffEndTime = 0;

void setup() {
  Serial.begin(9600);
  myLoRa.begin(9600);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);

  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);

  randomSeed(analogRead(A0) + millis());

  Serial.println("[SYSTEM] CSMA + EXP BACKOFF + ACK START");
}

// ================= 수신 =================
void receiver() {
  if (myLoRa.available()) {
    delay(10);
    String pkt = myLoRa.readString();

    // ACK 수신
    if (pkt.startsWith("ACK")) {
      Serial.println("[SYSTEM] ACK RECEIVED");
      coked = false;
      retryCount = 0;
      ammo = "";
      return;
    }

    // 데이터 수신
    Serial.print("[RECV] ");
    Serial.println(pkt);

    // ACK 응답
    if (pkt.indexOf("SEQ=") != -1) {
      String ack = "ACK:NODE=" + String(NODE_ID);
      myLoRa.print(ack);
    }
  }
}

// ================= 입력 =================
void chamber() {
  if (Serial.available() && !coked) {
    ammo = Serial.readStringUntil('\n');
    if (ammo.length() > 0) {
      coked = true;
      seqNum++;
      retryCount = 0;
      Serial.println("[SYSTEM] CHAMBERED");
    }
  }
}

// ================= 송신 =================
void trySend() {
  if (!coked) return;
  if (millis() < backOffEndTime) return;

  if (digitalRead(AUX_PIN) == LOW) {
    setBackoff();
    return;
  }

  String packet = "NODE=" + String(NODE_ID) +
                  ",SEQ=" + String(seqNum) +
                  ",DATA=" + ammo;

  myLoRa.print(packet);
  Serial.println("[SEND] " + packet);

  // ===== ACK 대기 =====
  unsigned long waitStart = millis();
  while (millis() - waitStart < ACK_TIMEOUT) {
    receiver();
  }

  // ACK 실패 → 백오프
  retryCount++;
  if (retryCount >= MAX_RETRY) {
    Serial.println("[SYSTEM] DROP PACKET");
    coked = false;
    retryCount = 0;
    return;
  }

  setBackoff();
}

// ================= 지수 백오프 =================
void setBackoff() {
  unsigned long delayTime =
    random((1 << retryCount) * 100,
           (1 << retryCount) * 300);

  backOffEndTime = millis() + delayTime;

  Serial.print("[BACKOFF] retry=");
  Serial.print(retryCount);
  Serial.print(" wait=");
  Serial.println(delayTime);
}

// ================= 메인 루프 =================
void loop() {
  receiver();
  chamber();
  trySend();
}
