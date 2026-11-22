#pragma once
#include <Arduino.h>
#include <SPI.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>
#include <driver/spi_master.h>
#include <soc/spi_struct.h>
#endif
#include "board_pins.h"

#if defined(ARDUINO_ARCH_ESP32)
#pragma GCC push_options
#pragma GCC optimize ("O3")
#endif

// Waveshare 13587 Arduino Shield, ILI9486 + 74HC4094/74HC4040.
// Initialization and wire framing derived from Waveshare team's official
// 4inch_TFT_Touch_Shield_Code V2.0 / Arduino/LCD_Show/LCD_Driver.cpp.
// Keep the author's attribution: original register sequence by Waveshare team.
// A command is ONE byte with its own CS pulse; each parameter is a zero-
// extended 16-bit word. Pixel data is ordinary RGB565, two bytes per pixel.
// This is not the ILI9341 nor the generic 18-bit SPI ILI9486 protocol.
class WaveshareILI9486 {
  static inline void pinWrite(uint8_t pin,uint8_t state) {
#if defined(ARDUINO_ARCH_ESP32)
    gpio_ll_set_level(&GPIO,pin,state);
#else
    digitalWrite(pin,state);
#endif
  }
  uint32_t hz=1000000;
  bool transaction=false,useDMA=false;
  uint16_t width_=480,height_=320;
#if defined(ARDUINO_ARCH_ESP32)
  spi_device_handle_t device=nullptr;
  spi_transaction_t pendingTransfer={};
  bool pending=false,busReady=false,primed=false;
  void configureDevice() {
    spi_device_interface_config_t config={};
    config.clock_speed_hz=hz;config.mode=0;config.spics_io_num=-1;
    config.queue_size=1;config.flags=SPI_DEVICE_HALFDUPLEX;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST,&config,&device));
    primed=false;
  }
#endif
  void waitPixels() {
#if defined(ARDUINO_ARCH_ESP32)
    if(pending){ESP_ERROR_CHECK(spi_device_polling_end(device,portMAX_DELAY));pending=false;pinWrite(TFT_CS,HIGH);}
#endif
  }
  void sendBytes(const uint8_t *data,size_t n) {
#if defined(ARDUINO_ARCH_ESP32)
    // Tiny register packets use the FIFO while the IDF bus is exclusively held.
    // Preserve DMA/user configuration so the next IDF pixel transfer sees the
    // same peripheral state it left. No IDF transaction overhead per LCD byte.
    uint32_t dma=GPSPI2.dma_conf.val,user=GPSPI2.user.val,len=GPSPI2.ms_dlen.val;
    GPSPI2.dma_conf.dma_tx_ena=0;GPSPI2.dma_conf.dma_rx_ena=0;
    GPSPI2.user.usr_command=0;GPSPI2.user.usr_addr=0;GPSPI2.user.usr_dummy=0;
    GPSPI2.user.usr_mosi_highpart=0;GPSPI2.user.usr_mosi=1;GPSPI2.user.usr_miso=0;
    while(n){
      size_t chunk=n>64?64:n;uint32_t words[16];words[(chunk-1)/4]=0;memcpy(words,data,chunk);
      GPSPI2.ms_dlen.ms_data_bitlen=chunk*8-1;
      for(size_t i=0;i<(chunk+3)/4;i++)GPSPI2.data_buf[i]=words[i];
      GPSPI2.cmd.update=1;while(GPSPI2.cmd.update){}
      GPSPI2.cmd.usr=1;while(GPSPI2.cmd.usr){}
      data+=chunk;n-=chunk;
    }
    GPSPI2.dma_conf.val=dma;GPSPI2.user.val=user;GPSPI2.ms_dlen.val=len;
#else
    SPI.writeBytes(data,n);
#endif
  }
  void command(uint8_t cmd,const uint8_t *params=nullptr,size_t n=0) {
    waitPixels();
    pinWrite(TFT_CS,HIGH);
    pinWrite(TFT_DC,LOW);pinWrite(TFT_CS,LOW);
    sendBytes(&cmd,1); // vendor command is 8 bits, not padded to 16
    pinWrite(TFT_CS,HIGH);
    if(n) {
      pinWrite(TFT_DC,HIGH);pinWrite(TFT_CS,LOW);
      alignas(4) uint8_t padded[32]; // largest vendor command is 15 parameters
      for(size_t i=0;i<n;i++){padded[2*i]=0;padded[2*i+1]=params[i];}
      sendBytes(padded,n*2);
      pinWrite(TFT_CS,HIGH);
    }
  }
public:
  void setDMA(bool value){waitPixels();useDMA=value;}
  void setInversionPattern(uint8_t value){
    // ILI9486 B4: 0 = column inversion, 2 = 2-dot inversion. 1 is prohibited.
    if(value!=0&&value!=2)return;
    endWrite();startWrite();command(0xb4,&value,1);endWrite();
  }
  void startWrite(){if(!transaction){
#if defined(ARDUINO_ARCH_ESP32)
    ESP_ERROR_CHECK(spi_device_acquire_bus(device,portMAX_DELAY));
    if(!primed){spi_transaction_t t={};t.flags=SPI_TRANS_USE_TXDATA;t.length=8;
      pinWrite(TFT_CS,HIGH);ESP_ERROR_CHECK(spi_device_polling_transmit(device,&t));primed=true;}
#else
    SPI.beginTransaction(SPISettings(hz,MSBFIRST,SPI_MODE0));
#endif
    transaction=true;}}
  void endWrite(){waitPixels();pinWrite(TFT_CS,HIGH);if(transaction){
#if defined(ARDUINO_ARCH_ESP32)
    spi_device_release_bus(device);
#else
    SPI.endTransaction();
#endif
    transaction=false;}}
  void setSPISpeed(uint32_t value){
    endWrite();hz=value>20000000?20000000:value;
#if defined(ARDUINO_ARCH_ESP32)
    if(device){ESP_ERROR_CHECK(spi_bus_remove_device(device));device=nullptr;configureDevice();}
#endif
  }
  void begin(uint32_t speed=1000000) {
    setSPISpeed(speed);
    pinMode(TFT_CS,OUTPUT);pinMode(TFT_DC,OUTPUT);pinMode(TFT_RST,OUTPUT);pinMode(TFT_BL,OUTPUT);
    pinWrite(TFT_CS,HIGH);pinWrite(TFT_DC,HIGH);pinWrite(TFT_BL,LOW);
#if defined(ARDUINO_ARCH_ESP32)
    if(!busReady){
      spi_bus_config_t bus={};bus.mosi_io_num=TFT_MOSI;bus.miso_io_num=-1;
      bus.sclk_io_num=TFT_SCLK;bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;
      bus.max_transfer_sz=480*8*2;
      ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
      busReady=true;configureDevice();
    }
#else
    SPI.begin(TFT_SCLK,TFT_MISO,TFT_MOSI,TFT_CS);
#endif
    pinWrite(TFT_RST,HIGH);delay(500);
    pinWrite(TFT_RST,LOW);delay(500);pinWrite(TFT_RST,HIGH);delay(500);
    static const uint8_t init[]={
      0xf9,0x02,0x00,0x08,
      0xc0,0x02,0x19,0x1a,
      0xc1,0x02,0x45,0x00,
      0xc2,0x01,0x33,
      0xc5,0x02,0x00,0x28,
      0xb1,0x02,0xa0,0x11,
      0xb4,0x01,0x00, // column inversion: user confirmed no central stripe
      0xb6,0x03,0x00,0x42,0x3b,
      0xb7,0x01,0x07,
      0xe0,0x0f,0x1f,0x25,0x22,0x0b,0x06,0x0a,0x4e,0xc6,0x39,0x00,0x00,0x00,0x00,0x00,0x00,
      0xe1,0x0f,0x1f,0x3f,0x3f,0x0f,0x1f,0x0f,0x46,0x49,0x31,0x05,0x09,0x03,0x1c,0x1a,0x00,
      0xf1,0x08,0x36,0x04,0x00,0x3c,0x0f,0x0f,0xa4,0x02,
      0xf2,0x09,0x18,0xa3,0x12,0x02,0x32,0x12,0xff,0x32,0x00,
      0xf4,0x05,0x40,0x00,0x08,0x91,0x04,
      0xf8,0x02,0x21,0x04,
      0x3a,0x01,0x55,
    };
    startWrite();
    for(size_t i=0;i<sizeof(init);){uint8_t cmd=init[i++],n=init[i++];command(cmd,init+i,n);i+=n;}
    endWrite();
    setRotation(1);delay(200);
    startWrite();command(0x11);endWrite();delay(120);
    startWrite();command(0x29);endWrite();
    pinWrite(TFT_BL,HIGH);
  }
  void setRotation(uint8_t value) {
    // Landscape: reverse the 480-line gate scan (GS) to remove horizontal
    // mirroring on the physical shield. Opposite rotation also flips SS.
    uint8_t b6[]={0x00,uint8_t(value==3?0x02:0x62)};
    uint8_t mad=0x28;
    startWrite();command(0xb6,b6,2);command(0x36,&mad,1);endWrite();
    width_=480;height_=320;
  }
  void setAddrWindow(uint16_t x,uint16_t y,uint16_t w,uint16_t h) {
    uint16_t xe=x+w-1,ye=y+h-1;
    uint8_t xp[]={uint8_t(x>>8),uint8_t(x),uint8_t(xe>>8),uint8_t(xe)};
    uint8_t yp[]={uint8_t(y>>8),uint8_t(y),uint8_t(ye>>8),uint8_t(ye)};
    command(0x2a,xp,4);command(0x2b,yp,4);command(0x2c);
  }
  void writePixels(uint16_t *data,uint32_t count,bool=true,bool bigEndian=true) {
    waitPixels();
    pinWrite(TFT_DC,HIGH);pinWrite(TFT_CS,LOW);
    if(bigEndian) {
#if defined(ARDUINO_ARCH_ESP32)
      // Keep small and unaligned updates on the FIFO. Sending full strips by
      // DMA allows their transfer to overlap composition of the next strip.
      if(useDMA&&count>=128&&count%2==0&&(reinterpret_cast<uintptr_t>(data)&3)==0){
        pendingTransfer={};pendingTransfer.length=count*16;pendingTransfer.tx_buffer=data;
        ESP_ERROR_CHECK(spi_device_polling_start(device,&pendingTransfer,portMAX_DELAY));
        pending=true;return;
      }
      sendBytes(reinterpret_cast<uint8_t *>(data),count*2);
#else
      sendBytes(reinterpret_cast<uint8_t *>(data),count*2);
#endif
    } else for(uint32_t i=0;i<count;i++){uint8_t bytes[]={uint8_t(data[i]>>8),uint8_t(data[i])};sendBytes(bytes,2);}
    pinWrite(TFT_CS,HIGH);
  }
  void fillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t native) {
    uint16_t be=(native>>8)|(native<<8),buf[128];
    for(auto &v:buf)v=be;
    startWrite();setAddrWindow(x,y,w,h);
    uint32_t left=uint32_t(w)*h;
    while(left){uint32_t n=left<128?left:128;writePixels(buf,n);left-=n;}
    endWrite();
  }
  void fillScreen(uint16_t native){fillRect(0,0,width_,height_,native);}
};

#if defined(ARDUINO_ARCH_ESP32)
#pragma GCC pop_options
#endif
