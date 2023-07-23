// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <array>

#include <libhal/units.hpp>
#include <libhal-util/i2c.hpp>
#include <libhal/i2c.hpp>
#include <libhal/timeout.hpp>

namespace hal::mpl {

class mpl
{
public:
    /* Keep track of the current set mode bit in ctrl_reg1 */
    enum class mpl_mode_t {
        BAROMETER_M = 0,
        ALTIMETER_M = 1,
    };

    struct temperature_read_t
    {
        celsius temperature;
    };

    struct pressure_read_t
    {
        float pressure; // Pascals (Pa)
    };

    struct altitude_read_t
    {
        meters altitude;
    };

    [[nodiscard]] static result<mpl> create(hal::i2c& i2c);

    /*
    * @brief Read pressure data from out_t_msb_r and out_t_lsb_r
    *        and perform temperature conversion to celsius.
    */
    [[nodiscard]] hal::result<temperature_read_t> read_temperature();

    /*
    * @brief Read pressure data from out_p_msb_r, out_p_csb_r, and out_p_lsb_r
    *        and perform pressure conversion to kilopascals.
    */
    [[nodiscard]] hal::result<pressure_read_t> read_pressure();

    /*
    * @brief Read altitude data from out_p_msb_r, out_p_csb_r, and out_p_lsb_r
    *        and perform altitude conversion to meters.
    */
    [[nodiscard]] hal::result<altitude_read_t> read_altitude();

    /*
    * @brief Set sea level pressure (Barometric input for altitude calculations)
    *        in bar_in_msb_r and bar_in_lsb_r registers
    * @param sea_level_pressure: Sea level pressure in Pascals. 
    *        Default value on startup is 101,326 Pa.
    */
    hal::status set_sea_pressure(float sea_level_pressure);
    
    /*
    * @brief Set altitude offset in off_h_r
    * @param offset Offset value in meters, from -127 to 128
    */
    hal::status set_altitude_offset(int8_t offset);

private:
    /*
    * @brief constructor for mpl objects
    * @param p_i2c The I2C peripheral used for communication with the device.
    */
    explicit mpl(hal::i2c& p_i2c);

    /**
     * @brief Initialization of MPLX device.
     *
     * This function performs the following steps during startup configuration:
     *   - Perform WHOAMI check
     *   - Trigger reset and wait for completion
     *   - Set altimeter mode
     *   - Set oversampling ratio to 2^128 (OS128)
     *   - Enable data ready events for pressure/altitude and temperature
     */
    hal::status init();

    /* The I2C peripheral used for communication with the device. */
    hal::i2c* m_i2c;

    /* Variable to track current sensor mode to determine if CTRL_REG1 ALT flag needs to be set. */
    mpl_mode_t sensor_mode;
};

}  // namespace hal::mpl
