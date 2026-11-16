#include "jotape_motion.h"

#include <esp_log.h>

#include <cstdint>
#include <initializer_list>

#define TAG "JotapeMotion"

namespace JotapeMotion {
namespace {

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;  // serial interface: address auto-increment
constexpr uint8_t kRegCtrl2 = 0x03;  // accelerometer range and output rate
constexpr uint8_t kRegCtrl7 = 0x08;  // sensor enable
constexpr uint8_t kRegAccelX = 0x35;
constexpr uint8_t kWhoAmIValue = 0x05;

i2c_master_dev_handle_t device = nullptr;

esp_err_t ReadRegs(uint8_t reg, uint8_t* data, size_t length) {
    return i2c_master_transmit_receive(device, &reg, 1, data, length, 50);
}

esp_err_t WriteReg(uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(device, data, sizeof(data), 50);
}

int16_t Decode(const uint8_t* data) {
    return static_cast<int16_t>(static_cast<uint16_t>(data[0]) |
                                (static_cast<uint16_t>(data[1]) << 8));
}

}  // namespace

esp_err_t Initialize(i2c_master_bus_handle_t bus) {
    if (device != nullptr) {
        return ESP_OK;
    }
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    for (uint8_t address : {uint8_t{0x6B}, uint8_t{0x6A}}) {
        if (i2c_master_probe(bus, address, 50) != ESP_OK) {
            continue;
        }
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = address;
        config.scl_speed_hz = 400000;
        ret = i2c_master_bus_add_device(bus, &config, &device);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "QMI8658 found at 0x%02X", address);
        }
        break;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 not found, auto-rotation disabled");
        return ret;
    }

    uint8_t id = 0;
    ret = ReadRegs(kRegWhoAmI, &id, 1);
    if (ret == ESP_OK && id != kWhoAmIValue) {
        ESP_LOGW(TAG, "Unexpected WHO_AM_I 0x%02X", id);
        ret = ESP_ERR_NOT_FOUND;
    }
    // Address auto-increment, +/-8 g at 250 Hz, accelerometer only.
    if (ret == ESP_OK) {
        ret = WriteReg(kRegCtrl1, 0x60);
    }
    if (ret == ESP_OK) {
        ret = WriteReg(kRegCtrl2, 0x25);
    }
    if (ret == ESP_OK) {
        ret = WriteReg(kRegCtrl7, 0x01);
    }
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(device);
        device = nullptr;
    }
    return ret;
}

bool IsReady() { return device != nullptr; }

bool Read(Accel& out) {
    if (device == nullptr) {
        return false;
    }
    uint8_t data[6] = {};
    if (ReadRegs(kRegAccelX, data, sizeof(data)) != ESP_OK) {
        return false;
    }
    // +/-8 g range: 4096 LSB per g
    out.x = static_cast<int>(Decode(data)) * 1000 / 4096;
    out.y = static_cast<int>(Decode(data + 2)) * 1000 / 4096;
    out.z = static_cast<int>(Decode(data + 4)) * 1000 / 4096;
    return true;
}

}  // namespace JotapeMotion
