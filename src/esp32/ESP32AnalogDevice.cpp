/*
 * Copyright (c) 2018 https://www.thecoderscorner.com (Nutricherry LTD).
 * This product is licensed under an Apache license, see the LICENSE file in the top-level directory.
 */

#ifdef ESP32

#include "ESP32AnalogDevice.h"
#include <SimpleCollections.h>
#include <driver/dac.h>
#include <driver/adc.h>
#include <AnalogDeviceAbstraction.h>

EspAnalogInputMode::EspAnalogInputMode(pinid_t pin) : onAdc1(false), adcChannelNum(0xff), pin(pin), attenuation(ADC_ATTEN_DB_11) {}

EspAnalogInputMode::EspAnalogInputMode(const EspAnalogInputMode& other) = default;

void EspAnalogInputMode::pinSetup() {
    if(adcChannelNum == 0xff) {
        for (int ch = ADC1_CHANNEL_0; ch < ADC1_CHANNEL_MAX; ch++) {
            gpio_num_t gpio = GPIO_NUM_0;
            if (adc1_pad_get_io_num(static_cast<adc1_channel_t>(ch), &gpio) == ESP_OK && gpio == pin) {
                onAdc1 = true;
                adcChannelNum = ch;
                break;
            }
        }
    }

    if(adcChannelNum == 0xff) {
        for (int ch = ADC2_CHANNEL_0; ch < ADC2_CHANNEL_MAX; ch++) {
            gpio_num_t gpio = GPIO_NUM_0;
            if (adc2_pad_get_io_num(static_cast<adc2_channel_t>(ch), &gpio) == ESP_OK && gpio == pin) {
                onAdc1 = false;
                adcChannelNum = ch;
                break;
            }
        }
    }

    // there's a chance that this GPIO may have previously been registered as output, we should
    // ensure that it's initialised as input with no pull up/down.
    if(adcChannelNum != 0xff) {
        alterPinAttenuation(ADC_ATTEN_DB_11);
        gpio_config_t config;
        config.intr_type = GPIO_INTR_DISABLE;
        config.mode = GPIO_MODE_INPUT;
        config.pin_bit_mask = uint64_t(1) << uint64_t(pin);
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&config);
    }
    else {
        serdebugF2("Did not find adc setting for ", pin);
    }
}

uint16_t EspAnalogInputMode::getCurrentReading() {
    // if the ADC is on the dac channel it must be turned off first.
    if(pin == DAC1 || pin == DAC2) {
        dac_output_disable(pin == DAC1 ? DAC_CHANNEL_1 : DAC_CHANNEL_2);
    }

    if(onAdc1) {
        adc1_config_channel_atten(static_cast<adc1_channel_t>(adcChannelNum), static_cast<adc_atten_t>(attenuation));
        return adc1_get_raw(static_cast<adc1_channel_t>(adcChannelNum));
    }
    else {
        int adcVal;
        adc2_config_channel_atten(static_cast<adc2_channel_t>(adcChannelNum), static_cast<adc_atten_t>(attenuation));
        if(adc2_get_raw(static_cast<adc2_channel_t>(adcChannelNum), ADC_WIDTH_BIT_DEFAULT, &adcVal) == ESP_OK) {
            lastCached = adcVal;
            return adcVal;
        }
        return lastCached;
    }
}

EspAnalogOutputMode::EspAnalogOutputMode(pinid_t pin) : pin(pin), pwmChannel(0xff), pwmWidth(5000) {}

EspAnalogOutputMode::EspAnalogOutputMode(const EspAnalogOutputMode& other)  {
    pin = other.pin;
    pwmChannel = other.pwmChannel;
    pwmWidth = other.pwmWidth;
}

void EspAnalogOutputMode::pinSetup() {
    if(!isDac()) {
        // for other than the dac ports, we need to set up PWM
        ledcSetup(pwmChannel, pwmWidth, 8);
        ledcAttachPin(pin, pwmChannel);
    }
    else {
        dac_output_enable(pin == ESP32_DAC1 ? DAC_CHANNEL_1 : DAC_CHANNEL_2);
    }
}

void EspAnalogOutputMode::write(unsigned int newVal) const {
    if(isDac()) {
        dac_output_voltage(pin == ESP32_DAC1 ? DAC_CHANNEL_1 : DAC_CHANNEL_2, newVal);
    }
    else {
        ledcWrite(pwmChannel, newVal);
    }
}

ESP32AnalogDevice* ESP32AnalogDevice::theInstance = nullptr;

ESP32AnalogDevice::ESP32AnalogDevice() {
    adc1_config_width(ADC_WIDTH_BIT_DEFAULT);
}

void ESP32AnalogDevice::initPin(pinid_t pin, AnalogDirection direction) {
    if(direction != DIR_IN) {
        auto* gpio = gpioToPwmKey.getByKey(pin);
        if(!gpio) {
            EspAnalogOutputMode outputMode(pin);
            gpioToPwmKey.add(outputMode);
            gpio = gpioToPwmKey.getByKey(pin);
            gpio->setPwmChannel(gpioToPwmKey.count());
        }
        gpio->pinSetup();
    }
    else {
        auto* gpio = gpioToInputKey.getByKey(pin);
        if(!gpio) {
            EspAnalogInputMode inputMode(pin);
            gpioToInputKey.add(inputMode);
            gpio = gpioToInputKey.getByKey(pin);
        }
        gpio->pinSetup();
    }
}

void ESP32AnalogDevice::setCurrentFloat(pinid_t pin, float value) {
    if(value < 0.0F) value = 0.0F;
    auto compVal = (int)(value * 255.0F);
    if(compVal > 255) compVal = 255;
    setCurrentValue(pin, compVal);
    //serdebugF4("Flt set ", value, maxValue, compVal);
}

AnalogDevice* internalAnalogIo() {
    if(ESP32AnalogDevice::theInstance == nullptr) ESP32AnalogDevice::theInstance = new ESP32AnalogDevice();
    return ESP32AnalogDevice::theInstance;
}

#endif
