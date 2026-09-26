#include "display_ui.h"
#include "app_config.h"
#include "app_state.h"
#include "control.h"
#include "network.h"
#include "storage.h"
#include "input.h"
#include "ui_fonts.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "soc/soc.h"
#include "soc/gpio_reg.h"

// ============================================================
// FAST ILI9486 DRIVER
// ============================================================
static const uint8_t dataPins[8] = {TFT_D0,TFT_D1,TFT_D2,TFT_D3,TFT_D4,TFT_D5,TFT_D6,TFT_D7};
static constexpr uint32_t TFT_DATA_MASK = 0xFFu << TFT_D0;
static constexpr uint32_t TFT_WR_MASK = 1u << TFT_WR;

static inline void writeBusFast(uint8_t value) {
  REG_WRITE(GPIO_OUT_W1TC_REG, TFT_DATA_MASK);
  REG_WRITE(GPIO_OUT_W1TS_REG, (uint32_t(value) << TFT_D0));
}

static inline void pulseWRFast() {
  REG_WRITE(GPIO_OUT_W1TC_REG, TFT_WR_MASK);
  __asm__ __volatile__("nop; nop; nop; nop;");
  REG_WRITE(GPIO_OUT_W1TS_REG, TFT_WR_MASK);
}

static void dataBusOutput() {
  for (int i = 0; i < 8; i++) pinMode(dataPins[i], OUTPUT);
}

static void writeCommand(uint8_t command) {
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_RS, LOW);
  writeBusFast(command);
  pulseWRFast();
  digitalWrite(TFT_CS, HIGH);
}

static void writeCommandData(uint8_t command, const uint8_t *data, int length) {
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_RS, LOW);
  writeBusFast(command);
  pulseWRFast();
  digitalWrite(TFT_RS, HIGH);
  for (int i = 0; i < length; i++) {
    writeBusFast(data[i]);
    pulseWRFast();
  }
  digitalWrite(TFT_CS, HIGH);
}

static void resetLCD() {
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_WR, HIGH);
  digitalWrite(TFT_RD, HIGH); // also controls buffer direction on this shield
  digitalWrite(TFT_RST, HIGH); delay(20);
  digitalWrite(TFT_RST, LOW); delay(50);
  digitalWrite(TFT_RST, HIGH); delay(150);
}

static void initILI9486() {
  resetLCD();
  writeCommand(0x01); delay(120);
  writeCommand(0x11); delay(120);
  const uint8_t pf[] = {0x55};
  writeCommandData(0x3A, pf, 1);
  const uint8_t c0[] = {0x0E,0x0E};
  const uint8_t c1[] = {0x41,0x00};
  const uint8_t c2[] = {0x55};
  const uint8_t c5[] = {0,0,0,0};
  writeCommandData(0xC0,c0,2); writeCommandData(0xC1,c1,2);
  writeCommandData(0xC2,c2,1); writeCommandData(0xC5,c5,4);
  const uint8_t gp[] = {0x0F,0x1F,0x1C,0x0C,0x0F,0x08,0x48,0x98,0x37,0x0A,0x13,0x04,0x11,0x0D,0x00};
  const uint8_t gn[] = {0x0F,0x32,0x2E,0x0B,0x0D,0x05,0x47,0x75,0x37,0x06,0x10,0x03,0x24,0x20,0x00};
  writeCommandData(0xE0,gp,15); writeCommandData(0xE1,gn,15);
  const uint8_t mad[] = {0x28}; // landscape, non-mirrored, BGR
  writeCommandData(0x36,mad,1);
  writeCommand(0x20);
  writeCommand(0x29);
  delay(100);
}

static void setAddressWindow(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) {
  const uint8_t c[] = {uint8_t(x0>>8),uint8_t(x0),uint8_t(x1>>8),uint8_t(x1)};
  const uint8_t r[] = {uint8_t(y0>>8),uint8_t(y0),uint8_t(y1>>8),uint8_t(y1)};
  writeCommandData(0x2A,c,4);
  writeCommandData(0x2B,r,4);
  writeCommand(0x2C);
}

static void fillRect(int x,int y,int w,int h,uint16_t color) {
  if (w<=0||h<=0) return;
  if (x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  if(x+w>LCD_WIDTH)w=LCD_WIDTH-x; if(y+h>LCD_HEIGHT)h=LCD_HEIGHT-y;
  if(w<=0||h<=0)return;
  setAddressWindow(x,y,x+w-1,y+h-1);
  uint8_t hi=color>>8,lo=color&0xFF;
  digitalWrite(TFT_CS,LOW); digitalWrite(TFT_RS,HIGH);
  uint32_t pixels=(uint32_t)w*h;
  for(uint32_t i=0;i<pixels;i++){
    writeBusFast(hi);pulseWRFast();writeBusFast(lo);pulseWRFast();
    if((i&0x3FFFu)==0) yield();
  }
  digitalWrite(TFT_CS,HIGH);
}

static void pushRect565(int x,int y,int w,int h,const uint16_t *pixels) {
  if (!pixels || w<=0 || h<=0) return;
  setAddressWindow(x,y,x+w-1,y+h-1);
  digitalWrite(TFT_CS,LOW); digitalWrite(TFT_RS,HIGH);
  uint32_t count=(uint32_t)w*h;
  for(uint32_t i=0;i<count;i++){
    uint16_t c=pixels[i];
    writeBusFast(uint8_t(c>>8)); pulseWRFast();
    writeBusFast(uint8_t(c)); pulseWRFast();
    if((i&0x3FFFu)==0) yield();
  }
  digitalWrite(TFT_CS,HIGH);
}

static void fillScreen(uint16_t c){fillRect(0,0,LCD_WIDTH,LCD_HEIGHT,c);}

// ============================================================
// SMOOTH FONT RENDERER (Inter Display, pre-rasterized in ui_fonts.h)
// ============================================================
static constexpr int TEXT_SCRATCH_W = LCD_WIDTH;
static constexpr int TEXT_SCRATCH_H = 96;
static uint16_t *textScratch = nullptr;

static const UiGlyph *findGlyph(const UiFont &font, uint16_t codepoint) {
  for (uint16_t i=0;i<font.glyphCount;i++) {
    if (font.glyphs[i].codepoint == codepoint) return &font.glyphs[i];
  }
  for (uint16_t i=0;i<font.glyphCount;i++) {
    if (font.glyphs[i].codepoint == '?') return &font.glyphs[i];
  }
  return nullptr;
}

static uint16_t nextCodepoint(const String &s, size_t &index) {
  if (index >= s.length()) return 0;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(s.c_str());
  uint8_t c = p[index++];
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && index < s.length()) {
    return uint16_t((c & 0x1F) << 6) | uint16_t(p[index++] & 0x3F);
  }
  if ((c & 0xF0) == 0xE0 && index + 1 < s.length()) {
    uint16_t cp = uint16_t((c & 0x0F) << 12) |
                  uint16_t((p[index] & 0x3F) << 6) |
                  uint16_t(p[index+1] & 0x3F);
    index += 2;
    return cp;
  }
  return '?';
}

static int textWidth(const String &s, const UiFont &font) {
  int width=0;
  size_t i=0;
  while(i<s.length()) {
    uint16_t cp=nextCodepoint(s,i);
    const UiGlyph *g=findGlyph(font,cp);
    if(g) width += g->advance;
  }
  return width;
}

static void appendCodepointUtf8(String &out, uint16_t cp) {
  if (cp < 0x80) out += char(cp);
  else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
  else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

static String fitText(const String &input, const UiFont &font, int maxWidth) {
  if (textWidth(input,font) <= maxWidth) return input;
  const String ell="...";
  int limit=max(0,maxWidth-textWidth(ell,font));
  String out;
  int used=0;
  size_t i=0;
  while(i<input.length()) {
    uint16_t cp=nextCodepoint(input,i);
    const UiGlyph *g=findGlyph(font,cp);
    int adv=g?g->advance:0;
    if(used+adv>limit)break;
    appendCodepointUtf8(out,cp);
    used+=adv;
  }
  return out+ell;
}

static uint16_t blend565(uint16_t bg,uint16_t fg,uint8_t alpha4) {
  if(alpha4==0) return bg;
  if(alpha4>=15) return fg;
  uint8_t br=(bg>>11)&31,bg6=(bg>>5)&63,bb=bg&31;
  uint8_t fr=(fg>>11)&31,fg6=(fg>>5)&63,fb=fg&31;
  uint8_t r=(br*(15-alpha4)+fr*alpha4+7)/15;
  uint8_t g=(bg6*(15-alpha4)+fg6*alpha4+7)/15;
  uint8_t b=(bb*(15-alpha4)+fb*alpha4+7)/15;
  return uint16_t((r<<11)|(g<<5)|b);
}

enum class TextAlign { LEFT, CENTER, RIGHT };

static void drawTextBox(int x,int y,int w,int h,const String &raw,const UiFont &font,
                        uint16_t fg,uint16_t bg,TextAlign align=TextAlign::LEFT,int padding=0) {
  if(w<=0||h<=0)return;
  if(!textScratch || w>TEXT_SCRATCH_W || h>TEXT_SCRATCH_H){fillRect(x,y,w,h,bg);return;}
  const size_t count=(size_t)w*h;
  for(size_t i=0;i<count;i++)textScratch[i]=bg;

  String text=fitText(raw,font,max(0,w-padding*2));
  int tw=textWidth(text,font);
  int cursorX=padding;
  if(align==TextAlign::CENTER)cursorX=(w-tw)/2;
  else if(align==TextAlign::RIGHT)cursorX=w-padding-tw;
  int baseline=(h-font.lineHeight)/2 + font.ascent + 1;

  size_t si=0;
  while(si<text.length()) {
    uint16_t cp=nextCodepoint(text,si);
    const UiGlyph *g=findGlyph(font,cp);
    if(!g)continue;
    int gx=cursorX+g->xOffset;
    int gy=baseline+g->yOffset;
    uint32_t pixelIndex=0;
    for(int yy=0;yy<g->height;yy++){
      int py=gy+yy;
      for(int xx=0;xx<g->width;xx++,pixelIndex++){
        int px=gx+xx;
        if(px<0||px>=w||py<0||py>=h)continue;
        uint8_t packed=font.bitmap[g->offset+(pixelIndex>>1)];
        uint8_t a=(pixelIndex&1)?(packed&0x0F):(packed>>4);
        if(a)textScratch[(size_t)py*w+px]=blend565(bg,fg,a);
      }
    }
    cursorX+=g->advance;
  }
  pushRect565(x,y,w,h,textScratch);
}

static String fmtDuration(int64_t sec) {
  if (sec < 0) return "--:--";
  uint32_t s=(uint32_t)sec;
  uint32_t h=s/3600; uint32_t m=(s%3600)/60;
  char b[16]; snprintf(b,sizeof(b),"%02lu:%02lu",(unsigned long)h,(unsigned long)m);
  return String(b);
}

static String fmtProgramDuration(uint32_t sec) {
  uint32_t h=sec/3600; uint32_t m=(sec%3600)/60;
  if(h>0) return String(h)+"h "+String(m)+"m";
  return String(m)+"m";
}

static uint16_t stateColor(){
  if(runMode==RunMode::FAULT)return RED;
  if(runMode==RunMode::DELAYED_START)return YELLOW;
  if(runMode==RunMode::PROGRAM){if(programPaused||programPhase==ProgramPhase::HOLD)return YELLOW;return GREEN;}
  if(runMode==RunMode::MANUAL_TEMP)return CYAN;
  if(runMode==RunMode::MANUAL_POWER)return YELLOW;
  return MIDGREY;
}

// ============================================================
// UI STATE
// ============================================================
static UiPage uiPage=UiPage::MAIN;
static int menuSelection=0;
static int programSelection=0;
static int actionSelection=0;
static int recoverySelection=0;
static float editManualTemp=100.0f;
static float editManualPower=25.0f;
static uint16_t editDelayMin=60;

static int lastTemp=INT32_MIN,lastTarget=INT32_MIN,lastPower=INT32_MIN;
static String lastState="#",lastInfo="#",lastTiming="#",lastRate="#",lastNet="#";

static void invalidateMain(){
  lastTemp=lastTarget=lastPower=INT32_MIN;
  lastState=lastInfo=lastTiming=lastRate=lastNet="#";
}

// ============================================================
// MAIN SCREEN
// ============================================================
static String mainInfo(){
  if(runMode==RunMode::PROGRAM&&activeProgram>=0)return programs[activeProgram].name+"  "+stepInfo();
  if(runMode==RunMode::DELAYED_START&&pendingProgram>=0)return programs[pendingProgram].name+"  DELAYED";
  if(runMode==RunMode::MANUAL_TEMP)return String("Manual temperature  ")+String((int)roundf(manualTarget))+" C";
  if(runMode==RunMode::MANUAL_POWER)return String("Manual power  ")+String((int)roundf(manualPowerRequest))+"%";
  if(runMode==RunMode::FAULT)return faultMessage;
  if(recovery.available)return String("Interrupted: ")+recovery.programName;
  return "Press encoder for menu";
}

static String timingLine(){
  if(runMode==RunMode::PROGRAM)return String("Elapsed ")+fmtDuration(elapsedRunSeconds())+"   Remaining "+fmtDuration(estimatedRemainingSeconds());
  if(runMode==RunMode::DELAYED_START)return String("Starts in ")+fmtDuration(delayedRemainingSeconds());
  if(runMode==RunMode::MANUAL_POWER)return String("Elapsed ")+fmtDuration(elapsedRunSeconds())+"   Timeout "+fmtDuration(manualPowerRemainingSeconds());
  if(runMode==RunMode::MANUAL_TEMP)return String("Elapsed ")+fmtDuration(elapsedRunSeconds());
  return "Elapsed --:--   Remaining --:--";
}

static String rateLine(){
  if(runMode==RunMode::PROGRAM){
    String actual=rateValid?String(actualRateCPerHour,0):String("--");
    String requested=programPhase==ProgramPhase::RAMP?String(requestedRampRate(),0):String("0");
    String s=String("Rate ")+actual+" / "+requested+" C/h   ETA "+etaConfidence();
    if(kilnLagging)s+="   LAGGING";
    return s;
  }
  return "Rate -- C/h";
}

static String netLine(){
  if(setupApActive)return String("Network  AP Kiln-Setup  ")+apIp();
  if(!wifiSsid.isEmpty()&&WiFi.status()==WL_CONNECTED)return String("Network  Wi-Fi  ")+stationIp();
  if(wifiSsid.isEmpty())return "Network  Wi-Fi not configured";
  return "Network  Wi-Fi retrying";
}

static void drawPowerBar(float pct){
  const int x=302,y=176,w=155,h=12;
  fillRect(x,y,w,h,DARKGREY);
  int f=constrain((int)roundf(pct*w/100.0f),0,w);
  if(f)fillRect(x,y,f,h,pct>80?RED:(pct>50?YELLOW:GREEN));
}

static void drawMainStatic(){
  fillScreen(BLACK);
  drawTextBox(18,6,150,32,"KILN",UiFontMedium,WHITE,BLACK,TextAlign::LEFT,0);
  fillRect(18,42,444,2,DARKGREY);
  drawTextBox(20,50,245,24,"Temperature",UiFontSmall,MIDGREY,BLACK);
  drawTextBox(302,50,158,24,"Target",UiFontSmall,MIDGREY,BLACK);
  drawTextBox(302,116,158,24,"Power",UiFontSmall,MIDGREY,BLACK);
  fillRect(18,196,444,2,DARKGREY);
  invalidateMain();
}

void refreshMainDisplay(bool force){
  if(uiPage!=UiPage::MAIN)return;

  String st=stateName();
  if(force||st!=lastState){
    drawTextBox(190,6,272,32,st,UiFontMedium,stateColor(),BLACK,TextAlign::RIGHT,0);
    lastState=st;
  }

  int t=(int)roundf(currentTemp);
  if(force||t!=lastTemp){
    drawTextBox(18,72,270,76,String(t)+" °C",UiFontLarge,runMode==RunMode::FAULT?RED:CYAN,BLACK,TextAlign::LEFT,0);
    lastTemp=t;
  }

  int tg=modeHasTarget()?(int)roundf(targetTemp):INT32_MIN+1;
  if(force||tg!=lastTarget){
    drawTextBox(302,75,158,38,modeHasTarget()?String((int)roundf(targetTemp))+" °C":"--",UiFontMedium,WHITE,BLACK);
    lastTarget=tg;
  }

  int p=(int)roundf(heaterPower);
  if(force||p!=lastPower){
    drawTextBox(302,139,158,36,String(p)+" %",UiFontMedium,WHITE,BLACK);
    drawPowerBar(heaterPower);
    lastPower=p;
  }

  String ti=timingLine();
  if(force||ti!=lastTiming){
    drawTextBox(18,202,444,27,ti,UiFontSmall,WHITE,BLACK);
    lastTiming=ti;
  }

  String inf=mainInfo();
  if(force||inf!=lastInfo){
    drawTextBox(18,231,444,27,String("Info  ")+inf,UiFontSmall,runMode==RunMode::FAULT?RED:WHITE,BLACK);
    lastInfo=inf;
  }

  String rl=rateLine();
  if(force||rl!=lastRate){
    drawTextBox(18,260,444,27,rl,UiFontSmall,kilnLagging?YELLOW:MIDGREY,BLACK);
    lastRate=rl;
  }

  String nl=netLine();
  if(force||nl!=lastNet){
    drawTextBox(18,290,444,25,nl,UiFontSmall,setupApActive?YELLOW:MIDGREY,BLACK);
    lastNet=nl;
  }
}

void showMainScreen(){
  uiPage=UiPage::MAIN;
  drawMainStatic();
  refreshMainDisplay(true);
}

// ============================================================
// PAGE HELPERS
// ============================================================
static void drawHeader(const String&title){
  fillScreen(BLACK);
  drawTextBox(18,8,444,34,title,UiFontMedium,WHITE,BLACK);
  fillRect(18,45,444,2,DARKGREY);
}

static void drawFooter(const String &text){
  drawTextBox(18,294,444,24,text,UiFontSmall,MIDGREY,BLACK,TextAlign::CENTER,0);
}

static int getMenuItems(String out[],int maxItems){
  int n=0;
  auto add=[&](const String&s){if(n<maxItems)out[n++]=s;};
  if(runMode==RunMode::PROGRAM){add(programPaused?"RESUME":"PAUSE / HOLD");add("DETAILS");add("STOP PROGRAM");add("BACK");return n;}
  if(runMode==RunMode::MANUAL_TEMP||runMode==RunMode::MANUAL_POWER){add("DETAILS");add("STOP");add("BACK");return n;}
  if(runMode==RunMode::DELAYED_START){add("DETAILS");add("CANCEL DELAY");add("BACK");return n;}
  if(runMode==RunMode::FAULT){add("DETAILS");add("CLEAR FAULT");add("BACK");return n;}
  if(recovery.available)add("RECOVERY");
  add("MANUAL TEMP");add("MANUAL POWER");add("PROGRAMS");add("DETAILS");
  add(setupApActive?"STOP WIFI HOTSPOT":"START WIFI HOTSPOT");add("BACK");
  return n;
}

// ============================================================
// MAIN MENU - partial row updates, no full-screen flashing
// ============================================================
static constexpr int MENU_Y0=62;
static constexpr int MENU_ROW_H=33;

static void drawMenuRow(int index){
  String items[10]; int n=getMenuItems(items,10);
  if(index<0||index>=n)return;
  int y=MENU_Y0+index*MENU_ROW_H;
  bool selected=index==menuSelection;
  drawTextBox(16,y,448,29,items[index],UiFontMedium,selected?WHITE:MIDGREY,selected?BLUE:BLACK,TextAlign::LEFT,12);
}

static void drawMenu(){
  drawHeader("MENU");
  String items[10]; int n=getMenuItems(items,10);
  menuSelection=constrain(menuSelection,0,max(0,n-1));
  for(int i=0;i<n;i++)drawMenuRow(i);
  drawFooter("Turn = move   Press = select   STOP = home");
}

static void updateMenuSelection(int oldSelection){
  if(oldSelection!=menuSelection){drawMenuRow(oldSelection);drawMenuRow(menuSelection);}
}

// ============================================================
// DETAILS
// ============================================================
static void drawDetails(){
  drawHeader("DETAILS");
  int y=58;
  auto row=[&](const String&a,const String&b){
    drawTextBox(22,y,180,29,a,UiFontSmall,MIDGREY,BLACK);
    drawTextBox(200,y,255,29,b,UiFontMedium,WHITE,BLACK,TextAlign::RIGHT,0);
    y+=29;
  };
  row("State",stateName());
  row("Elapsed",fmtDuration((runMode==RunMode::IDLE||runMode==RunMode::FAULT)?-1:elapsedRunSeconds()));
  row("Remaining",fmtDuration(estimatedRemainingSeconds()));
  row("Actual rate",rateValid?String(actualRateCPerHour,0)+" C/h":"--");
  row("Requested rate",String(requestedRampRate(),0)+" C/h");
  row("ETA confidence",etaConfidence());
  row("Power limit",String(currentStepPowerLimit(),0)+" %");
  row("Raw thermocouple",thermocoupleValid?String(rawTemp,1)+" C":"FAULT");
  drawFooter("Press or STOP = main");
}

// ============================================================
// VALUE EDITORS - only redraw the value when encoder moves
// ============================================================
static void drawEditBase(const String&title){
  drawHeader(title);
  drawTextBox(38,225,404,34,"Turn to change",UiFontMedium,WHITE,BLACK,TextAlign::CENTER,0);
  drawFooter("Press to start   STOP = home");
}

static void updateEditValue(float value,const String&unit){
  drawTextBox(50,92,380,88,String((int)roundf(value))+unit,UiFontLarge,CYAN,BLACK,TextAlign::CENTER,0);
}

static void drawEdit(const String&title,float value,const String&unit){
  drawEditBase(title);
  updateEditValue(value,unit);
}

// ============================================================
// PROGRAM BROWSER - name + nominal duration
// ============================================================
static constexpr int PROGRAM_VISIBLE_ROWS=6;
static constexpr int PROGRAM_ROW_Y0=62;
static constexpr int PROGRAM_ROW_H=36;
static int programWindowStart=0;

static int desiredProgramWindowStart(){
  if(programCount<=PROGRAM_VISIBLE_ROWS)return 0;
  int start=programSelection-PROGRAM_VISIBLE_ROWS/2;
  int maxStart=(int)programCount-PROGRAM_VISIBLE_ROWS;
  return constrain(start,0,maxStart);
}

static void drawProgramRowBySlot(int slot){
  int index=programWindowStart+slot;
  int y=PROGRAM_ROW_Y0+slot*PROGRAM_ROW_H;
  if(index<0||index>=(int)programCount){fillRect(16,y,448,32,BLACK);return;}
  bool selected=index==programSelection;
  uint16_t bg=selected?BLUE:BLACK;
  uint16_t fg=selected?WHITE:MIDGREY;
  fillRect(16,y,448,32,bg);
  String duration=fmtProgramDuration(nominalProgramDurationSeconds(index,20.0f));
  drawTextBox(26,y,320,32,programs[index].name,UiFontMedium,fg,bg,TextAlign::LEFT,0);
  drawTextBox(350,y,104,32,duration,UiFontSmall,fg,bg,TextAlign::RIGHT,0);
}

static void drawProgramList(){
  fillRect(16,PROGRAM_ROW_Y0,448,PROGRAM_VISIBLE_ROWS*PROGRAM_ROW_H,BLACK);
  for(int slot=0;slot<PROGRAM_VISIBLE_ROWS;slot++)drawProgramRowBySlot(slot);
}

static void drawProgramSelect(){
  drawHeader("PROGRAMS");
  if(programCount==0){
    drawTextBox(24,105,432,50,"No programs",UiFontMedium,RED,BLACK,TextAlign::CENTER,0);
    drawFooter("STOP = home");
    return;
  }
  programSelection=constrain(programSelection,0,(int)programCount-1);
  programWindowStart=desiredProgramWindowStart();
  drawProgramList();
  drawFooter("Turn = choose   Press = options   STOP = home");
}

static void updateProgramSelection(int oldSelection,int oldWindowStart){
  int newWindow=desiredProgramWindowStart();
  if(newWindow!=oldWindowStart){programWindowStart=newWindow;drawProgramList();return;}
  int oldSlot=oldSelection-programWindowStart;
  int newSlot=programSelection-programWindowStart;
  if(oldSlot>=0&&oldSlot<PROGRAM_VISIBLE_ROWS)drawProgramRowBySlot(oldSlot);
  if(newSlot>=0&&newSlot<PROGRAM_VISIBLE_ROWS)drawProgramRowBySlot(newSlot);
}

// ============================================================
// PROGRAM START OPTIONS
// ============================================================
static constexpr int ACTION_Y0=128;
static constexpr int ACTION_ROW_H=45;

static void drawProgramActionRow(int i){
  static const char* opts[]={"START NOW","DELAYED START","BACK"};
  int y=ACTION_Y0+i*ACTION_ROW_H;
  bool selected=i==actionSelection;
  drawTextBox(18,y,444,36,opts[i],UiFontMedium,selected?WHITE:MIDGREY,selected?BLUE:BLACK,TextAlign::LEFT,16);
}

static void drawProgramAction(){
  drawHeader("PROGRAM");
  drawTextBox(22,56,436,36,programs[programSelection].name,UiFontMedium,CYAN,BLACK);
  String d=String("Nominal duration  ")+fmtProgramDuration(nominalProgramDurationSeconds(programSelection,20.0f));
  drawTextBox(22,91,436,28,d,UiFontSmall,MIDGREY,BLACK);
  for(int i=0;i<3;i++)drawProgramActionRow(i);
  drawFooter("STOP = home");
}

static void updateProgramActionSelection(int oldSelection){
  if(oldSelection!=actionSelection){drawProgramActionRow(oldSelection);drawProgramActionRow(actionSelection);}
}

// ============================================================
// DELAYED START EDITOR
// ============================================================
static void updateDelayValue(){
  uint16_t h=editDelayMin/60,m=editDelayMin%60;
  char b[16];snprintf(b,sizeof(b),"%02u:%02u",h,m);
  drawTextBox(60,88,360,82,String(b),UiFontLarge,CYAN,BLACK,TextAlign::CENTER,0);
}

static void drawDelayEdit(){
  drawHeader("DELAYED START");
  updateDelayValue();
  drawTextBox(40,182,400,34,"Delay before start",UiFontMedium,WHITE,BLACK,TextAlign::CENTER,0);
  drawTextBox(40,224,400,30,"Turn = 15 minute steps",UiFontSmall,MIDGREY,BLACK,TextAlign::CENTER,0);
  drawFooter("Press = confirm   STOP = home");
}

// ============================================================
// POWER-FAILURE RECOVERY
// ============================================================
static constexpr int RECOVERY_Y0=140;
static constexpr int RECOVERY_ROW_H=42;

static void drawRecoveryRow(int i){
  static const char* opts[]={"RESUME","DISCARD","BACK"};
  int y=RECOVERY_Y0+i*RECOVERY_ROW_H;
  bool selected=i==recoverySelection;
  drawTextBox(18,y,444,34,opts[i],UiFontMedium,selected?WHITE:MIDGREY,selected?BLUE:BLACK,TextAlign::LEFT,16);
}

static void drawRecoveryAction(){
  drawHeader("POWER RECOVERY");
  drawTextBox(22,56,436,34,recovery.programName,UiFontMedium,CYAN,BLACK);
  drawTextBox(22,92,436,30,String("Elapsed  ")+fmtDuration(recovery.runElapsedSec),UiFontSmall,MIDGREY,BLACK);
  for(int i=0;i<3;i++)drawRecoveryRow(i);
  drawFooter("Heater remains off until Resume is selected");
}

static void updateRecoverySelection(int oldSelection){
  if(oldSelection!=recoverySelection){drawRecoveryRow(oldSelection);drawRecoveryRow(recoverySelection);}
}

static void enterMenu(){
  uiPage=UiPage::MENU;
  menuSelection=0;
  drawMenu();
}

static void selectMenuItem(){
  String items[10];int n=getMenuItems(items,10);
  if(menuSelection<0||menuSelection>=n)return;
  String item=items[menuSelection];
  if(item=="PAUSE / HOLD"){pauseProgram();showMainScreen();return;}
  if(item=="RESUME"){resumeProgram();showMainScreen();return;}
  if(item=="DETAILS"){uiPage=UiPage::DETAILS;drawDetails();return;}
  if(item=="STOP PROGRAM"||item=="STOP"||item=="CANCEL DELAY"){stopControlByUser();showMainScreen();return;}
  if(item=="CLEAR FAULT"){clearFault();showMainScreen();return;}
  if(item=="RECOVERY"){uiPage=UiPage::RECOVERY_ACTION;recoverySelection=0;drawRecoveryAction();return;}
  if(item=="MANUAL TEMP"){editManualTemp=manualTarget;uiPage=UiPage::EDIT_MANUAL_TEMP;drawEdit("MANUAL TEMP",editManualTemp," °C");return;}
  if(item=="MANUAL POWER"){editManualPower=manualPowerRequest;uiPage=UiPage::EDIT_MANUAL_POWER;drawEdit("MANUAL POWER",editManualPower," %");return;}
  if(item=="PROGRAMS"){uiPage=UiPage::PROGRAM_SELECT;drawProgramSelect();return;}
  if(item=="START WIFI HOTSPOT"||item=="STOP WIFI HOTSPOT"){
    if(setupApActive)stopSetupAP();else startSetupAP();
    drawMenuRow(menuSelection);
    return;
  }
  showMainScreen();
}

void initLocalUi(){
  pinMode(TFT_RS,OUTPUT);pinMode(TFT_WR,OUTPUT);pinMode(TFT_RD,OUTPUT);pinMode(TFT_CS,OUTPUT);pinMode(TFT_RST,OUTPUT);
  dataBusOutput();
  digitalWrite(TFT_RD,HIGH);digitalWrite(TFT_WR,HIGH);digitalWrite(TFT_CS,HIGH);

  textScratch=(uint16_t*)heap_caps_malloc(TEXT_SCRATCH_W*TEXT_SCRATCH_H*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!textScratch)textScratch=(uint16_t*)malloc(TEXT_SCRATCH_W*TEXT_SCRATCH_H*sizeof(uint16_t));
  if(!textScratch)Serial.println("WARNING: smooth text buffer allocation failed");

  initILI9486();
  showMainScreen();
}

void updateLocalUi(){
  // STOP is always safety-first. If heat/delay is active it aborts it;
  // otherwise it is a global Home key.
  if(consumeStopPress()){
    if(runMode==RunMode::PROGRAM||runMode==RunMode::MANUAL_TEMP||runMode==RunMode::MANUAL_POWER||runMode==RunMode::DELAYED_START)stopControlByUser();
    showMainScreen();
    return;
  }

  int32_t delta=consumeEncoderDelta();
  if(delta!=0){
    if(uiPage==UiPage::MENU){
      String items[10];int n=getMenuItems(items,10);
      int old=menuSelection;
      menuSelection=(menuSelection+delta)%n;if(menuSelection<0)menuSelection+=n;
      updateMenuSelection(old);
    }
    else if(uiPage==UiPage::EDIT_MANUAL_TEMP){
      editManualTemp=constrain(editManualTemp+delta,0.0f,settings.maxTemp);
      updateEditValue(editManualTemp," °C");
    }
    else if(uiPage==UiPage::EDIT_MANUAL_POWER){
      editManualPower=constrain(editManualPower+delta,0.0f,settings.manualPowerMax);
      updateEditValue(editManualPower," %");
    }
    else if(uiPage==UiPage::PROGRAM_SELECT&&programCount){
      int old=programSelection;
      int oldWindow=programWindowStart;
      programSelection=(programSelection+delta)%(int)programCount;if(programSelection<0)programSelection+=programCount;
      updateProgramSelection(old,oldWindow);
    }
    else if(uiPage==UiPage::PROGRAM_ACTION){
      int old=actionSelection;
      actionSelection=(actionSelection+delta)%3;if(actionSelection<0)actionSelection+=3;
      updateProgramActionSelection(old);
    }
    else if(uiPage==UiPage::DELAY_EDIT){
      int v=(int)editDelayMin+(int)delta*DELAY_INCREMENT_MIN;
      v=constrain(v,(int)DELAY_INCREMENT_MIN,(int)MAX_DELAY_MIN);
      editDelayMin=(uint16_t)v;
      updateDelayValue();
    }
    else if(uiPage==UiPage::RECOVERY_ACTION){
      int old=recoverySelection;
      recoverySelection=(recoverySelection+delta)%3;if(recoverySelection<0)recoverySelection+=3;
      updateRecoverySelection(old);
    }
  }

  if(!consumeEncoderPress())return;
  switch(uiPage){
    case UiPage::MAIN:enterMenu();break;
    case UiPage::MENU:selectMenuItem();break;
    case UiPage::DETAILS:showMainScreen();break;
    case UiPage::EDIT_MANUAL_TEMP:startManualTemp(editManualTemp);showMainScreen();break;
    case UiPage::EDIT_MANUAL_POWER:startManualPower(editManualPower);showMainScreen();break;
    case UiPage::PROGRAM_SELECT:
      if(programCount){uiPage=UiPage::PROGRAM_ACTION;actionSelection=0;drawProgramAction();}
      break;
    case UiPage::PROGRAM_ACTION:
      if(actionSelection==0){startProgramNow(programSelection);showMainScreen();}
      else if(actionSelection==1){editDelayMin=60;uiPage=UiPage::DELAY_EDIT;drawDelayEdit();}
      else {uiPage=UiPage::PROGRAM_SELECT;drawProgramSelect();}
      break;
    case UiPage::DELAY_EDIT:scheduleDelayedProgram(programSelection,editDelayMin);showMainScreen();break;
    case UiPage::RECOVERY_ACTION:
      if(recoverySelection==0){resumeRecoveryProgram();showMainScreen();}
      else if(recoverySelection==1){discardRecoveryToHistory();showMainScreen();}
      else {enterMenu();}
      break;
  }
}
