#include <SoftwareSerial.h>

// RX: 2번, TX: 3번 (E22의 TX, RX와 교차 연결)
SoftwareSerial myLoRa(2, 3); 

#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 4

void setup() {
  Serial.begin(9600);
  myLoRa.begin(9600);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);

  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);
  
  Serial.println("LoRa Sender Ready!");
  delay(1000); // 모듈 안정화 대기

  Serial.timeout(500);//입력버퍼 대기 default 500ms
}

void loop() {
  String sendMsg, receivedMsg;
  int dataLength;
  
  delay(500);//TDMA 구현 전까진 송 수신 delay 0.5s

  if(myLoRa.available() > 0){ //수신된 데이터가 있을 때
    delay(10);
    dataLength = myLoRa.available();
    Serial.println(dataLength);
    uint8_t buff[dataLength];

    for(int i = 0; i < dataLength; i++){
      buff[i] = myLoRa.read();
    }
    //myLoRa.readbytes(buff, dataLength);
    
    Serial.print("Received: ");
    for(int i=0; i < dataLength-1; i++){
      Serial.write(buff[i]);
    }
    uint8_t raw_rssi = buff[dataLength - 1];
    
    // 계산식: 입력값 - 256 = dBm
    int rssi_dbm = (int)raw_rssi - 256;

    Serial.print("  |  [RSSI] ");
    Serial.print(rssi_dbm);
    Serial.println(" dBm");
  }

  if(Serial.available()>0){//사용자가 입력한 데이터가 있을 때
    Serial.print("Sending: ");
    receivedMsg = Serial.readStringUntil('\n');
    myLoRa.print(receivedMsg);
  }
}