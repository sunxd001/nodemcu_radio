//WIFI网络收音机
//设计参考了DanaRota_Radio_Project的设计，主要修改了TFT显示和收音功放
//TFT库进行了修改，RST接modemcu的RST，CS接D8，CLK接D5，MOSI接D7。TFT库初始化仅需要指定D3为DS即可
//功放为TDA2822，接RX
//旋转编码器接D0,D1,D2
//原程序中D3,D4接了两个LED,D3为播放状态指示，D4为电源指示
//由于ESP8266的IO数较少，ST7735 TFT LCD使用了硬件SPI作为接口，而I2S使用了D4、D8、RX,D8与HSPI的CS冲突，所以在显示与播放声音间需要切换
//CPU FREQ 160MHZ,lwvariant V2 Highbankwidth

#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2SNoDAC.h"
#include <EEPROM.h>

#include "Button2.h" //  https://github.com/LennartHennigs/Button2
#include "ESPRotary.h"

#define ROTARY_PIN1 D0
#define ROTARY_PIN2 D1
#define BUTTON_PIN  D2

#define CLICKS_PER_STEP 4   // this number depends on your rotary encoder
#define MIN_POS         0
#define MAX_POS         9
#define START_POS       0
#define INCREMENT       1   // this number is the counter increment on each step

// To run, set your ESP8266 build to 160MHz, update the SSID info, and upload.
// Enter your WiFi setup here:
#ifndef STASSID
#define STASSID "qq21012215"
#define STAPSK  "20091224lxy"
#endif

#include <SPI.h>
#include <Adafruit_GFX.h>//图形库
#include <Adafruit_ST7735.h> // Hardware-specific library

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 128 // OLED display height, in pixels

#define TFT_CS     0  //connect CS pin to D8,hardware CS
#define TFT_RST    0 // connect this to the Arduino reset
#define TFT_DC     D3
//Adafruit_ST7735 display = Adafruit_ST7735(TFT_CS,  TFT_DC, TFT_RST);
Adafruit_ST7735 display(TFT_CS,  TFT_DC, TFT_RST);

const char* ssid = STASSID;
const char* password = STAPSK;

//URL'S
const char *URL[] = {"http://jazz.streamr.ru/jazz-64.mp3",
                     "http://a1rj.streams.com.br:7801/sm",
                     "http://www.golden-apple.com:680/;",
                     //"http://stm14.mfmedios.info:8048/;",
                     "http://desi.canstream.co.uk:8001/live.mp3",
                     //"http://cast2.servcast.net:3020/;",
                     "http://59.120.88.155:8000/live.mp3",
                     //"http://live02.rfi.fr/rfimonde-64.mp3",
                     "http://mobilewkdm.serverroom.us:6912/stream/1/",
                     "http://live.wbcb1490.com:88/broadwavehigh.mp3",
                     "http://14543.live.streamtheworld.com:3690/XHFO_FM_SC",
                     "http://14523.live.streamtheworld.com:3690/KNBAFM_SC",
                     "http://radio.sxtvs.com/radio/story.mp3",
                     "http://stream.lt8.com.ar:8080/delsiglo995.mp3"
                    };
//URL'S Names
const char *ChName[] = {"Jazz RU",
                        "ALJ",
                        "Golden Apple",
                        //"Mfmedios",
                        "Desi Radio Live",
                        //"Servcast",
                        "Taiwan Classic Music",
                        //"RFI - Monde",
                        "NewYork chinese",
                        "WBCB UK",
                        "XHFO FM",
                        "ShanXi Story",
                        "Radio Nacional"
                       };

//旋转编码器与配套开关
ESPRotary r;
Button2 b;    // https://github.com/LennartHennigs/Button2

AudioGeneratorMP3 *mp3;
AudioFileSourceICYStream *file;
AudioFileSourceBuffer *buff;
AudioOutputI2SNoDAC *out;

int normalmode = true;
int runstream = true;
int ch = 0;

//EEPROM//
int addr = 0;
int address = 0;
byte value;
byte value1;


const int preallocateBufferSize = 5*1024;
const int preallocateCodecSize = 29192; // MP3 codec max mem needed

void *preallocateBuffer = NULL;
void *preallocateCodec = NULL;

//add by shs 
int bGotIP=0;
bool bConnected=false;

WiFiEventHandler STAConnected;
WiFiEventHandler STADisconnected;
WiFiEventHandler STAGotIP;

void ConnectedHandler(const WiFiEventStationModeConnected &event)
{
    Serial.println(WiFi.status());
    WiFi.printDiag(Serial);
    bConnected=true;
    Serial.println("模块连接到网络");
}

void DisconnectedHandler(const WiFiEventStationModeDisconnected &event)
{
    Serial.println(WiFi.status());
    WiFi.printDiag(Serial);
    bConnected=false;
    Serial.println("模块从网络断开");
}

void GotIPHandler(const WiFiEventStationModeGotIP &event)
{
        Serial.println(WiFi.status());
        bGotIP=1;
        Serial.println("模块获得IP");
        
};
//add by shs end
void setup()
{
  Serial.begin(115200);

  preallocateBuffer = malloc(preallocateBufferSize);
  preallocateCodec = malloc(preallocateCodecSize);
  if (!preallocateBuffer || !preallocateCodec) {
    Serial.begin(115200);
    Serial.printf_P(PSTR("FATAL ERROR:  Unable to preallocate %d bytes for app\n"), preallocateBufferSize+preallocateCodecSize);
    while (1) delay(1000); // Infinite halt
  }
  
  EEPROM.begin(512);
 //初始化ch和normalmode
  EepromRead();
  if(value==255)
    ch=0;//测试时指定0频道
  else 
    ch = value;
  //ch=0;  
  if(value1 == 0){
    normalmode = false;
  }
   
  if(value1 == 1){
    normalmode = true;
  }
  

  b.begin(BUTTON_PIN);//D2
  b.setLongClickHandler(LongPress);
    
  r.begin(ROTARY_PIN1, ROTARY_PIN2, CLICKS_PER_STEP, MIN_POS, MAX_POS, ch, INCREMENT);//D0,D1
  r.setChangedHandler(rotate);
 

  display.initR(INITR_144GREENTAB);
  display.fillScreen(ST7735_BLACK);
  
  Serial.println("Connecting to WiFi");
  //WiFi.disconnect();
  //WiFi.softAPdisconnect(true);
  //STAConnected = WiFi.onStationModeConnected(ConnectedHandler);
  //STADisconnected = WiFi.onStationModeDisconnected(DisconnectedHandler);
  //STAGotIP = WiFi.onStationModeGotIP(GotIPHandler);
  
  //WiFi.forceSleepWake();
  //WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Try forever
  normalmode=true;//长按longpress false
  if(normalmode == true){
    while (WiFi.status() != WL_CONNECTED) {
      b.loop();
      r.loop();
      triangle();
      Serial.println("\r\n...Connecting to WiFi\r\n");
      //Serial.println(WiFi.status());
      //Serial.println(WL_CONNECTED);
      //Serial.println(WiFi.localIP());    
    }
 }
 
  b.setTapHandler(click);
  //以上是BUTTON,ROTARY,屏幕,WIFI
  draw();
  //Serial.print("ch=");Serial.println(ch);
  
  //audioLogger = &Serial;
  bufffile();   
}

void loop()
{
  if(normalmode == true){//只有相应MP3源成功连接才播放，否则内存溢出，重启;可能是主动重启
    if(mp3){
      RunStream();
    }
  }
  
  r.loop();
  b.loop();
  EepromWrite();
}

void bufffile() {
  //处理管脚冲突
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U,FUNC_I2SO_BCK);//设为I2SBCK
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U,FUNC_I2SO_WS);//设为I2SWS
  for (int i = 0; i < 10; i++) {
    if (ch == i) {
      file = new AudioFileSourceICYStream(URL[i]);
    }
  }
  file->RegisterMetadataCB(MDCallback, (void*)"ICY");
  //buff = new AudioFileSourceBuffer(file, 8192);
  buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);
  
  buff->RegisterStatusCB(StatusCallback, (void*)"buffer");
  out = new AudioOutputI2SNoDAC();
  //mp3 = new AudioGeneratorMP3();
  mp3 = new AudioGeneratorMP3(preallocateCodec, preallocateCodecSize);
  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");
  mp3->begin(buff, out);
}

void unbufffile(){
  if(mp3){
    mp3->stop();
    delete mp3;
    mp3=NULL;
  }
  if(out){
    delete out;
    out=NULL;
  }
  if(buff){
    delete buff;
    buff=NULL;
  }
  
  if(file){
    delete file;
    file=NULL;
  }
  //处理管脚冲突
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U,FUNC_HSPI_CS0);//设为HSPI_CS,以便操作显示屏
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U,FUNC_GPIO2);//恢复GPIO2  
}

void RunStream() {
    if (runstream == true) {
      static int lastms = 0;
      if (mp3->isRunning()) {
        if (millis() - lastms > 1000) {
          lastms = millis();
          //Serial.printf("Running for %d ms...\n", lastms);
          //Serial.flush();
          //digitalWrite(D3, HIGH);
        }
        if (!mp3->loop()) mp3->stop();
  
      } else {
        Serial.printf("MP3 done_RunStream\n");
        if(normalmode == true){
          unbufffile();
          Serial.printf("MP3 done_unbufffile\n");
          int lastms1=millis();
          while(millis()-lastms1<500){
            b.loop();
            r.loop();
          }  
          bufffile();
          Serial.printf("MP3 done_bufffile\n");
          runstream=true;
        //ESP.reset();//此处重启？？？？？？？？？？？？？？？？？？？？？？？？？？
        }
      }
    }
  
}

// Called when a metadata event occurs (i.e. an ID3 tag, an ICY block, etc.
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  // Note that the type and string may be in PROGMEM, so copy them to RAM for printf
  char s1[32], s2[64];
  strncpy_P(s1, type, sizeof(s1));
  s1[sizeof(s1) - 1] = 0;
  strncpy_P(s2, string, sizeof(s2));
  s2[sizeof(s2) - 1] = 0;
  //Serial.printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
  //Serial.flush();

  draw();
  display.fillRect(2,11*10,120,10,ST7735_BLACK);
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(ST7735_WHITE);        // Draw white text
  display.setCursor(2,11*10);
  
  display.println(s2);  
}


// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string)
{
  //digitalWrite(D3, LOW);

  const char *ptr = reinterpret_cast<const char *>(cbData);
  // Note that the string may be in PROGMEM, so copy it to RAM for printf
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1) - 1] = 0;
  //Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  //Serial.flush();
  
}

void EepromWrite() {
  if (runstream == false) {
    int val = ch;
    int val1 = normalmode;

    EEPROM.write(addr, val);
    addr = addr + 1;  
    
    EEPROM.write(addr, val1);
   
    addr = 512;
    if (addr == 512) {
      addr = 0;
      if (EEPROM.commit()) {
        //      Serial.println("EEPROM successfully committed");
      } else {
        //      Serial.println("ERROR! EEPROM commit failed");
      }
    }
  }
}
void EepromRead() {
  value = EEPROM.read(address);
  address = address + 1;
  value1 = EEPROM.read(address);
  
  address = 512;
  if (address == 512) {
    address = 0;
  }
  delay(500);
}

// on change
void rotate(ESPRotary& r) {
  unbufffile();
  runstream = false;
  ch = r.getPosition();
  draw();
  int lastms=millis();
  while(millis()-lastms<100){
    
  }
  //Serial.println("Rotate");
  //Serial.println(r.getPosition());
  //Serial.println(ch);
}


// single click
void click(Button2& btn) {
  Serial.println("Click!");
  draw();
  if(!mp3)bufffile();
  
  runstream=true;
  normalmode=true;
  mp3->loop();
  normalmode = true;
  EepromWrite();
  //ESP.reset();
}

// long click
void LongPress(Button2& btn) { 
   Serial.println("LONG CLICK");
   //runstream = false;
   //normalmode = false;
   //unbufffile();
   EepromWrite();
   
   //ESP.reset();
}

void draw(void) {
  display.fillScreen(ST7735_BLACK);
  ///////////////////////////////////////////
  display.drawRect(0,0,127,127,ST7735_YELLOW);
  
  display.setTextSize(1);
  display.setTextColor(ST7735_YELLOW);
  for(int i=0;i<10;i++){
    display.setCursor(2,i*10+2);
    display.println(ChName[i]);
  }
  
  display.setCursor(2,11*10);
  display.println(WiFi.localIP());
  
  //display.setCursor(80,10*10);
  //display.print("ch:");
  //display.println(ch);
  
  Serial.println("draw()");
  Serial.println(ch);
  
  ///////////////////////////////////////////
  
  display.fillRect(2,ch*10+2,120,10,ST7735_BLUE);
  display.setCursor(2,ch*10+2);
  display.println(ChName[ch]);
  
}

void triangle(void) {
  display.fillScreen(ST7735_BLACK);
  for (int16_t i = 0; i < max(display.width(), display.height()) / 4; i += 5) {
    display.drawTriangle(
      display.width() / 2  , display.height() / 2 - i,
      display.width() / 2 - i, display.height() / 2 + i,
      display.width() / 2 + i, display.height() / 2 + i, ST7735_WHITE);    
    delay(100);
  }
}
