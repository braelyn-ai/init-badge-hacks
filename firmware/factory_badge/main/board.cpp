// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
// Display, controller and power setup adapted from M5StopWatch-UserDemo
// 6b4aa125. See vendor/README.md and vendor/LICENSE-M5Stack.
#include "board.h"
#include "board_rotation.h"
#include "board_touch.h"
#include "touch_mapping.h"
#include <cmath>
#include "board_flush.h"
#include "neutral_gray.h"
#include "board_vibration.h"
#include "vendor/cst820/cst820.h"
#include "vendor/rx8130/rx8130.h"

#include <i2c_bus.h>
#include <M5GFX.h>
#include <M5IOE1.h>
#include <M5PM1.h>
#include <bmi270_bmm150.h>
#include <lgfx/v1/panel/Panel_AMOLED.hpp>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <memory>

namespace board {
namespace {
constexpr const char* Tag = "badge-board";

// Factory QSPI panel integration. Touch is deliberately not attached to M5GFX:
// its sensor samples enter LVGL directly, matching the official demo HAL.
class PanelCo5300 final : public lgfx::Panel_AMOLED {
public:
    PanelCo5300() {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
        _write_depth = _read_depth = lgfx::color_depth_t::rgb565_2Byte;
    }
    const uint8_t* getInitCommands(uint8_t list) const override {
        static constexpr uint8_t commands[] = {
            0x11, 0 + CMD_INIT_DELAY, 150,
            0xc4, 1, 0x80, 0x35, 1, 0x80,
            0x44, 2, 0x01, 0xd2,
            0x53, 1, 0x20, 0x20, 0, 0x36, 1, 0,
            0x51, 1, 0xa0, 0x29, 0, 0xff, 0xff
        };
        return list == 0 ? commands : nullptr;
    }
};

class StopWatchDisplay final : public M5GFX {
    lgfx::Bus_SPI bus_;
    PanelCo5300 panel_;
public:
    bool init_impl(bool reset, bool clear) override {
        auto bus = bus_.config();
        bus.freq_write = 80000000;
        bus.freq_read = 10000000;
        bus.pin_sclk = GPIO_NUM_40;
        bus.pin_io0 = GPIO_NUM_41;
        bus.pin_io1 = GPIO_NUM_42;
        bus.pin_io2 = GPIO_NUM_46;
        bus.pin_io3 = GPIO_NUM_45;
        bus.spi_host = SPI2_HOST;
        bus.spi_mode = 0;
        bus.spi_3wire = true;
        bus.dma_channel = SPI_DMA_CH_AUTO;
        bus_.config(bus);
        panel_.setBus(&bus_);

        auto panel = panel_.config();
        panel.pin_rst = GPIO_NUM_NC;
        panel.pin_cs = GPIO_NUM_39;
        panel.panel_width = NativeWidth;
        panel.panel_height = NativeHeight;
        panel.offset_x = 6;
        panel.offset_y = 0;
        panel.readable = false;
        panel_.config(panel);
        setPanel(&panel_);
        lgfx::pinMode(GPIO_NUM_38, lgfx::pin_mode_t::input_pullup);
        if (!LGFX_Device::init_impl(reset, clear)) return false;
        if (!panel_.initPanelFb()) return false;
        auto* framebuffer = panel_.getPanelFb();
        if (!framebuffer) return false;
        framebuffer->setBus(&bus_);
        framebuffer->setAutoDisplay(true);
        setPanel(framebuffer);
        panel_.setBrightness(128);
        return true;
    }
    void brightness(uint8_t value) { panel_.setBrightness(value); }
};

i2c_bus_handle_t i2cBus = nullptr;
std::unique_ptr<M5IOE1> ioe;
struct InputMotor {
    bool setFrequency(uint16_t frequency) {
        return ioe && ioe->setPwmFrequency(frequency) == M5IOE1_OK;
    }
    bool setDuty(uint8_t duty) {
        return ioe && ioe->setPwmDuty(M5IOE1_PWM_CH1, duty, false, true) == M5IOE1_OK;
    }
} inputMotor;
InputVibration inputVibration;
std::unique_ptr<M5PM1> pmic;
std::unique_ptr<StopWatchDisplay> gfx;
std::unique_ptr<Cst820> sensor;
Rx8130 rtc;
bool rtcReady = false, initialized = false;
bmi270_bmm150_handle_t imu = nullptr;
lv_display_t* lvDisplay = nullptr;
lv_indev_t* lvPointer = nullptr;
TouchSample touchSample;
Buttons buttonState;
Acceleration accel;
Battery batteryState;
uint8_t rotationValue = 0, brightnessValue = 60;
uint32_t lastTouchPoll = 0, lastImuPoll = 0, lastBatteryPoll = 0;
bool sampledTouchOnce = false, sampledImuOnce = false, sampledBatteryOnce = false;
bool injectedActive = false, injectedReleasePending = false;
bool physicalPressed = false;
uint16_t filteredBatteryMv = 0;

void delayMs(uint32_t duration) { vTaskDelay(pdMS_TO_TICKS(duration)); }
uint32_t tick() { return millis(); }

bool initI2c() {
    i2c_config_t cfg{};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = GPIO_NUM_47;
    cfg.scl_io_num = GPIO_NUM_48;
    cfg.sda_pullup_en = true;
    cfg.scl_pullup_en = true;
    cfg.master.clk_speed = 100000;
    i2cBus = i2c_bus_create(I2C_NUM_0, &cfg);
    return i2cBus != nullptr;
}

bool initPower() {
    auto bus = i2c_bus_get_internal_bus_handle(i2cBus);
    pmic = std::make_unique<M5PM1>();
    if (pmic->begin(bus) != M5PM1_OK) return false;
    pmic->setI2cSleepTime(0);
    pmic->setI2cSleepTime(0);
    pmic->btnSetConfig(M5PM1_BTN_TYPE_CLICK, M5PM1_BTN_CLICK_DELAY_1000MS);
    pmic->wdtSet(0);
    pmic->ldoSetPowerHold(true); // Preserve RTC rail through device shutdown.
    pmic->setChargeEnable(true);
    pmic->gpioSet(M5PM1_GPIO_NUM_3, M5PM1_GPIO_MODE_OUTPUT, 0,
                 M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
    pmic->gpioSetFunc(M5PM1_GPIO_NUM_2, M5PM1_GPIO_FUNC_GPIO);
    pmic->gpioSetMode(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_INPUT);
    pmic->gpioSetPull(M5PM1_GPIO_NUM_2, M5PM1_GPIO_PULL_NONE);
    pmic->setSingleResetDisable(true);

    ioe = std::make_unique<M5IOE1>();
    auto result = ioe->begin(bus, 0x4f, M5IOE1_I2C_FREQ_400K);
    if (result != M5IOE1_OK) result = ioe->begin(bus, 0x6f, M5IOE1_I2C_FREQ_400K);
    if (result != M5IOE1_OK) return false;
    ioe->setI2cSleepTime(0);
    ioe->setI2cSleepTime(0);
    // Clear retained motor PWM before setting output modes. Keep audio off.
    inputVibration.init(inputMotor, millis());
    for (auto pin : {M5IOE1_PIN_9, M5IOE1_PIN_8, M5IOE1_PIN_10,
                     M5IOE1_PIN_4, M5IOE1_PIN_5, M5IOE1_PIN_1, M5IOE1_PIN_3}) {
        ioe->pinMode(pin, OUTPUT);
    }
    ioe->digitalWrite(M5IOE1_PIN_9, 0);
    ioe->digitalWrite(M5IOE1_PIN_10, 0);
    ioe->digitalWrite(M5IOE1_PIN_3, 0);
    ioe->digitalWrite(M5IOE1_PIN_1, 0);
    ioe->digitalWrite(M5IOE1_PIN_8, 1);
    ioe->digitalWrite(M5IOE1_PIN_4, 1);
    ioe->digitalWrite(M5IOE1_PIN_5, 1);
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 0);
    // The factory retries indefinitely. Bound startup if the expander fails.
    for (int attempt = 0; attempt < 10; ++attempt) {
        delayMs(80);
        if (ioe->digitalRead(M5IOE1_PIN_8) == 1) return true;
        ioe->digitalWrite(M5IOE1_PIN_8, 1);
    }
    return false;
}

void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    neutralizeGrays565(reinterpret_cast<uint16_t*>(pixels), size_t(w) * h);
    const auto* source = reinterpret_cast<const lgfx::rgb565_t*>(pixels);
    flushImageChunks(*gfx, area->x1, area->y1, w, h, source);
    lv_display_flush_ready(display);
}

lv_point_t nativeTouchPoint() {
    auto p = touchSample.sensor ? touch_mapping::map({double(touchSample.rawX), double(touchSample.rawY)})
                               : touchcal::Point{double(touchSample.rawX), double(touchSample.rawY)};
    return {int32_t(std::lround(std::clamp(p.x, 0.0, double(NativeWidth - 1)))),
            int32_t(std::lround(std::clamp(p.y, 0.0, double(NativeHeight - 1))))};
}
void readPointer(lv_indev_t*, lv_indev_data_t* data) {
    data->state = touchSample.valid && touchSample.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    // lv_indev.c calls lv_display_rotate_point itself. Never pass x/y here.
    data->point = nativeTouchPoint();
    data->continue_reading = false;
    if (injectedReleasePending) {
        injectedReleasePending = false;
        injectedActive = false;
    }
}

bool initLvgl() {
    lv_init();
    lv_tick_set_cb(tick);
    lvDisplay = lv_display_create(NativeWidth, NativeHeight);
    if (!lvDisplay) return false;
    lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lvDisplay, flush);
    constexpr size_t bufferSize = NativeWidth * 60 * sizeof(uint16_t);
    void* first = heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void* second = heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!first || !second) {
        heap_caps_free(first);
        heap_caps_free(second);
        lv_display_delete(lvDisplay);
        lvDisplay = nullptr;
        return false;
    }
    lv_display_set_buffers(lvDisplay, first, second, bufferSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lvPointer = lv_indev_create();
    if (!lvPointer) return false;
    lv_indev_set_type(lvPointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(lvPointer, lvDisplay);
    lv_indev_set_read_cb(lvPointer, readPointer);
    lv_timer_set_period(lv_indev_get_read_timer(lvPointer), 10);
    return true;
}

void rotateObservation() {
    lv_point_t point = nativeTouchPoint();
    lv_display_rotate_point(lvDisplay, &point);
    touchSample.x = point.x;
    touchSample.y = point.y;
}

void pollTouch(uint32_t now) {
    if (sampledTouchOnce && uint32_t(now - lastTouchPoll) < 8) return;
    sampledTouchOnce = true;
    lastTouchPoll = now;
    const bool previousPressed = touchSample.pressed;
    const bool valid = sensor && sensor->read();
    physicalPressed = valid && sensor->getFingerNum() > 0;
    if (injectedActive && !physicalPressed) return;
    if (injectedActive) {
        // A real finger takes ownership; abort the simulated gesture safely.
        lv_indev_reset(lvPointer, nullptr);
        injectedActive = injectedReleasePending = false;
    }
    acceptPhysicalSample(touchSample, valid, physicalPressed,
                         sensor ? sensor->getX() : 0, sensor ? sensor->getY() : 0, now);
    if (!touchSample.valid && previousPressed && lvPointer) {
        // A failed bus transaction must not become a completed button click.
        lv_indev_reset(lvPointer, nullptr);
    }
    rotateObservation();
}

struct Debouncer {
    bool candidate = false, stable = false;
    uint32_t changedAt = 0;
    bool update(bool raw, uint32_t now) {
        if (candidate != raw) { candidate = raw; changedAt = now; }
        if (uint32_t(now - changedAt) >= 10) stable = candidate;
        return stable;
    }
};
Debouncer yellowButton, blueButton;

void pollBattery(uint32_t now) {
    if (sampledBatteryOnce && uint32_t(now - lastBatteryPoll) < 1000) return;
    sampledBatteryOnce = true;
    lastBatteryPoll = now;
    uint16_t mv = 0, vin = 0;
    uint8_t chargeStatus = 1;
    batteryState.valid = pmic && pmic->readVbat(&mv) == M5PM1_OK;
    if (!batteryState.valid) return;
    filteredBatteryMv = filteredBatteryMv ? (uint32_t(filteredBatteryMv) * 7 + mv + 4) / 8 : mv;
    batteryState.millivolts = filteredBatteryMv;
    batteryState.percent = std::clamp((int(filteredBatteryMv) - 3300) * 100 / 900, 0, 100);
    batteryState.usb = pmic->readVin(&vin) == M5PM1_OK && vin > 4000;
    batteryState.charging = batteryState.usb &&
        pmic->gpioGetInput(M5PM1_GPIO_NUM_2, &chargeStatus) == M5PM1_OK && chargeStatus == 0;
}

void pollImu(uint32_t now) {
    if (sampledImuOnce && uint32_t(now - lastImuPoll) < 50) return;
    sampledImuOnce = true;
    lastImuPoll = now;
    int available = 0;
    accel.valid = false;
    if (imu && bmi270_bmm150_sensor_acceleration_available(imu, &available) == ESP_OK && available > 0 &&
        bmi270_bmm150_sensor_read_acceleration(imu, &accel.x, &accel.y, &accel.z) == ESP_OK) {
        accel.valid = true;
        accel.sampledAtMs = now;
    }
}
}  // namespace

bool init() {
    if (initialized) return true;
    if (!initI2c() || !initPower()) {
        ESP_LOGE(Tag, "I2C/power initialization failed");
        return false;
    }
    delayMs(50);
    gfx = std::make_unique<StopWatchDisplay>();
    if (!gfx->init()) { ESP_LOGE(Tag, "display initialization failed"); return false; }
    ioe->digitalWrite(M5IOE1_PIN_4, 0);
    delayMs(10);
    ioe->digitalWrite(M5IOE1_PIN_4, 1);
    delayMs(50);
    sensor = std::make_unique<Cst820>();
    if (!sensor->begin(i2c_bus_get_internal_bus_handle(i2cBus))) {
        ESP_LOGE(Tag, "touch initialization failed");
        return false;
    }
    touch_mapping::load();
    if (!initLvgl()) { ESP_LOGE(Tag, "LVGL initialization failed"); return false; }
    bmi270_bmm150_config_t imuConfig{};
    imuConfig.i2c_addr = 0x68;
    imuConfig.config_file_ptr = nullptr;
    imuConfig.mode = BOSCH_ACCELEROMETER_ONLY;
    if (bmi270_bmm150_sensor_create(i2cBus, &imu, &imuConfig) != ESP_OK) {
        imu = nullptr;
        ESP_LOGW(Tag, "IMU unavailable; fixed orientations remain usable");
    }
    rtcReady = rtc.begin(i2c_bus_get_internal_bus_handle(i2cBus));
    if (!rtcReady) ESP_LOGW(Tag, "RTC unavailable");
    for (auto pin : {GPIO_NUM_1, GPIO_NUM_2}) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    }
    setBrightness(brightnessValue);
    initialized = true;
    poll();
    return true;
}

void poll() {
    if (!initialized) return;
    const uint32_t now = millis();
    pollTouch(now);
    inputVibration.poll(touchSample.valid && touchSample.pressed, inputMotor, millis());
    buttonState.yellow = yellowButton.update(gpio_get_level(GPIO_NUM_2) == 0, now);
    buttonState.blue = blueButton.update(gpio_get_level(GPIO_NUM_1) == 0, now);
    pollImu(now);
    pollBattery(now);
}
uint32_t millis() { return uint32_t(esp_timer_get_time() / 1000); }
const TouchSample& touch() { return touchSample; }
void setInputVibration(bool pressed) {
    if (!initialized) return;
    inputVibration.set(pressed, touchSample.valid && touchSample.pressed, inputMotor, millis());
}
bool inputVibrationAvailable() { return initialized && inputVibration.available(); }
bool inputVibrationActive() { return inputVibration.active(); }
bool injectTouch(int x, int y, bool pressed) {
    if (!initialized || physicalPressed || (!pressed && !injectedActive) ||
        x < 0 || y < 0 || x >= width() || y >= height()) return false;
    if (injectedReleasePending) return false;
    const auto raw = displayToNative(x, y, rotationValue, NativeWidth, NativeHeight);
    injectedActive = true;
    injectedReleasePending = !pressed;
    acceptInjectedSample(touchSample, raw.x, raw.y, x, y, pressed, millis());
    return true;
}
Buttons buttons() { return buttonState; }
Acceleration acceleration() { return accel; }
Battery battery() { return batteryState; }
bool setRotation(uint8_t value) {
    if (!lvDisplay || !gfx || touchSample.pressed || injectedActive || value > 3) return false;
    if (rotationValue == value) return true;
    gfx->setRotation(value);
    rotationValue = value;
    lv_indev_reset(lvPointer, nullptr);
    lv_display_set_rotation(lvDisplay, lvRotation(value));
    rotateObservation();
    return true;
}
uint8_t rotation() { return rotationValue; }
int width() { return gfx ? gfx->width() : NativeWidth; }
int height() { return gfx ? gfx->height() : NativeHeight; }
void setBrightness(uint8_t percent) {
    brightnessValue = std::clamp<int>(percent, 10, 100);
    if (gfx) gfx->brightness((unsigned(brightnessValue) * 255 + 50) / 100);
}
uint8_t brightness() { return brightnessValue; }
lv_display_t* display() { return lvDisplay; }
lv_indev_t* pointer() { return lvPointer; }
bool readFrameRow(int y, uint8_t* rgb, size_t bytes) {
    if (!gfx || !rgb || y < 0 || y >= height() || bytes < size_t(width()) * 3) return false;
    gfx->readRectRGB(0, y, width(), 1, rgb);
    return true;
}
bool rtcAvailable() { return rtcReady; }
bool readRtcUtc(int64_t& epoch) { return rtcReady && rtc.readUtc(epoch); }
bool setRtcUtc(int64_t epoch) { return rtcReady && rtc.writeUtc(epoch); }
}  // namespace board
