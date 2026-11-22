#pragma once
#include "hand_station_font.h"
#include <math.h>
#include <string.h>

// Składanie tekstu tylko przy zmianie nazwy, bez alokacji i bez pracy co klatkę.
class HandStationTextRenderer {
public:
  static constexpr int W=408,H=84;
private:
  static constexpr int MAX_GLYPHS=32;
  struct Item {uint16_t glyph;float x;uint8_t line;};
  struct Layout {
    Item items[MAX_GLYPHS];int count=0,lines=1;
    float minX[2]={10000,10000},maxX[2]={-10000,-10000};
    float minY=10000,maxY=-10000,width=0,scale=0;
  };
  uint8_t bitmap[W*H/4]={0};
  static unsigned glyphFor(uint32_t code) {
    unsigned fallback=0;
    for(unsigned i=0;i<HAND_STATION_GLYPH_COUNT;i++) {
      if(HAND_STATION_GLYPHS[i].code==code) return i;
      if(HAND_STATION_GLYPHS[i].code=='?') fallback=i;
    }
    return fallback;
  }
  static uint32_t decode(const unsigned char *&p) {
    uint32_t c=*p++;
    if(c<128) return c;
    unsigned count=0;
    if((c&0xe0)==0xc0) {c&=31;count=1;}
    else if((c&0xf0)==0xe0) {c&=15;count=2;}
    else if((c&0xf8)==0xf0) {c&=7;count=3;}
    else return '?';
    while(count--) {
      if((*p&0xc0)!=0x80) return '?';
      c=(c<<6)|(*p++&63);
    }
    return c;
  }
  static uint8_t glyphAlpha(const HandStationGlyph &g,int x,int y) {
    if(x<0||y<0||x>=g.width||y>=g.height) return 0;
    int index=y*g.width+x;
    uint8_t packed=pgm_read_byte(HAND_STATION_FONT_ALPHA+g.offset+index/2);
    return (index&1)?packed>>4:packed&15;
  }
  static Layout layout(const uint32_t *codes,int count,int split) {
    Layout l;float cursor=0;int line=0;
    for(int i=0;i<count;i++) {
      if(i==split) {line=1;cursor=0;l.lines=2;continue;}
      unsigned gi=glyphFor(codes[i]);const auto &g=HAND_STATION_GLYPHS[gi];
      if(g.width && g.height) {
        float x=cursor+g.xOffset,y=line*30.f+g.yOffset;
        l.items[l.count++]={uint16_t(gi),x,uint8_t(line)};
        l.minX[line]=fminf(l.minX[line],x);l.maxX[line]=fmaxf(l.maxX[line],x+g.width);
        l.minY=fminf(l.minY,y);l.maxY=fmaxf(l.maxY,y+g.height);
      }
      cursor+=g.advance64/64.f;
    }
    if(!l.count) return l;
    for(int i=0;i<l.lines;i++) {
      if(l.maxX[i]<l.minX[i]) {l.scale=0;return l;}
      l.width=fmaxf(l.width,l.maxX[i]-l.minX[i]);
    }
    l.scale=fminf(W/l.width,H/(l.maxY-l.minY));
    return l;
  }
  static uint32_t printedCapital(uint32_t c){
    if(c>='a'&&c<='z')return c-32;
    switch(c){case 0x105:return 0x104;case 0x107:return 0x106;case 0x119:return 0x118;case 0x142:return 0x141;case 0x144:return 0x143;case 0xf3:return 0xd3;case 0x15b:return 0x15a;case 0x17a:return 0x179;case 0x17c:return 0x17b;default:return c;}
  }
  void put(int x,int y,uint8_t a){
    int index=y*W+x,shift=(index&3)*2;uint8_t q=(a+2)/5;
    uint8_t &packed=bitmap[index/4],old=(packed>>shift)&3;
    if(q>old)packed=uint8_t((packed&~(3u<<shift))|(q<<shift));
  }

public:
  char text[96]={0};int lineCount=1;float renderedScale=1;
  uint8_t alpha(int x,int y) const {
    if(x<0||y<0||x>=W||y>=H) return 0;
    int index=y*W+x;uint8_t packed=bitmap[index/4];
    return uint8_t(((packed>>((index&3)*2))&3)*5);
  }
  bool render(const char *utf8) {
    if(!utf8 || strlen(utf8)>=sizeof(text)) return false;
    uint32_t codes[MAX_GLYPHS];int count=0;
    const unsigned char *p=reinterpret_cast<const unsigned char *>(utf8);
    while(*p) {if(count==MAX_GLYPHS) return false;codes[count++]=printedCapital(decode(p));}
    Layout best=layout(codes,count,-1);
    if(best.scale<=0) return false;
    memset(bitmap,0,sizeof(bitmap));strcpy(text,utf8);
    lineCount=best.lines;renderedScale=best.scale;
    float top=-best.minY*best.scale;
    for(int i=0;i<best.count;i++) {
      const auto &item=best.items[i];const auto &g=HAND_STATION_GLYPHS[item.glyph];
      int line=item.line;
      float shift=(best.width-(best.maxX[line]-best.minX[line]))/2-best.minX[line];
      float left=(W-best.width*best.scale)/2+(item.x+shift)*best.scale;
      float yTop=top+(line*30.f+g.yOffset)*best.scale;
      int x0=int(floorf(left)),x1=int(ceilf(left+g.width*best.scale));
      int y0=int(floorf(yTop)),y1=int(ceilf(yTop+g.height*best.scale));
      for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++) {
        if(x<0||y<0||x>=W||y>=H) continue;
        float sx=(x+.5f-left)/best.scale-.5f,sy=(y+.5f-yTop)/best.scale-.5f;
        int ix=int(floorf(sx)),iy=int(floorf(sy));float fx=sx-ix,fy=sy-iy;
        float a=glyphAlpha(g,ix,iy)*(1-fx)*(1-fy)+glyphAlpha(g,ix+1,iy)*fx*(1-fy)
          +glyphAlpha(g,ix,iy+1)*(1-fx)*fy+glyphAlpha(g,ix+1,iy+1)*fx*fy;
        put(x,y,uint8_t(fminf(15.f,fmaxf(0.f,roundf(a)))));
      }
    }
    return true;
  }
};
