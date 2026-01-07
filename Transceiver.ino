#include <SoftwareSerial.h>

// RX: 2번, TX: 3번 (E22의 TX, RX와 교차 연결)
SoftwareSerial myLoRa(2, 3); 

#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 4


string Ammo = "";
bool Caulked = false;
unsigned long BackOffTime = 0;

void setup() {
  Serial.begin(9600);
  myLoRa.begin(9600);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);

  digitalWrite(M0_PIN, LOW);
  digitalWrite(M1_PIN, LOW);
  
  //Serial.println("LoRa Sender Ready!");
  delay(1000); // 모듈 안정화 대기

  Serial.setTimeout(50);//입력버퍼 대기 default 500ms
  randomSeed(analogRead(A0));
}

void runTDMA() {
  //나중에
}

void runCSMA() {

  // 1. PC에서 받은 데이터가 있는지 확인/ 탄창확인
  // 2. 있다면 -> AUX 핀(채널)이 비었는지 확인/ 약실확인 
  // 3. 비었으면 전송, 쓰고 있으면 대기(Random Backoff) /발사, 대기
  // 4. LoRa에서 데이터가 오면 -> PC로 전달/ 장전

  
  //1. 장전
  if(Serial.available()>0 && Caulked == false){
    Ammo = Serial.readStringUntil('\n');

    if(Ammo.length>0) {
      Caulked = true;
      BackOffTime = 0;
    }
  }
  //코킹된 경우
  if(Caulked==true)
  {
    if(milis() >= ReloadTime) {
      if(digitalRead(AUX_PIN) == HIGH) { //HIGH면 사용가능
        myLoRa.print(Ammo);
        Caulked = false;
        Ammo = ""
      }
    }
    else
    {
      //RandomBackOff, 대기 to be continued...
    }
  }

//수신
  if(myLoRa.available() > 0){ 
    delay(10); // 데이터가 전송되는 동안 살짝 기다림 (안정성)
    
    dataLength = myLoRa.available();
    //Serial.println(dataLength);
    
    String buff = myLoRa.readString();
    //myLoRa.readbytes(buff, dataLength);

    // 받은 데이터 출력 (마지막 1바이트는 RSSI값이므로 제외하고 출력)
    for(int i=0; i < dataLength - 1; i++){
      Serial.write(buff[i]); // PC로 한 글자씩 보냄
    }
    char raw_rssi = buff.charAt(buff.length() - 1);
    
    // 계산식: 입력값 - 256 = 실제 dBm 값
    // 예: 171 - 256 = -85 dBm
    int rssi_dbm = (uint8_t)raw_rssi - 256;
    
    Serial.print('/');
    Serial.println(rssi_dbm);
  }
}

void loop() {

  if(useTDMA){
    runTDMA();
  }
  else{
    runCSMA();
  }
}