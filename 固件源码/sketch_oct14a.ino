#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>

// 红外接收部分
const uint16_t kRecvPin = 2;  // 红外接收管连接的引脚
IRrecv irrecv(kRecvPin);
decode_results results;

// WiFi部分
ESP8266WebServer server(80);  // Web服务器对象

const int EEPROM_SIZE = 512;  // EEPROM总大小
const int EEPROM_ADDR_BUTTON1 = 0;
const int EEPROM_ADDR_BUTTON2 = 4;
const int EEPROM_ADDR_BUTTON3 = 8;
const int EEPROM_ADDR_BUTTON4 = 12;
const int EEPROM_ADDR_SSID = 50;    // 存储SSID的EEPROM地址
const int EEPROM_ADDR_PASSWORD = 100; // 存储密码的EEPROM地址
const int SSID_MAX_LEN = 32;
const int PASSWORD_MAX_LEN = 64;

char ssid[SSID_MAX_LEN];  // 存储WiFi名称
char password[PASSWORD_MAX_LEN];  // 存储WiFi密码

uint32_t learningIRCode = 0;  // 存储学习过程中接收到的红外信号
int learningCount = 0;        // 记录接收到相同信号的次数
const int EEPROM_ADDR_PWM_FREQ = 16;  // 保存PWM频率的EEPROM地址


// PWM部分
const int pwmPin = 0;  // PWM输出引脚
int freq = 1000;  // PWM频率1kHz
const int pwmChannel = 0;
const int resolution = 8;  // PWM分辨率8位

// 按钮代码存储
uint32_t button1Code = 0;  // 存储按钮1的红外代码
uint32_t button2Code = 0;  // 存储按钮2的红外代码
uint32_t button3Code = 0;  // 存储按钮3的红外代码
uint32_t button4Code = 0;  // 存储按钮4的红外代码

int dutyCycle = 50;  // 运行占空比
bool pwmEnabled = false;  // PWM状态
int nowCycle = 0;   //占空比记录

// 当前选择的按钮
int selectedButton = 0;  // 0 表示未选择学习按钮

// 函数：保存字符串到EEPROM
void saveStringToEEPROM(int startAddress, const char* str, int maxLength) {
  for (int i = 0; i < maxLength; i++) {
    EEPROM.write(startAddress + i, str[i]);
    if (str[i] == 0) break;
  }
  EEPROM.commit();
}

// 函数：从EEPROM读取字符串
void readStringFromEEPROM(int startAddress, char* buffer, int maxLength) {
  for (int i = 0; i < maxLength; i++) {
    buffer[i] = EEPROM.read(startAddress + i);
    if (buffer[i] == 0) break;
  }
}

void saveIRCodeToEEPROM(int address, uint32_t code) {
  EEPROM.put(address, code);
  EEPROM.commit();  // 保存修改
}

uint32_t readIRCodeFromEEPROM(int address) {
  uint32_t code;
  EEPROM.get(address, code);
  return code;
}

// 函数：显示主页按钮界面
void handleRoot() {
  String html = "<html>\
  <head>\
  <title>IR Learning</title>\
  <meta http-equiv='Content-Type' content='text/html;charset=utf-8'>\
  <meta http-equiv='Cache-Control' content='no-cache, no-store, must-revalidate'>\
  <meta http-equiv='Pragma' content='no-cache'>\
  <meta http-equiv='Expires' content='0'>\
  <script>\
    function updateDutyCycle(val) {\
      var xhr = new XMLHttpRequest();\
      xhr.open('GET', '/setDutyCycle?value=' + val, true);\
      xhr.send();\
    }\
    function updateFreq() {\
      var freqVal = document.getElementById('freqInput').value;\
      var xhr = new XMLHttpRequest();\
      xhr.open('GET', '/setFreq?freq=' + freqVal, true);\
      xhr.send();\
    }\
    function togglePWM() {\
      var xhr = new XMLHttpRequest();\
      xhr.open('GET', '/togglePWM', true);\
      xhr.send();\
    }\
  </script>\
  </head>\
  <body>\
  <h1>红外遥控学习 by Hoobers</h1>\
  <p>选择按钮:</p>\
  <button onclick=\"location.href='/learn?button=1'\">Learn 开/关PWM</button><br><br>\
  <button onclick=\"location.href='/learn?button=2'\">Learn 增加占空比</button><br><br>\
  <button onclick=\"location.href='/learn?button=3'\">Learn 减少占空比</button><br><br>\
  <button onclick=\"location.href='/learn?button=4'\">Learn 一键起飞</button><br><br>\
  <button onclick=\"location.href='/config'\">配置 WiFi</button><br><br>\
  <h2>PWM 开关</h2>\
  <button onclick='togglePWM()'>" + String(pwmEnabled ? "关闭 PWM" : "打开 PWM") + "</button>\
  <h2>PWM 滑动控制</h2>\
  <input type='range' min='0' max='255' value='" + String(dutyCycle) + "' oninput='updateDutyCycle(this.value)'>\
  <p>Current Duty Cycle: <span id='dutyCycleValue'>" + String(dutyCycle) + "</span></p>\
  <h2>PWM 频率设置</h2>\
  <input type='number' id='freqInput' value='" + String(freq) + "' min='10' max='38000'>\
  <button onclick='updateFreq()'>设置频率</button>\
  <p>Current Frequency: <span id='freqValue'>" + String(freq) + "</span></p>\
  <h2>固件升级</h2>\
  <form action='/update' method='POST' enctype='multipart/form-data'>\
    <input type='file' name='update'>\
    <input type='submit' value='Update'>\
  </form>\
  </body>\
  </html>";
  server.send(200, "text/html", html);
}

//pwm频率设置函数
void handleSetFreq() {
  if (server.hasArg("freq")) {
    int newFreq = server.arg("freq").toInt();  // 获取传入的频率
    if (newFreq < 10) newFreq = 10;  // 设置最小频率限制
    if (newFreq > 38000) newFreq = 38000;  // 设置最大频率限制
    
    freq = newFreq;  // 更新PWM频率

    analogWriteFreq(freq);  // 设置新的PWM频率
    saveIRCodeToEEPROM(EEPROM_ADDR_PWM_FREQ, freq);  // 保存到EEPROM

    Serial.print("PWM Frequency updated to: ");
    Serial.println(freq);

    // 发送成功响应
    server.send(200, "text/plain", "PWM Frequency updated");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}


//滑动条 函数
void handleSetDutyCycle() {
  if (server.hasArg("value")) {
    dutyCycle = server.arg("value").toInt();  // 获取滑动条的值并更新占空比
    if (dutyCycle < 0) dutyCycle = 0;
    if (dutyCycle > 255) dutyCycle = 255;

    // 如果PWM已启用，更新占空比
    if (pwmEnabled) {
      analogWrite(pwmPin, dutyCycle);
    }

    Serial.print("Duty Cycle updated to: ");
    Serial.println(dutyCycle);

    // 发送成功响应
    server.send(200, "text/plain", "Duty Cycle updated");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

//pwm开关控制
void handleTogglePWM() {
  pwmEnabled = !pwmEnabled;  // 切换PWM状态
  if (pwmEnabled) {
    analogWrite(pwmPin, dutyCycle);  // 如果启用了PWM，则开始输出
    Serial.println("PWM Enabled");
  } else {
    analogWrite(pwmPin, 0);  // 如果关闭了PWM，则停止输出
    Serial.println("PWM Disabled");
  }

  // 更新主页的 PWM 按钮状态
  handleRoot();
}



// 函数：学习红外信号界面
void handleLearn() {
  if (server.hasArg("button")) {
    selectedButton = server.arg("button").toInt();  // 选择的按钮编号
    String message = "Selected Button " + String(selectedButton) + ". Waiting for IR signal...";
    
    // HTML内容包括返回主页按钮
    String html = "<html>\
    <head>\
    <title>IR Learning</title>\
    </head>\
    <body>\
    <h1>" + message + "</h1>\
    <button onclick=\"location.href='/'\">Return to Home</button>\
    </body>\
    </html>";
    
    server.send(200, "text/html", html);
    Serial.println(message);
  }
}

// 函数：显示WiFi配置界面
void handleConfig() {
  String html = "<html>\
  <head><title>WiFi Configuration</title></head>\
  <body>\
  <h1>Configure WiFi</h1>\
  <form action='/saveConfig' method='POST'>\
    SSID: <input type='text' name='ssid'><br><br>\
    Password: <input type='password' name='password'><br><br>\
    <input type='submit' value='Save'>\
  </form>\
  </body>\
  </html>";
  server.send(200, "text/html", html);
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("Starting firmware update...");
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) { //start with max available size
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { //true to set the size to the current progress
      Serial.println("Update success!");
      String html = "<html><body><h1>Update Success!</h1><p>Device will restart in 5 seconds...</p></body></html>";
      server.send(200, "text/html", html);
      delay(5000);
      ESP.restart();
    } else {
      Update.printError(Serial);
    }
  }
}

// 函数：保存WiFi配置
void handleSaveConfig() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");

    // 保存到EEPROM
    saveStringToEEPROM(EEPROM_ADDR_SSID, newSSID.c_str(), SSID_MAX_LEN);
    saveStringToEEPROM(EEPROM_ADDR_PASSWORD, newPassword.c_str(), PASSWORD_MAX_LEN);

    // 显示保存成功页面
    String html = "<html>\
    <head><title>Configuration Saved</title></head>\
    <body>\
    <h1>Configuration Saved!</h1>\
    <p>Device will restart and attempt to connect to the new network.</p>\
    <script>setTimeout(function(){ location.href='/'; }, 3000);</script>\
    </body>\
    </html>";
    server.send(200, "text/html", html);
    
    delay(2000);
    ESP.restart();  // 重启设备
  }
}

// 函数：启动AP模式进行网页配网
void startAPMode() {
  WiFi.softAP("ESP8266_Config", "12345678");  // 设置AP模式的SSID和密码
  IPAddress IP = WiFi.softAPIP();  // 获取AP模式的IP地址
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // 启动网页配网服务器
  // server.on("/", handleConfig);
  // server.on("/saveConfig", handleSaveConfig);
  // server.begin();
  // Serial.println("Web server for configuration started in AP mode.");

  server.on("/", handleRoot);       // 处理主页请求
  server.on("/learn", handleLearn); // 处理学习按钮请求
  server.on("/config", handleConfig); // 配置WiFi
  server.on("/setFreq", handleSetFreq);  // 处理PWM频率设置请求
  server.on("/togglePWM", handleTogglePWM);  // 处理 PWM 开关请求
  server.on("/setDutyCycle", handleSetDutyCycle);  // 处理占空比设置请求
  server.on("/saveConfig", handleSaveConfig);
  server.on("/update", HTTP_POST, []() {
  server.send(200, "text/plain", "Update started...");
  }, handleUpdate); // 处理固件升级请求
  server.begin();                   // 启动Web服务器
  Serial.println("Web server started after successful WiFi connection.");
}

// 函数：连接WiFi
void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {  // 最多尝试20次
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());  // 输出设备的IP地址

    // 启动Web服务器
    server.on("/", handleRoot);       // 处理主页请求
    server.on("/learn", handleLearn); // 处理学习按钮请求
    server.on("/config", handleConfig); // 配置WiFi
    server.on("/setFreq", handleSetFreq);  // 处理PWM频率设置请求
    server.on("/togglePWM", handleTogglePWM);  // 处理 PWM 开关请求
    server.on("/setDutyCycle", handleSetDutyCycle);  // 滑动条处理占空比设置请求
    server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", "Update started...");
  }, handleUpdate); // 处理固件升级请求
    server.begin();                   // 启动Web服务器
    Serial.println("Web server started after successful WiFi connection.");

  } else {
    Serial.println("Failed to connect to WiFi. Starting AP mode...");
    startAPMode();  // 启动AP模式进行配网
  }
}

void setup() {
  Serial.begin(115200);
  irrecv.enableIRIn();  // 启动红外接收

  // 初始化EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // 从EEPROM读取之前存储的WiFi信息
  readStringFromEEPROM(EEPROM_ADDR_SSID, ssid, SSID_MAX_LEN);
  readStringFromEEPROM(EEPROM_ADDR_PASSWORD, password, PASSWORD_MAX_LEN);

   // 从EEPROM中读取PWM频率
  freq = readIRCodeFromEEPROM(EEPROM_ADDR_PWM_FREQ);
  if (freq < 10 || freq > 38000) {  // 如果频率不在合法范围，使用默认频率
    freq = 1000;  // 默认1kHz
  }

  // 从EEPROM中读取之前存储的红外信号
  button1Code = readIRCodeFromEEPROM(EEPROM_ADDR_BUTTON1);
  button2Code = readIRCodeFromEEPROM(EEPROM_ADDR_BUTTON2);
  button3Code = readIRCodeFromEEPROM(EEPROM_ADDR_BUTTON3);
  button4Code = readIRCodeFromEEPROM(EEPROM_ADDR_BUTTON4);


  Serial.println("Stored IR codes loaded from EEPROM");

  // 检查是否有存储的WiFi信息
  if (strlen(ssid) > 0 && strlen(password) > 0) {
    connectToWiFi();  // 尝试连接WiFi
  } else {
    // 没有存储的WiFi信息，启动AP模式进行配网
    Serial.println("No WiFi credentials found, starting AP mode...");
    startAPMode();
  }

  // PWM设置
  pinMode(pwmPin, OUTPUT);
  analogWriteFreq(freq);  // 设置PWM频率
  analogWrite(pwmPin, nowCycle);  // 设置初始占空比
}

void loop() {
  server.handleClient();  // 处理网页请求

  // 红外信号处理
  if (irrecv.decode(&results)) {
    uint32_t irCode = results.value;

    if (selectedButton != 0) {
      // 学习模式：需要接收到两次相同的信号
      if (irCode == learningIRCode) {
        learningCount++;  // 如果接收到的信号和上次相同，计数加1
      } else {
        learningIRCode = irCode;  // 如果不同，重新开始学习新的信号
        learningCount = 1;  // 重置计数器
      }

      if (learningCount >= 2) {
        // 当接收到相同信号两次后，认为学习成功
        String buttonName;
        switch (selectedButton) {
          case 1:
            button1Code = irCode;
            saveIRCodeToEEPROM(EEPROM_ADDR_BUTTON1, button1Code);  // 保存到EEPROM
            buttonName = "Button 1";
            break;
          case 2:
            button2Code = irCode;
            saveIRCodeToEEPROM(EEPROM_ADDR_BUTTON2, button2Code);  // 保存到EEPROM
            buttonName = "Button 2";
            break;
          case 3:
            button3Code = irCode;
            saveIRCodeToEEPROM(EEPROM_ADDR_BUTTON3, button3Code);  // 保存到EEPROM
            buttonName = "Button 3";
            break;
          case 4:
            button4Code = irCode;
            saveIRCodeToEEPROM(EEPROM_ADDR_BUTTON4, button4Code);  // 保存到EEPROM
            buttonName = "Button 4";
            break;
        }
        Serial.println(buttonName + " learned");

        // 重置学习模式变量
        selectedButton = 0;  // 重置选择
        learningIRCode = 0;  // 清空学习信号
        learningCount = 0;   // 重置计数
      }

    } else {
      // 控制模式：只需要接收到一次信号就能执行命令
      if (irCode == button1Code) {
        pwmEnabled = !pwmEnabled;
        if (pwmEnabled) {
          analogWrite(pwmPin, nowCycle);
          Serial.println("PWM Enabled");
        } else {
          nowCycle = dutyCycle;
          analogWrite(pwmPin, 0);
          Serial.println("PWM Disabled");
        }
      } else if (irCode == button2Code) {
        if(pwmEnabled == true){
          dutyCycle += 10;
          if (dutyCycle > 255) dutyCycle = 255;
        }else {
          pwmEnabled = true;
        }
        
      } else if (irCode == button3Code) {
        dutyCycle -= 10;
        if (dutyCycle < 0) dutyCycle = 0;
      } else if (irCode == button4Code) {
        if(dutyCycle == 255){
          dutyCycle = nowCycle;
        } else {
          nowCycle = dutyCycle;
          dutyCycle = 255;
        }
        
      }

      // 更新PWM输出
      if (pwmEnabled) {
        analogWrite(pwmPin, dutyCycle);
      }

      Serial.print("Duty Cycle: ");
      Serial.println(dutyCycle);
    }

    irrecv.resume();  // 继续接收下一个红外信号
  }
}
