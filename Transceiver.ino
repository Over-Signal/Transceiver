#include <SoftwareSerial.h>


// RX: 2번, TX: 3번 (E22의 TX, RX와 교차 연결)
SoftwareSerial myLoRa(2, 3); 
#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 4


#define USE_TDMA 0

#define GUARD_TIME 100 //ms, for TDMA

#define THIS_NOD_NUM 1 //for TDMA

String ammo = "";
bool coked = false;
unsigned long backOffEndTime = 0; //for CSMA

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

void receiver() 
{
    if(myLoRa.available() > 0){ 
    delay(10); // 데이터가 전송되는 동안 살짝 기다림 (안정성)
    String buff = myLoRa.readString();
    
    for(int i=0; i < buff.length() - 1; i++){
      Serial.write(buff[i]); // PC로 한 글자씩 보냄
    }

    char raw_rssi = buff.charAt(buff.length() - 1);

    int rssi_dbm = (uint8_t)raw_rssi - 256;
    Serial.print('/');
    Serial.println(rssi_dbm);
  }
}

void chamber() 
{
  if(Serial.available()>0 && coked == false){
    delay(10); //안정성
    ammo = Serial.readStringUntil('\n');

    if(ammo.length()>0) {
      coked = true;
      Serial.print("[SYSTEM]coked, sucess");
    }
  }
}

void runTDMA() {
  
  receiver();
  chamber();

  //3. 송신-----------------------------------
  //장전이 되어있다면, 타임슬롯을 생성 지금 타임 슬롯을 확인하여 
  //자신이 지금 말해도 되는 타임 슬롯이라면 송신!
  if(coked == true){
    
    unsigned long currentTime = millis(); //디버그용 사실 쓸모는 없음
    
    int timeSlot = (millis()/1000)%4+1  //1~4 4개의 1초 짜리 타임 슬롯
    int timeInSlot = (millis()%1000); //슬롯 내부 시간

    if( (tiemSlot == THIS_NOD_NUM) && timeInSlot >= GUARD_TIME ) { //슬롯 내부에서 타임가드 100ms 이후에 출발

      myLoRa.print(ammo);
      coked = false;
      ammo = "";
      Serial.print("[SYSTEM]send success, currentMillis : ");
      Serial.print(currentTime);      
    }
  }
}

void runCSMA() {
  
  unsigned long currentMillis = millis();

  receiver();
  chamber();

  //3. 송신--------------------------------------------------
  if((coked==true) && (currentMillis >= backOffEndTime))
  {
      if(digitalRead(AUX_PIN) == HIGH) { //AUX핀이 HIGH면 사용가능
        myLoRa.print(ammo);
        coked = false;
        ammo = ""
        Serial.print("[SYSTEM]send success, currentMillis : ");
        Serial.print(currentMillis);
      }
      else //AUX핀이 LOW, 사용중인 경우
      {
        backOffEndTime = random(100,1000) + currentMillis; //백오프 설정

        Serial.print("[SYSTEM]Channel is Full, BackoffTIme : ");
        Serial.print(backOffEndTime);
      }
    }
}

void loop() {

  if(USE_TDMA){
    runTDMA();
  }
  else{
    runCSMA();
  }
}