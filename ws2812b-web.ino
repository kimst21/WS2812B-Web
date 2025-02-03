// 필수 라이브러리 포함
#include <WiFi.h>            // Wi-Fi 연결 관리
#include <WebServer.h>       // 웹 서버 기능
#define WEB_SERVER WebServer // WebServer 클래스 축약
#define ESP_RESET ESP.restart() // ESP32 리셋 매크로
#include <WS2812FX.h>        // LED 제어 라이브러리

// 외부 파일 참조 (HTML, JavaScript)
extern const char index_html[]; // 웹 페이지 HTML
extern const char main_js[];    // 웹 페이지 JavaScript

// Wi-Fi 설정
#define WIFI_SSID "WeVO_2.4G"     // Wi-Fi SSID
#define WIFI_PASSWORD "------"    // Wi-Fi 비밀번호

// 정적 IP 설정 (옵션)
#ifdef STATIC_IP
  IPAddress ip(192,168,0,123);     // 고정 IP 주소
  IPAddress gateway(192,168,0,1);  // 게이트웨이 주소
  IPAddress subnet(255,255,255,0); // 서브넷 마스크
#endif

// 최소/최대 값 계산 매크로
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

// LED 설정
#define LED_PIN 4        // LED 데이터 핀
#define LED_COUNT 15     // LED 개수

// 네트워크 타임아웃 및 포트 설정
#define WIFI_TIMEOUT 30000 // Wi-Fi 재연결 시도 시간 (30초)
#define HTTP_PORT 80       // 웹 서버 포트

// LED 기본 설정
#define DEFAULT_COLOR 0xFF5900      // 기본 색상 (주황색)
#define DEFAULT_BRIGHTNESS 128      // 기본 밝기 (50%)
#define DEFAULT_SPEED 1000          // 기본 속도 (1초)
#define DEFAULT_MODE FX_MODE_STATIC // 기본 효과 모드 (고정 색상)

// 전역 변수
unsigned long auto_last_change = 0;      // 자동 모드 변경 시간 기록
unsigned long last_wifi_check_time = 0;  // 마지막 Wi-Fi 확인 시간
String modes = "";                       // 효과 모드 목록 (HTML 형식)
uint8_t myModes[] = {};                  // 사용자 정의 모드 목록 (옵션)
bool auto_cycle = false;                 // 자동 모드 전환 여부

// 객체 초기화
WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800); // LED 제어 객체
WEB_SERVER server(HTTP_PORT); // 웹 서버 객체

// 설정 함수
void setup(){
  Serial.begin(115200); // 시리얼 통신 시작 (속도 115200bps)
  delay(500); // 안정화 대기
  Serial.println("\n\nStarting...");

  modes.reserve(5000); // 모드 문자열 메모리 예약
  modes_setup(); // 효과 모드 목록 생성

  // LED 초기화
  Serial.println("WS2812FX setup");
  ws2812fx.init(); // LED 초기화
  ws2812fx.setMode(DEFAULT_MODE); // 기본 모드 설정
  ws2812fx.setColor(DEFAULT_COLOR); // 기본 색상 설정
  ws2812fx.setSpeed(DEFAULT_SPEED); // 기본 속도 설정
  ws2812fx.setBrightness(DEFAULT_BRIGHTNESS); // 기본 밝기 설정
  ws2812fx.start(); // LED 효과 시작

  // Wi-Fi 연결
  Serial.println("Wifi setup");
  wifi_setup(); // Wi-Fi 연결 시도

  // 웹 서버 라우팅 설정
  Serial.println("HTTP server setup");
  server.on("/", srv_handle_index_html);       // 루트 경로 처리
  server.on("/main.js", srv_handle_main_js);   // JavaScript 파일 처리
  server.on("/modes", srv_handle_modes);       // 모드 목록 요청 처리
  server.on("/set", srv_handle_set);           // 설정 변경 요청 처리
  server.onNotFound(srv_handle_not_found);     // 404 에러 처리
  server.begin(); // 서버 시작
  Serial.println("HTTP server started.");

  Serial.println("ready!"); // 준비 완료 메시지
}

// 메인 루프
void loop() {
  unsigned long now = millis(); // 현재 시간 측정

  server.handleClient(); // 클라이언트 요청 처리
  ws2812fx.service(); // LED 효과 업데이트

  // Wi-Fi 주기적 확인 (30초마다)
  if(now - last_wifi_check_time > WIFI_TIMEOUT) {
    Serial.print("Checking WiFi... ");
    if(WiFi.status() != WL_CONNECTED) { // 연결 끊김 시 재시도
      Serial.println("WiFi connection lost. Reconnecting...");
      wifi_setup();
    } else {
      Serial.println("OK");
    }
    last_wifi_check_time = now; // 마지막 확인 시간 업데이트
  }

  // 자동 모드 전환 (10초마다)
  if(auto_cycle && (now - auto_last_change > 10000)) {
    uint8_t next_mode = (ws2812fx.getMode() + 1) % ws2812fx.getModeCount(); // 다음 모드 계산
    if(sizeof(myModes) > 0) { // 사용자 정의 모드 목록이 있을 경우
      for(uint8_t i=0; i < sizeof(myModes); i++) {
        if(myModes[i] == ws2812fx.getMode()) {
          next_mode = ((i + 1) < sizeof(myModes)) ? myModes[i + 1] : myModes[0]; // 커스텀 목록 순환
          break;
        }
      }
    }
    ws2812fx.setMode(next_mode); // 모드 변경
    Serial.print("mode is "); Serial.println(ws2812fx.getModeName(ws2812fx.getMode()));
    auto_last_change = now; // 변경 시간 기록
  }
}

// Wi-Fi 연결 함수
void wifi_setup() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Wi-Fi 연결 시도
  WiFi.mode(WIFI_STA); // 스테이션 모드 설정
  #ifdef STATIC_IP  
    WiFi.config(ip, gateway, subnet); // 정적 IP 설정
  #endif

  unsigned long connect_start = millis(); // 연결 시작 시간 기록
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    // 타임아웃 시 ESP32 재시작
    if(millis() - connect_start > WIFI_TIMEOUT) {
      Serial.println();
      Serial.print("Tried ");
      Serial.print(WIFI_TIMEOUT);
      Serial.print("ms. Resetting ESP now.");
      ESP_RESET; // 강제 재시작
    }
  }

  // 연결 성공 시 정보 출력
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

// 효과 모드 목록 생성 함수
void modes_setup() {
  modes = "";
  uint8_t num_modes = sizeof(myModes) > 0 ? sizeof(myModes) : ws2812fx.getModeCount(); // 모드 개수 계산
  for(uint8_t i=0; i < num_modes; i++) {
    uint8_t m = sizeof(myModes) > 0 ? myModes[i] : i; // 모드 번호 선택
    modes += "<li><a href='#'>"; // HTML 리스트 항목 생성
    modes += ws2812fx.getModeName(m); // 모드 이름 추가
    modes += "</a></li>";
  }
}

// 웹 서버 핸들러 함수들
void srv_handle_not_found() {
  server.send(404, "text/plain", "File Not Found"); // 404 에러 응답
}

void srv_handle_index_html() {
  server.send_P(200,"text/html", index_html); // HTML 파일 전송
}

void srv_handle_main_js() {
  server.send_P(200,"application/javascript", main_js); // JavaScript 파일 전송
}

void srv_handle_modes() {
  server.send(200,"text/plain", modes); // 모드 목록 전송
}

// 설정 변경 핸들러
void srv_handle_set() {
  for (uint8_t i=0; i < server.args(); i++){
    // 색상 변경 (c 파라미터)
    if(server.argName(i) == "c") {
      uint32_t tmp = (uint32_t) strtol(server.arg(i).c_str(), NULL, 10);
      if(tmp >= 0x000000 && tmp <= 0xFFFFFF) {
        ws2812fx.setColor(tmp); // 유효한 HEX 색상일 경우 적용
      }
    }

    // 모드 변경 (m 파라미터)
    if(server.argName(i) == "m") {
      uint8_t tmp = (uint8_t) strtol(server.arg(i).c_str(), NULL, 10);
      uint8_t new_mode = sizeof(myModes) > 0 ? myModes[tmp % sizeof(myModes)] : tmp % ws2812fx.getModeCount();
      ws2812fx.setMode(new_mode); // 모드 변경 및 로그 출력
      Serial.print("mode is "); Serial.println(ws2812fx.getModeName(ws2812fx.getMode()));
    }

    // 밝기 조절 (b 파라미터)
    if(server.argName(i) == "b") {
      if(server.arg(i)[0] == '-') { // 20% 감소
        ws2812fx.setBrightness(ws2812fx.getBrightness() * 0.8);
      } else if(server.arg(i)[0] == ' ') { // 20% 증가
        ws2812fx.setBrightness(min(max(ws2812fx.getBrightness(), 5) * 1.2, 255));
      } else { // 직접 설정
        uint8_t tmp = (uint8_t) strtol(server.arg(i).c_str(), NULL, 10);
        ws2812fx.setBrightness(tmp);
      }
      Serial.print("brightness is "); Serial.println(ws2812fx.getBrightness());
    }

    // 속도 조절 (s 파라미터)
    if(server.argName(i) == "s") {
      if(server.arg(i)[0] == '-') { // 20% 감속
        ws2812fx.setSpeed(max(ws2812fx.getSpeed(), 5) * 1.2);
      } else if(server.arg(i)[0] == ' ') { // 20% 가속
        ws2812fx.setSpeed(ws2812fx.getSpeed() * 0.8);
      } else { // 직접 설정
        uint16_t tmp = (uint16_t) strtol(server.arg(i).c_str(), NULL, 10);
        ws2812fx.setSpeed(tmp);
      }
      Serial.print("speed is "); Serial.println(ws2812fx.getSpeed());
    }

    // 자동 모드 전환 (a 파라미터)
    if(server.argName(i) == "a") {
      if(server.arg(i)[0] == '-') {
        auto_cycle = false; // 비활성화
      } else {
        auto_cycle = true;  // 활성화
        auto_last_change = 0; // 시간 초기화
      }
    }
  }
  server.send(200, "text/plain", "OK"); // 성공 응답
}
