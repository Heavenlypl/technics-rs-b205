#pragma once
#include <driver/gpio.h>
#include <esp_timer.h>
#include "board_pins.h"
#include "radio_audio.h"

static constexpr gpio_num_t counterPins[]={
  gpio_num_t(COUNTER_IN1),gpio_num_t(COUNTER_IN2),
  gpio_num_t(COUNTER_IN3),gpio_num_t(COUNTER_IN4)
};
static constexpr uint32_t counterRpmTenths=75;
static constexpr uint32_t counterHalfStepsPerTurn=4096;
static constexpr uint32_t counterPeriodUs=600000000U/(counterRpmTenths*counterHalfStepsPerTurn);
static std::atomic<uint32_t> tapeCounterSteps{0};
static std::atomic<bool> tapeCounterActive{false};

static void writeCounterCoils(uint8_t mask) {
  for(unsigned i=0;i<4;++i)gpio_set_level(counterPins[i],(mask>>i)&1U);
}

static void tickTapeCounter(void *) {
  static constexpr uint8_t halfSteps[]={1,3,2,6,4,12,8,9};
  static unsigned phase=0;
  if(!radioReelsMoving()) {
    if(tapeCounterActive.exchange(false,std::memory_order_relaxed))writeCounterCoils(0);
    return;
  }
  writeCounterCoils(halfSteps[phase]);
  phase=(phase+1)%8;
  tapeCounterSteps.fetch_add(1,std::memory_order_relaxed);
  tapeCounterActive.store(true,std::memory_order_relaxed);
}

static void startTapeCounter() {
  gpio_config_t pins={};
  pins.mode=GPIO_MODE_OUTPUT;
  for(gpio_num_t pin : counterPins) {
    gpio_set_level(pin,0);
    pins.pin_bit_mask|=1ULL<<pin;
  }
  if(gpio_config(&pins)!=ESP_OK){
    Serial.println("BLAD: piny licznika tasmy");
    return;
  }
  esp_timer_create_args_t args={};
  args.callback=tickTapeCounter;
  args.dispatch_method=ESP_TIMER_TASK;
  args.name="tape_counter";
  args.skip_unhandled_events=true;
  esp_timer_handle_t timer=nullptr;
  if(esp_timer_create(&args,&timer)!=ESP_OK){
    Serial.println("BLAD: timer licznika tasmy");
    return;
  }
  if(esp_timer_start_periodic(timer,counterPeriodUs)!=ESP_OK){
    esp_timer_delete(timer);
    Serial.println("BLAD: start licznika tasmy");
  }
}
