// Technics RS-B205: radio MP3 na rdzeniu 0, animacja i ekran na rdzeniu 1.
// ESP32-S3 N16R8 + Waveshare 13587 ILI9486 SPI-16 + PCM5102A.
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <esp_heap_caps.h>
#include "board_pins.h"
#include "waveshare_ili9486.h"
#include <math.h>
#include "reel_artwork.h"
#include "radio_audio.h"
#include "station_hand_text.h"
#include "stations.h"
#include "magnet_mqtt.h"
#include "dlna_renderer.h"
#include "rim_coverage.h"
#include "hub_coverage.h"

#if defined(ARDUINO_ARCH_ESP32)
#pragma GCC optimize ("O3")
#endif

WaveshareILI9486 tft;

constexpr int SCREEN_W=480, SCREEN_H=320, HALF_W=SCREEN_W/2;
// Physical 43 mm window in the 56.5 mm-high active display.
constexpr int VIEW_H=244, VIEW_Y=19;
constexpr uint16_t PANEL_MARGIN_COLOR=0x73ae;
constexpr int CROP_Y=(SCREEN_H-VIEW_H)/2; // preserve the original crop when moving it
constexpr int LABEL_W=HandStationTextRenderer::W,LABEL_H=HandStationTextRenderer::H;
constexpr int LABEL_X=(SCREEN_W-LABEL_W)/2,LABEL_Y=VIEW_Y+5;
constexpr int RATE_X=HALF_W-32,RATE_Y=VIEW_Y+VIEW_H-17;
static_assert(LABEL_Y>=VIEW_Y&&LABEL_Y+LABEL_H<=VIEW_Y+VIEW_H,"Station signature must fit the visible window");
static_assert(RATE_Y>=LABEL_Y+LABEL_H,"Bitrate must stay below the signature");
constexpr int STRIP_H = 8;
constexpr uint32_t FRAME_MS = 1000 / 30;
constexpr double SIDE_MS = 30.0 * 60.0 * 1000.0;
constexpr float TAPE_SPEED_SCENE=47.6f*320.f/100.4f;
// Ping-pong internal DMA strips: render while the previous strip is sent.
DMA_ATTR uint16_t stripStorage[2][SCREEN_W * STRIP_H];
uint16_t *strip=stripStorage[0];
uint8_t stripIndex=0;
// Hot ink cache in internal RAM; PSRAM fallback if allocation fails.
uint8_t *previousInk=nullptr;
uint8_t *artwork=nullptr;
float sceneX[SCREEN_W], sceneY[VIEW_H];
uint32_t columnDistance2[SCREEN_W], rowDistance2[VIEW_H];
int32_t columnShade[SCREEN_W], rowShade[VIEW_H];
uint16_t metalPalette[256], inkPalette[11];
int16_t spinLeft[2][VIEW_H], spinRight[2][VIEW_H];
uint16_t tapeScanRow=0;
uint16_t refreshRow=0;
uint8_t refreshFrame=0;
uint32_t tapePainted[2][VIEW_H]={};
uint32_t spiHz=20000000;
constexpr float ZOOM=100.4f/84.3f*SCREEN_W/312;

struct Reel {
  float x, radius2, cosine, sine;
};
Reel reels[2];
constexpr int ATLAS_PHASES=128;
struct InkRun {uint8_t left,right,ink;};
struct RunFrame {uint16_t *rows=nullptr;InkRun *runs=nullptr;};
struct Atlas {int left=0,top=0,width=0,height=0;size_t bytes=0;uint8_t *data=nullptr,*frame=nullptr,*cache=nullptr;int phase=-1,previousPhase=-1;RunFrame ranges[ATLAS_PHASES],cachedRuns[2];int cacheIndex=0;};
Atlas atlas[2];
inline uint8_t frameInk(int part,int x,int row,int32_t u,int32_t v) {
  const Atlas &a=atlas[part];
  if(a.frame) {
    int xx=x-a.left,yy=row-a.top;
    if(unsigned(xx)>=unsigned(a.width)||unsigned(yy)>=unsigned(a.height)) return 0;
    int i=yy*a.width+xx;uint8_t b=a.frame[i/2];return i&1?b>>4:b&15;
  }
  int i=(v>>16)*216+(u>>16);uint8_t b=artwork[i/2];return i&1?b>>4:b&15;
}
void buildAtlas() {
  uint32_t start=millis();size_t total=0;
  for(int part=0;part<2;part++) {
    Atlas &a=atlas[part];int right=0,bottom=0;a.left=SCREEN_W;a.top=VIEW_H;
    for(int y=0;y<VIEW_H;y++)if(spinLeft[part][y]>=0){a.left=min(a.left,int(spinLeft[part][y]));right=max(right,int(spinRight[part][y]));a.top=min(a.top,y);bottom=max(bottom,y);}
    a.width=(right-a.left+2)&~1;a.height=bottom-a.top+1;a.bytes=a.width*a.height/2;
    a.data=static_cast<uint8_t *>(heap_caps_malloc(a.bytes*ATLAS_PHASES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!a.data){Serial.println("BLAD: ATLAS allocation failed");while(true)delay(1000);}
    total+=a.bytes*ATLAS_PHASES;
    uint8_t rowBuffer[HALF_W/2+1];
    for(int phase=0;phase<ATLAS_PHASES;phase++){
      float angle=phase*(TWO_PI/ATLAS_PHASES),c=cosf(angle),sn=sinf(angle);
      int32_t du=int32_t(c*(104857.6f/ZOOM)),dv=int32_t(-sn*(104857.6f/ZOOM));
      for(int y=a.top;y<a.top+a.height;y++){
        float dx=sceneX[part*HALF_W]-(part?232:88),dy=sceneY[y]-120;
        int32_t u=int32_t((dx*c+dy*sn+67.5f)*104857.6f)+du*(a.left-part*HALF_W);
        int32_t v=int32_t((-dx*sn+dy*c+67.5f)*104857.6f)+dv*(a.left-part*HALF_W);
        for(int xx=0;xx<a.width;xx+=2){uint8_t packed=0;
          for(int bit=0;bit<2;bit++,u+=du,v+=dv){int x=a.left+xx+bit;
            if(x<=right && columnDistance2[x]+rowDistance2[y]<=uint32_t(60.8f*60.8f*65536)){
              int i=(v>>16)*216+(u>>16);uint8_t b=artwork[i/2],ink=i&1?b>>4:b&15;packed|=ink<<(bit*4);
            }
          }
          rowBuffer[xx/2]=packed;
        }
        memcpy(a.data+phase*a.bytes+(y-a.top)*a.width/2,rowBuffer,a.width/2);
      }
      if(!(phase&7))delay(1);
    }
    a.frame=a.data;
  }
  for(auto &a:atlas)for(int phase=0;phase<ATLAS_PHASES;phase++){
    RunFrame &f=a.ranges[phase];
    f.rows=static_cast<uint16_t *>(heap_caps_malloc((a.height+1)*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!f.rows){Serial.println("BLAD: RLE rows");while(true)delay(1000);}
    const uint8_t *data=a.data+phase*a.bytes;int count=0;
    for(int pass=0;pass<2;pass++){
      count=0;
      for(int y=0;y<a.height;y++){
        f.rows[y]=count;
        int left=-1,kind=0;
        for(int x=0;x<=a.width;x++){
          int ink=0;
          if(x<a.width){int i=y*a.width+x;uint8_t b=data[i/2];ink=i&1?b>>4:b&15;if(ink<7)ink=0;}
          if(ink!=kind){
            if(kind){if(pass)f.runs[count]={uint8_t(left),uint8_t(x-1),uint8_t(kind)};count++;}
            kind=ink;left=x;
          }
        }
      }
      f.rows[a.height]=count;
      if(!pass){f.runs=static_cast<InkRun *>(heap_caps_malloc(max(1,count)*sizeof(InkRun),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!f.runs){Serial.println("BLAD: RLE runs");while(true)delay(1000);}}
    }
    if(!(phase&7))delay(1);
  }
  free(artwork);artwork=const_cast<uint8_t *>(REEL_ARTWORK);
  for(auto &a:atlas){
    a.cache=static_cast<uint8_t *>(heap_caps_malloc(a.bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    int maxRuns=0;for(auto &f:a.ranges)maxRuns=max(maxRuns,int(f.rows[a.height]));
    for(auto &f:a.cachedRuns){
      f.rows=static_cast<uint16_t *>(heap_caps_malloc((a.height+1)*sizeof(uint16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
      f.runs=static_cast<InkRun *>(heap_caps_malloc(maxRuns*sizeof(InkRun),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
      if(!f.rows||!f.runs){Serial.println("BLAD: internal RLE cache");while(true)delay(1000);}
    }
  }
  Serial.printf("ATLAS %u bytes, %u phases, built %lums, PSRAM free %u\n",unsigned(total),ATLAS_PHASES,(unsigned long)(millis()-start),ESP.getFreePsram());
}

void selectAtlasPhase(int part,int phase){
  Atlas &a=atlas[part];a.previousPhase=a.phase;
  if(a.phase==phase)return;
  a.frame=a.data+phase*a.bytes;
  if(a.cache){memcpy(a.cache,a.frame,a.bytes);a.frame=a.cache;}
  a.cacheIndex^=1;RunFrame &dst=a.cachedRuns[a.cacheIndex],&src=a.ranges[phase];
  memcpy(dst.rows,src.rows,(a.height+1)*sizeof(uint16_t));
  memcpy(dst.runs,src.runs,src.rows[a.height]*sizeof(InkRun));
  a.phase=phase;
}
float angle[2] = {0, 0};
double positionMs = 0;
float timeSpeed = 1;
bool playing = true;
bool colorTest=false;
bool reverseSide=false, refreshAll=false;
uint32_t sideChanges=0;
GFXcanvas1 streamLabel(64,8);
HandStationTextRenderer stationText;
struct LabelSpan {int16_t left,right;};
LabelSpan stationInk[LABEL_H];
int stationInkHeight=0,stationEraseHeight=0;
bool stationLabelDirty=true,readingLabel=false,labelOverflow=false;
char labelInput[96]={0};uint8_t labelInputLength=0;
bool receiving=false;
uint32_t displayedBitrate=0;
uint32_t previousMs = 0, previousFrame = 0, reportMs = 0;
uint32_t frames = 0, drawTotalUs = 0, maxDrawUs = 0;
uint32_t profileCpu=0,profileSpi=0,profileCalls=0,profilePixels=0,profilePhase=0,profileFinish=0;

constexpr uint16_t rgb(int r, int g, int b) {
  uint16_t native=((r & 248) << 8) | ((g & 252) << 3) | ((b & 248) >> 3);
  return (native>>8)|(native<<8); // Gotowa kolejność bajtów dla SPI.
}

constexpr uint16_t backgroundColor() {return rgb(112,116,112);}

uint16_t blendWhite4(uint16_t bigEndianColor,uint8_t coverage) {
  if(!coverage) return bigEndianColor;
  if(coverage==15) return rgb(255,255,255);
  uint16_t native=(bigEndianColor>>8)|(bigEndianColor<<8);
  int r=(native>>11)&31, g=(native>>5)&63, b=native&31;
  r+=((31-r)*coverage+7)/15;
  g+=((63-g)*coverage+7)/15;
  b+=((31-b)*coverage+7)/15;
  uint16_t mixed=(r<<11)|(g<<5)|b;
  return (mixed>>8)|(mixed<<8);
}

uint16_t blendStationRed4(uint16_t color, uint8_t coverage) {
  if (!coverage) return color;
  if (coverage == 15) return rgb(255, 0, 0);
  const uint16_t native = (color >> 8) | (color << 8);
  const unsigned remaining = 15 - coverage;
  const unsigned red = (((native >> 11) & 31) * remaining + 31 * coverage + 7) / 15;
  const unsigned green = (((native >> 5) & 63) * remaining + 7) / 15;
  const unsigned blue = ((native & 31) * remaining + 7) / 15;
  const uint16_t mixed = (red << 11) | (green << 5) | blue;
  return (mixed >> 8) | (mixed << 8);
}

bool setStationLabel(const char *utf8) {
  if(!stationText.render(utf8)) return false;
  stationEraseHeight=max(stationEraseHeight,stationInkHeight);
  stationInkHeight=0;
  for(int y=0;y<LABEL_H;y++){
    LabelSpan &span=stationInk[y];
    span={LABEL_W,-1};
    for(int x=0;x<LABEL_W;x++)if(stationText.alpha(x,y)){
      span.left=min<int>(span.left,x);span.right=x;
      stationInkHeight=y+1;
    }
  }
  stationEraseHeight=max(stationEraseHeight,stationInkHeight);
  stationLabelDirty=true;
  return true;
}

void compositeRims(int left,int top,int width,int height) {
  const uint16_t dark=rgb(94,106,98),outer=rgb(99,111,102);
  for(int sy=0;sy<height;sy++) {
    int row=top+sy;
    for(int i=0;i<4;i++) {
      const RimSpan &span=RIM_SPANS[row][i];
      if(span.left<0) continue;
      int start=max(left,int(span.left)),end=min(left+width,int(span.right)+1);
      for(int x=start;x<end;x++) {
        const Reel &reel=reels[x<HALF_W?0:1];
        uint32_t d2=columnDistance2[x]+rowDistance2[row];
        uint16_t under=d2<uint32_t(reel.radius2*65536)?rgb(40,30,24):backgroundColor();
        uint16_t colors[5]={metalPalette[constrain((columnShade[x]+rowShade[row])>>8,130,228)],inkPalette[1],dark,outer,under};
        uint32_t weights=pgm_read_dword(RIM_WEIGHTS+span.offset+x-span.left);
        unsigned r=0,g=0,b=0;
        for(int band=0;band<5;band++,weights>>=5) {
          unsigned weight=weights&31;
          if(!weight) continue;
          uint16_t color=(colors[band]>>8)|(colors[band]<<8);
          r+=((color>>11)&31)*weight;g+=((color>>5)&63)*weight;b+=(color&31)*weight;
        }
        uint16_t native=(((r+8)>>4)<<11)|(((g+8)>>4)<<5)|((b+8)>>4);
        strip[sy*width+x-left]=(native>>8)|(native<<8);
      }
    }
  }
}

void compositeHubRims(int left,int top,int width,int height){
  for(int sy=0;sy<height;sy++){
    int row=top+sy;
    for(int i=0;i<4;i++){
      const HubSpan &span=HUB_SPANS[row][i];if(span.left<0)continue;
      int start=max(left,int(span.left)),end=min(left+width,int(span.right)+1);
      for(int x=start;x<end;x++){
        uint16_t colors[]={inkPalette[4],inkPalette[3],metalPalette[constrain((columnShade[x]+rowShade[row])>>8,130,228)]};
        unsigned weights=pgm_read_word(HUB_WEIGHTS+span.offset+x-span.left),r=0,g=0,b=0;
        for(int band=0;band<3;band++,weights>>=5){unsigned w=weights&31;uint16_t c=(colors[band]>>8)|(colors[band]<<8);r+=((c>>11)&31)*w;g+=((c>>5)&63)*w;b+=(c&31)*w;}
        uint16_t c=(((r+8)>>4)<<11)|(((g+8)>>4)<<5)|((b+8)>>4);
        strip[sy*width+x-left]=(c>>8)|(c<<8);
      }
    }
  }
}

void updateMetalPalette() {
  for(int i=0;i<256;i++) {
    metalPalette[i]=reverseSide?rgb(min(i+30,255),min(i+5,255),i/6):rgb(i,min(i+2,255),max(i-3,0));
  }
  inkPalette[1]=reverseSide?rgb(245,220,110):rgb(217,223,213);
}

bool updateOverlayState() {
  bool next=radioIsReceiving();
  uint32_t rate=next?audioBitrate.load(std::memory_order_relaxed):0;
  bool changed=next!=receiving || rate!=displayedBitrate;
  if(changed) {
    receiving=next;displayedBitrate=rate;
    inkPalette[9]=receiving?rgb(0,255,0):rgb(255,0,0);
    streamLabel.fillScreen(0);
    if(rate) {
      char text[20];snprintf(text,sizeof(text),"%lukbit",(unsigned long)rate);
      streamLabel.setTextSize(1);streamLabel.setTextColor(1);
      streamLabel.setCursor((64-int(strlen(text))*6)/2,0);
      streamLabel.print(text);
    }
  }
  return changed;
}

void compositeLabels(int left,int top,int width,int height) {
  for(int sy=0;sy<height;sy++){
    int screenY=top+VIEW_Y+sy;
    if(screenY>=LABEL_Y&&screenY<LABEL_Y+stationInkHeight){
      const LabelSpan &span=stationInk[screenY-LABEL_Y];
      int start=max(left,LABEL_X+span.left),end=min(left+width,LABEL_X+span.right+1);
      for(int x=start;x<end;x++){
        uint8_t a=stationText.alpha(x-LABEL_X,screenY-LABEL_Y);
        if(a)strip[sy*width+x-left]=blendStationRed4(strip[sy*width+x-left],a);
      }
    }
    if(screenY>=RATE_Y&&screenY<RATE_Y+8){
      int start=max(left,RATE_X),end=min(left+width,RATE_X+64);
      for(int x=start;x<end;x++)if(streamLabel.getPixel(x-RATE_X,screenY-RATE_Y))strip[sy*width+x-left]=rgb(240,240,240);
    }
  }
}

void drawRegion(int left, int top, int width, int height,bool applyOverlays=true) {
  for (int y=top;y<top+height;y+=STRIP_H) {
    int h=min(STRIP_H,top+height-y);
    uint32_t cpuStart=micros();
    for (int sy=0;sy<h;sy++) {
      int row=y+sy;uint32_t rowD2=rowDistance2[row];
      for(int part=0;part<2;part++) {
        int start=max(left,part*HALF_W),end=min(left+width,(part+1)*HALF_W);
        if(start>=end)continue;
        const Atlas &a=atlas[part];
        int yy=row-a.top;
        const uint8_t *inkRow=unsigned(yy)<unsigned(a.height)?a.frame+yy*(a.width/2):nullptr;
        uint32_t tape2=uint32_t(reels[part].radius2*65536);
        for(int x=start;x<end;x++) {
          uint32_t d2=columnDistance2[x]+rowD2;
          uint16_t color=d2<tape2?rgb(40,30,24):backgroundColor();
          if(d2>4356UL*65536&&d2<4489UL*65536)color=rgb(99,111,102);
          if(d2<=4356UL*65536) {
            int xx=x-a.left;uint8_t ink=0;
            if(inkRow&&unsigned(xx)<unsigned(a.width)){
              uint8_t packed=inkRow[xx>>1];ink=xx&1?packed>>4:packed&15;
            }
            if(ink==0)color=metalPalette[constrain((columnShade[x]+rowShade[row])>>8,130,228)];
            else if(ink==8)color=d2<=tape2?rgb(35,27,22):backgroundColor();
            else color=inkPalette[ink];
          }
          strip[sy*width+x-left]=color;
        }
      }
    }
    if(applyOverlays) {
      compositeRims(left,y,width,h);
      compositeHubRims(left,y,width,h);
      compositeLabels(left,y,width,h);
    }
    profileCpu+=micros()-cpuStart;
    uint32_t spiStart=micros();
    tft.setAddrWindow(left,VIEW_Y+y,width,h);
    tft.writePixels(strip,width*h,true,true);
    profileSpi+=micros()-spiStart;profileCalls++;profilePixels+=width*h;
    stripIndex^=1;strip=stripStorage[stripIndex];

  }
}

// Exact old/new stencil comparison: draw changed edge pixels, joining gaps
// of at most 8 pixels to keep the number of SPI address-window commands low.

inline uint8_t rotatingInk(int32_t u,int32_t v) {
  // Every sampled point has r<=60.8; the 216px artwork covers r<=67.5.
  int index=(v>>16)*216+(u>>16);
  uint8_t packed=artwork[index/2];
  return (index&1)?packed>>4:packed&15;
}

void drawMovingReel(int part,bool forceLogo) {
  if(atlas[part].phase==atlas[part].previousPhase&&!forceLogo)return;
  // Merge close runs across up to 8 rows, balancing pixels and window setup.
  // Flush completed rectangles at each band so the strip buffer bounds hold.
  struct Rect {int16_t left,right,top,bottom;};
  Rect rects[240];
  for(int band=0;band<VIEW_H;band+=STRIP_H) {
    int count=0;
    for(int row=band;row<min(VIEW_H,band+STRIP_H);row++) {
      Atlas &a=atlas[part];
      if(row<a.top||row>=a.top+a.height) continue;
      int yy=row-a.top;
      RunFrame &cur=a.cachedRuns[a.cacheIndex],&old=a.cachedRuns[(a.previousPhase<0||a.previousPhase==a.phase)?a.cacheIndex:(a.cacheIndex^1)];
      int ci=cur.rows[yy],ce=cur.rows[yy+1],oi=old.rows[yy],oe=old.rows[yy+1];
      int x=0,left=-1,last=-1;
      while(x<=a.width) {
        while(ci<ce&&cur.runs[ci].right<x)ci++;
        while(oi<oe&&old.runs[oi].right<x)oi++;
        int next=a.width,now=0,before=0;
        if(ci<ce){const InkRun &run=cur.runs[ci];if(x>=run.left){now=run.ink;next=min(next,int(run.right)+1);}else next=min(next,int(run.left));}
        if(oi<oe){const InkRun &run=old.runs[oi];if(x>=run.left){before=run.ink;next=min(next,int(run.right)+1);}else next=min(next,int(run.left));}
        bool changed=x<a.width&&(now!=before||(forceLogo&&now==9));
        if(changed){if(left<0)left=x;last=next-1;}
        if(left>=0&&((!changed&&next-last>12)||x==a.width)) {
          int right=min(min(a.width-1,last),(part+1)*HALF_W-1-a.left);left+=a.left;right+=a.left;
          int chosen=-1,best=100000;
          for(int i=0;i<count;i++) {
            const Rect &q=rects[i];
            if(q.bottom!=row || left>q.right+2 || right<q.left-2) continue;
            int w=max(right,int(q.right))-min(left,int(q.left))+1;
            int old=(q.right-q.left+1)*(q.bottom-q.top);
            int area=w*(row+1-q.top),extra=area-old-(right-left+1);
            if(extra<=18 && extra<best){chosen=i;best=extra;}
          }
          if(chosen>=0) {
            Rect &q=rects[chosen];q.left=min(int(q.left),left);q.right=max(int(q.right),right);q.bottom=row+1;
          } else if(count<240) rects[count++]={int16_t(left),int16_t(right),int16_t(row),int16_t(row+1)};
          else drawRegion(left,row,right-left+1,1);
          left=-1;
        }
        if(x==a.width)break;
        x=next;
      }
    }
    for(int i=0;i<count;i++) {
      const Rect &q=rects[i];drawRegion(q.left,q.top,q.right-q.left+1,q.bottom-q.top);
    }
  }
}

void updateTapeEdges(){
  int bottom=min(VIEW_H,int(tapeScanRow)+STRIP_H);
  for(int part=0;part<2;part++){
    uint32_t now=uint32_t(reels[part].radius2*65536);const Atlas &a=atlas[part];
    for(int row=tapeScanRow;row<bottom;row++){
      uint32_t before=tapePainted[part][row];tapePainted[part][row]=now;
      if(before==now||row<a.top||row>=a.top+a.height)continue;
      const RunFrame &f=a.cachedRuns[a.cacheIndex];int yy=row-a.top;
      for(int i=f.rows[yy];i<f.rows[yy+1];i++){
        const InkRun &run=f.runs[i];if(run.ink!=8)continue;
        int left=-1;
        for(int xx=run.left;xx<=run.right+1;xx++){
          int x=a.left+xx;bool dirty=false;
          if(xx<=run.right&&x<(part+1)*HALF_W){uint32_t d2=columnDistance2[x]+rowDistance2[row];dirty=(d2<=before)!=(d2<=now);}
          if(dirty){if(left<0)left=x;}
          else if(left>=0){drawRegion(left,row,x-left,1);left=-1;}
        }
      }
    }
  }
  tapeScanRow=bottom>=VIEW_H?0:bottom;
}

void drawFrame(bool full = false) {
  bool nameChanged=stationLabelDirty;
  bool labelChanged=updateOverlayState()||nameChanged;
  stationLabelDirty=false;
  full=full||refreshAll;
  refreshAll=false;
  float p = float(positionMs / SIDE_MS);
  reels[0] = {88, 625 + 3731*(1-p), cosf(angle[0]), sinf(angle[0])};
  reels[1] = {232, 625 + 3731*p, cosf(angle[1]), sinf(angle[1])};
  uint32_t phaseStart=micros();
  for(int part=0;part<2;part++)if(atlas[part].data) {
    int phase=int(lroundf(angle[part]*(ATLAS_PHASES/TWO_PI)))&(ATLAS_PHASES-1);
    selectAtlasPhase(part,phase);
  }
  profilePhase+=micros()-phaseStart;
  tft.startWrite();
  if(full) {
    drawRegion(0,0,SCREEN_W,VIEW_H);
    for(int part=0;part<2;part++)for(int row=0;row<VIEW_H;row++)tapePainted[part][row]=uint32_t(reels[part].radius2*65536);
  } else {
    // Obraca się tylko piasta, nadruk i wycięcia do promienia 60.
    // Zewnętrzny metalowy pierścień jest wizualnie nieruchomy.
    for(int part=0;part<2;part++) drawMovingReel(part,labelChanged);
    updateTapeEdges();
  }
  if(labelChanged && !full) drawRegion(RATE_X,RATE_Y-VIEW_Y,64,8);
  if(nameChanged&&!full)drawRegion(LABEL_X,LABEL_Y-VIEW_Y,LABEL_W,stationEraseHeight);
  // Sweep physical LCD rows, including the two margins outside the cassette.
  if(full){refreshRow=0;refreshFrame=0;}
  else if(++refreshFrame==4){
    const int row=refreshRow;
    const int viewEnd=VIEW_Y+VIEW_H;
    const bool inView=row>=VIEW_Y&&row<viewEnd;
    const int boundary=inView?viewEnd:(row<VIEW_Y?VIEW_Y:SCREEN_H);
    const int rows=min(STRIP_H,boundary-row);
    if(inView)drawRegion(0,row-VIEW_Y,SCREEN_W,rows);
    else {
      constexpr uint16_t wireColor=uint16_t((PANEL_MARGIN_COLOR>>8)|(PANEL_MARGIN_COLOR<<8));
      const uint32_t pixels=SCREEN_W*rows;
      const uint32_t cpuStart=micros();
      for(uint32_t i=0;i<pixels;i++)strip[i]=wireColor;
      profileCpu+=micros()-cpuStart;
      const uint32_t spiStart=micros();
      tft.setAddrWindow(0,row,SCREEN_W,rows);
      tft.writePixels(strip,pixels,true,true);
      profileSpi+=micros()-spiStart;profileCalls++;profilePixels+=pixels;
      stripIndex^=1;strip=stripStorage[stripIndex];
    }
    refreshRow=row+rows==SCREEN_H?0:row+rows;
    refreshFrame=0;
  }
  stationEraseHeight=0;
  uint32_t finishStart=micros();tft.endWrite();

  profileFinish+=micros()-finishStart;
  yield();
}

void setup() {
  Serial.begin(115200);
  // The display loop must never wait for a host that has stopped reading USB.
  Serial.setTxTimeoutMs(0);
  Serial.println("\nTechnics S3 v1: Waveshare ILI9486 480x320; duze szpule r67, tasma r<=66; AA obwodow, dynamiczny font UTF-8; cel 30 FPS.");
  Serial.printf("RAM przed TFT: %u; PSRAM: %u\n",ESP.getFreeHeap(),ESP.getPsramSize());
  artwork=static_cast<uint8_t *>(heap_caps_malloc(sizeof(REEL_ARTWORK),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!artwork){Serial.println("BLAD: brak RAM artwork");while(true)delay(1000);}
  memcpy(artwork,REEL_ARTWORK,sizeof(REEL_ARTWORK));
  // Join Wi-Fi and announce the renderer while the display initializes.
  startRadioTask();
  startMagnetHA();
  startDLNARenderer();
  tft.begin(1000000);
  tft.setRotation(1);
  tft.fillScreen(PANEL_MARGIN_COLOR);
  Serial.println("LCD: Waveshare shield, commands 8-bit / params 16-bit, no LCD readback.");
  setStationLabel(STATION_PRESETS[1].speak);
  streamLabel.fillScreen(0);
  tft.setSPISpeed(spiHz);
  // Ta sama geometria kadru co w zaakceptowanym podglądzie.
  for(int x=0;x<SCREEN_W;x++) {
    sceneX[x]=(x+.5f-HALF_W)/ZOOM+160;
    float dx=sceneX[x]-(x<HALF_W?88:232);
    int32_t q=int32_t(dx*256);
    columnDistance2[x]=uint32_t(q*q);
    columnShade[x]=int32_t((187-.35f*dx)*256);
  }
  for(int y=0;y<VIEW_H;y++) {
    sceneY[y]=(y+CROP_Y+.5f-SCREEN_H/2)/ZOOM+121;
    float dy=sceneY[y]-120;
    int32_t q=int32_t(dy*256);
    rowDistance2[y]=uint32_t(q*q);
    rowShade[y]=int32_t(-.5f*dy*256);
  }
  // Granice fragmentów obrotowych wyznaczone raz, zamiast wysyłania
  // prostokątów z wieloma niezmieniającymi się pikselami.
  for(int part=0;part<2;part++) {
    for(int y=0;y<VIEW_H;y++) {
      spinLeft[part][y]=spinRight[part][y]=-1;
      for(int x=part*HALF_W;x<(part+1)*HALF_W;x++) {
        if(columnDistance2[x]+rowDistance2[y]<=uint32_t(60.8f*60.8f*65536)) {
          if(spinLeft[part][y]<0) spinLeft[part][y]=x;
          spinRight[part][y]=x;
        }
      }
    }
  }
  buildAtlas();
  updateMetalPalette();
  inkPalette[2]=rgb(94,106,98);
  inkPalette[3]=rgb(211,217,211);inkPalette[4]=rgb(137,145,140);
  inkPalette[5]=rgb(48,55,51);inkPalette[6]=rgb(7,10,9);
  inkPalette[7]=rgb(144,153,146);inkPalette[9]=rgb(255,0,0);
  inkPalette[10]=rgb(24,25,22);
  Serial.printf("Pole obrazu: %dx%d, Y=%d, SPI=%lu; bufor=%u B\n",SCREEN_W,VIEW_H,VIEW_Y,(unsigned long)spiHz,unsigned(sizeof(stripStorage)));
  Serial.println("Komendy: p/r/f/n; c test kolorow, ! reset LCD; SPI 1/2; :nazwa UTF-8 + ENTER zmienia tylko napis.");
  drawFrame(true);
  previousMs=previousFrame=reportMs=millis();
  startTapeCounter();
}

void loop() {
  const int seekDirection=tapeSeekDirection();
  int mqttPreset;
  if(!seekDirection&&magnetTakeLabel(mqttPreset)&&setStationLabel(STATION_PRESETS[mqttPreset].speak))
    magnetAppliedPreset.store(mqttPreset);
  uint32_t now=millis();
  float dt=(now-previousMs)*.001f;
  previousMs=now;
  if(playing&&seekDirection) {
    constexpr float seekSpeed=4.f;
    const float p=float(positionMs/SIDE_MS);
    const float direction=seekDirection*(reverseSide?-1.f:1.f);
    const float motion=direction*dt*seekSpeed*TAPE_SPEED_SCENE;
    angle[0]=fmodf(angle[0]+motion/sqrtf(625+3731*(1-p)),TWO_PI);
    angle[1]=fmodf(angle[1]+motion/sqrtf(625+3731*p),TWO_PI);
    positionMs=constrain(positionMs+direction*dt*1000.0*seekSpeed,0.0,double(SIDE_MS));
  } else if(radioReelsMoving(playing)) {
    double advance=dt*1000.0*timeSpeed;
    while(advance>0) {
      double remaining=reverseSide?positionMs:SIDE_MS-positionMs;
      double step=advance<remaining?advance:remaining;
      float p=float(positionMs/SIDE_MS);
      float activeDt=float(step/(1000.0*timeSpeed));
      float sign=reverseSide?-1.f:1.f;
      angle[0]=fmodf(angle[0]+sign*activeDt*TAPE_SPEED_SCENE/sqrtf(625+3731*(1-p)),TWO_PI);
      angle[1]=fmodf(angle[1]+sign*activeDt*TAPE_SPEED_SCENE/sqrtf(625+3731*p),TWO_PI);
      positionMs+=sign*step;
      advance-=step;
      if(step>=remaining) {
        positionMs=reverseSide?0:SIDE_MS;
        reverseSide=!reverseSide;sideChanges++;
        updateMetalPalette();refreshAll=true;
        Serial.printf("AUTO-REVERSE: strona %s, kolor %s, zmiany=%lu\n",
          reverseSide?"B":"A",reverseSide?"zolty":"srebrny",(unsigned long)sideChanges);
      }
    }
  }
  while(Serial.available()) {
    char cmd=Serial.read();
    if(readingLabel) {
      if(cmd=='\n') {
        labelInput[labelInputLength]=0;
        if(!labelOverflow && setStationLabel(labelInput))
          Serial.printf("LABEL: %s, lines=%d scale=%.3f (bez zmiany streamu)\n",stationText.text,stationText.lineCount,stationText.renderedScale);
        else Serial.println("LABEL: odrzucono pusty lub zbyt dlugi napis.");
        readingLabel=false;labelInputLength=0;labelOverflow=false;
      } else if(cmd!='\r') {
        if(labelInputLength<sizeof(labelInput)-1) labelInput[labelInputLength++]=cmd;
        else labelOverflow=true;
      }
      continue;
    }
    if(cmd==':') {readingLabel=true;labelInputLength=0;labelOverflow=false;continue;}
    if(cmd=='p') {if(colorTest){colorTest=false;tft.fillScreen(PANEL_MARGIN_COLOR);refreshAll=true;playing=true;}else playing=!playing;}
    if(cmd=='r') {if(colorTest)tft.fillScreen(PANEL_MARGIN_COLOR);colorTest=false;positionMs=0;angle[0]=angle[1]=0;playing=true;reverseSide=false;sideChanges=0;updateMetalPalette();refreshAll=true;}
    if(cmd=='f') timeSpeed=60;
    if(cmd=='n') timeSpeed=1;
    if(cmd=='!') {colorTest=false;tft.begin(1000000);tft.setSPISpeed(spiHz);tft.fillScreen(PANEL_MARGIN_COLOR);refreshAll=true;}
    if(cmd=='I'||cmd=='J'){tft.setInversionPattern(cmd=='I'?0:2);Serial.printf("LCD inversion pattern %d\n",cmd=='I'?0:2);}
    if(cmd=='D'||cmd=='F'){tft.setDMA(cmd=='D');Serial.printf("TRANSFER %s\n",cmd=='D'?"DMA":"FIFO");}
    if(cmd=='g'){colorTest=true;playing=false;tft.fillScreen(PANEL_MARGIN_COLOR);Serial.println("LCD TEST: uniform grey; p restores cassette");}
    if(cmd=='c') {colorTest=true;
      const uint16_t bars[]={0xf800,0x07e0,0x001f,0xffff,0x7bef,0x0000};
      for(int i=0;i<6;i++)tft.fillRect(i*80,0,80,320,bars[i]);
      Serial.println("LCD TEST: red green blue white grey black; r restores cassette.");
      playing=false;
    }
    if(cmd=='1'||cmd=='2') {
      spiHz=(cmd-'0')*10000000UL;
      tft.setSPISpeed(spiHz);
      Serial.printf("SPI teraz %lu Hz\n",(unsigned long)spiHz);
    }
  }
  if(!colorTest && now-previousFrame>=FRAME_MS) {
    previousFrame=now;
    uint32_t started=micros();
    drawFrame();
    uint32_t took=micros()-started;
    frames++;drawTotalUs+=took;if(took>maxDrawUs)maxDrawUs=took;
  }
  now=millis();
  if(now-reportMs>=5000) {
    displayFpsTenths.store(frames*10000UL/(now-reportMs),std::memory_order_relaxed);
    if(Serial && Serial.availableForWrite()>=128) {
      Serial.printf("FPS=%.1f render=%.1fms max=%.1fms heap=%u min=%u pozycja=%lus speed=%.0fx play=%d\n",
        frames*1000.f/(now-reportMs),frames?drawTotalUs/(frames*1000.f):0,
        maxDrawUs/1000.f,ESP.getFreeHeap(),ESP.getMinFreeHeap(),
        (unsigned long)(positionMs/1000),timeSpeed,playing);
      Serial.printf("AUDIO state=%lu receiving=%d samples=%lu bitrate=%lu rate=%lu buffer=%lu underflows=%lu connections=%lu stack=%lu side=%s\n",
        (unsigned long)audioState.load(),radioIsReceiving(),(unsigned long)audioSamples.load(),
        (unsigned long)audioBitrate.load(),(unsigned long)audioRate.load(),
        (unsigned long)audioFill.load(),(unsigned long)audioUnderflows.load(),
        (unsigned long)audioConnections.load(),(unsigned long)audioStackFree.load(),reverseSide?"B":"A");
      Serial.printf("PLAYBACK mode=%s motion=%d muted_samples=%lu\n",radioPlaybackState(),radioReelsMoving(playing),(unsigned long)audioMutedSamples.load());
      if(frames)Serial.printf("PROFILE cpu=%.1f spi=%.1f other=%.1f ms calls=%.0f pixels=%.0f CPU=%uMHz phase=%.2f finish=%.2f\n",
        profileCpu/(frames*1000.f),profileSpi/(frames*1000.f),(drawTotalUs-profileCpu-profileSpi)/(frames*1000.f),
        profileCalls/float(frames),profilePixels/float(frames),getCpuFrequencyMhz(),
        profilePhase/(frames*1000.f),profileFinish/(frames*1000.f));
    }
    profileCpu=profileSpi=profileCalls=profilePixels=profilePhase=profileFinish=0;
    frames=drawTotalUs=maxDrawUs=0;reportMs=now;
  }
  delay(1);
}
