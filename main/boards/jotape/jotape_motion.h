#ifndef JOTAPE_MOTION_H
#define JOTAPE_MOTION_H

#include <driver/i2c_master.h>
#include <esp_err.h>

// Minimal QMI8658 accelerometer access for screen auto-rotation.
namespace JotapeMotion {

struct Accel {
    int x = 0;  // milli-g
    int y = 0;
    int z = 0;
};

// Probes 0x6A/0x6B on the bus and enables the accelerometer.
esp_err_t Initialize(i2c_master_bus_handle_t bus);
bool IsReady();
bool Read(Accel& out);

}  // namespace JotapeMotion

#endif  // JOTAPE_MOTION_H
