#include <SoftwareSerial.h>

// RX: 2번, TX: 3번 (E22의 TX, RX와 교차 연결)
SoftwareSerial myLoRa(2, 3); 
#define M0_PIN 7
#define M1_PIN 6
#define AUX_PIN 4


String ammo = "";
bool coked = false;
unsigned long backOffEndTime = 0; //for CSMA

int nodeID = 0;

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
  myLoRa.setTimeout(50);//LoRa버퍼 50ms
  
  randomSeed(analogRead(A0));

  // [수정 1] 시스템 시작 메시지를 setup()으로 이동하여 한 번만 출력
  Serial.println(); // 초기 공백
  Serial.println(F("[SYSTEM] Arduino LoRa Node Started"));
  Serial.println(F("[SYSTEM] Mode: CSMA Activated"));
  Serial.println(F("[SYSTEM] Waiting for input..."));
}

void command(char com[]){
  switch(com[2]){
    case 0:
      //node id
      nodeID = com[4];
      break;
  }
}

void receiver() 
{
    if(myLoRa.available() > 0){ 
    delay(10); // 데이터가 전송되는 동안 살짝 기다림 (안정성)
    String buff = myLoRa.readString();
    Serial.print("[SYSTEM]get, sucess\n");
    
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
  String ammo;
  char array[20];
  unsigned long currentMillis = millis();

  if(Serial.available()>0 && coked == false){
    delay(10); //안정성
    ammo = Serial.readStringUntil('\n');

    if (ammo[0] == 'C'){
      ammo.toCharArray(array);
      command()
    }
    if(ammo.length()>0) {
      coked = true;
      Serial.print("[COKED], currentMillis : ");
      Serial.print(currentMillis);
      Serial.print("\n");
    }
  }
}

void trySend()
{
  if(coked==false) { return; }
  
  unsigned long currentMillis = millis();
  unsigned long receiverTime = random((nodeID+1)*200, (nodeID+2)*200) + currentMillis; //노드에 따른 체널 분리, 랜덤은 완충 역할

  while (receiverTime >= millis()) { receiver(); } //쏠게 있는 경우 잠깐 듣기만 하다가 체널검사하고,  
  
  if(digitalRead(AUX_PIN)==LOW) { backOffEndTime = random(50, 100) + currentMillis; Serial.print("[SEND_FAIL], backOff\n"); return; } //백오프 설정
  if(backOffEndTime >= currentMillis) { return; }
  
  myLoRa.print(ammo);
  coked = false;
  ammo = "";
  Serial.print("[SEND_SUCSSES], currentMillis : ");
  Serial.print(currentMillis);

}

void printSerial()
{
  int sectime = millis()/1000;
  static int prvtime = 0;
  if(prvtime < sectime){
    Serial.println(sectime);
    prvtime = sectime;
  }
}

void runCSMA() {
  //printSerial();
  receiver();
  chamber();
  trySend();
  
}

void loop() {
  runCSMA();
}