#include "libhal-mpl311/mpl311.hpp"

namespace hal::mpl311 {

bool mpl311::begin()
{
    // sanity check
    std::array<hal::byte, 1> whoami_payload { WHOAMI_R };
    std::array<hal::byte, 1> whoami_buffer;
    HAL_CHECK(hal::write_then_read(*m_i2c, device_address, whoami_payload, whoami_buffer, hal::never_timeout()));

    if (whoami_buffer[0] != 0xC4) {
        return false;
    }

    // software reset
    std::array<hal::byte, 2> reset_payload { CTRL_REG1, CTRL_REG1_RST };
    HAL_CHECK(hal::write(*m_i2c, device_address, reset_payload, hal::never_timeout()));

    std::array<hal::byte, 1> ctrl_payload { CTRL_REG1 };
    std::array<hal::byte, 1> ctrl_buffer;
    while (1) {
        HAL_CHECK(hal::write_then_read(*m_i2c, device_address, ctrl_payload, ctrl_buffer, hal::never_timeout()));
        if (!(ctrl_buffer[0] & CTRL_REG1_RST))
            break;

        hal::delay(m_clk, 10ms);
    }

    // set oversampling ratio to 2^128 and set altitude mode
    sensor_mode = ALTIMETER_M;

    std::array<hal::byte, 2> os_payload { CTRL_REG1, CTRL_REG1_OS128 | CTRL_REG1_ALT };
    HAL_CHECK(hal::write(*m_i2c, device_address, os_payload, hal::never_timeout()));

    // enable data ready events for pressure/altitude and temperature
    std::array<hal::byte, 2> dr_payload {
        PT_DATA_CFG_R, 
        PT_DATA_CFG_TDEFE |
        PT_DATA_CFG_PDEFE |
        PT_DATA_CFG_DREM
    };
    HAL_CHECK(hal::write(*m_i2c, device_address, dr_payload, hal::never_timeout()));

    return true;
}

void mpl311::set_mode(mpl311_mode_t mode = BAROMETER_M)
{
    // Read value of CTRL_REG1
    std::array<hal::byte, 1> ctrl_payload { CTRL_REG1 };
    std::array<hal::byte, 1> ctrl_buffer;
    HAL_CHECK(hal::write_then_read(*m_i2c, device_address, ctrl_payload, ctrl_buffer, hal::never_timeout()));

    // Set mode ctrl reg bit to binary value of 'mode'
    hal::byte mode_set_reg = ctrl_buffer[0] ^= (-mode ^ ctrl_buffer[0]) & (1UL << 7);
    std::array<hal::byte, 2> ctrl_write { CTRL_REG1, mode_set_reg }
    HAL_CHECK(hal::write(*m_i2c, device_address, ctrl_write, hal::never_timeout()));

    sensor_mode = mode;
}

void mpl311::initiate_one_shot() 
{
    // wait for one-shot to clear before proceeding
    std::array<hal::byte, 1> ctrl_payload { CTRL_REG1 };
    std::array<hal::byte, 1> ctrl_buffer;
    while (1) {
        HAL_CHECK(hal::write_then_read(*m_i2c, device_address, ctrl_payload, ctrl_buffer, hal::never_timeout()));
        if (!(ctrl_buffer[0] & CTRL_REG1_OST))
            break;

        hal::delay(m_clk, 10ms);
    }

    // Set ost bit in CTRL_REG1 - initiate one shot measurement
    hal::byte ost_set_reg = ctrl_buffer[0] |= 1UL << 1;
    std::array<hal::byte, 2> ctrl_write { CTRL_REG1, ost_set_reg }
    HAL_CHECK(hal::write(*m_i2c, device_address, ctrl_write, hal::never_timeout()));
}

bool mpl311::check_data_ready_flag()
{   
    // Read value of CTRL_REG1
    std::array<hal::byte, 1> status_payload { STATUS_R };
    std::array<hal::byte, 1> status_buffer;
    HAL_CHECK(hal::write_then_read(*m_i2c, device_address, status_payload, status_buffer, hal::never_timeout()));

    return ((status_buffer[0] & STATUS_PTDR) != 0);
}

hal::result<temperature_read_t> mpl311::t_read()
{
    initiate_one_shot();
    while (!check_data_ready_flag())
        hal::delay(m_clk, 10ms);
    
    constexpr float temp_conversion_factor = 256.0f;

    // Read data from OUT_T_MSB_R and OUT_T_LSB_R
    std::array<hal::byte, 1> temp_payload = { OUT_T_MSB_R };
    std::array<hal::byte, 2> temp_buffer;
    HAL_CHECK(hal::write_then_read(*m_i2c, device_address, temp_payload, temp_buffer, hal::never_timeout()));

    const int16_t temp_reading = uint16_t(temp_buffer[0]) << 8 | uint16_t(temp_buffer[4]);
    return temperature_read_t {
        .temperature = static_cast<float>(temp_reading) / temp_conversion_factor;
    }
}

hal::result<pressure_read_t> mpl311::p_read()
{
    if (sensor_mode != BAROMETER_M)
        setMode(BAROMETER_M);
    initiate_one_shot();
    while (!check_data_ready_flag())
        hal::delay(m_clk, 10ms);

    // Note: 64 -> Pa, 6400 -> kPa
    constexpr float pressure_conversion_factor = 64.0f;

    // Read data from OUT_P_MSB_R, OUT_P_CSB_R, and OUT_P_LSB_R
    std::array<hal::byte, 1> pres_payload = { OUT_P_MSB_R };
    std::array<hal::byte, 3> pres_buffer;
    HAL_CHECK(hal::write_then_read(*m_i2c, device_address, pres_payload, pres_buffer, hal::never_timeout()));

    const uint32_t pressure_reading = uint32_t(pres_buffer[0]) << 16 
                                    | uint32_t(pres_buffer[1]) << 8 
                                    | uint32_t(pres_buffer[2]);
    
    return pressure_read_t {
        .pressure = static_cast<float>(pressure_reading) / pressure_conversion_factor;
    }
}

hal::result<altitude_read_t> mpl311::a_read()
{
    if (sensor_mode != ALTIMETER_M)
        setMode(ALTIMETER_M);
    initiate_one_shot();
    while (!check_data_ready_flag())
        hal::delay(m_clk, 10ms);

    constexpr float altitude_conversion_factor = 65536.0f;

    // Read data from OUT_P_MSB_R, OUT_P_CSB_R, and OUT_P_LSB_R
    std::array<hal::byte, 1> alt_payload = { OUT_P_MSB_R };
    std::array<hal::byte, 3> alt_buffer;
    HAL_CHECK(hal::write_then_read(*m_i2c, device_address, alt_payload, alt_buffer, hal::never_timeout()));

    const uint32_t alt_reading = uint32_t(buffer[0]) << 24 
                               | uint32_t(buffer[1]) << 16 
                               | uint32_t(buffer[2]) << 8;

    return altitude_read_t {
        .altitude = static_cast<float>(alt_reading) / altitude_conversion_factor;
    }
}

/*
* @brief Set sea level pressure (Barometric input for altitude calculations)
    in BAR_IN_MSB_R and BAR_IN_LSB_R registers
* @param sea_level_pressure: Sea level pressure in Pa. Default is 101,326 Pa.
*/
void mpl311::set_sea_pressure(float sea_level_pressure)
{
    // divide by 2 to convert to 2 Pa per LSB
    uint16_t bar = sea_level_pressure / 2;

    // write result to register
    std::array<hal::byte, 3> slp_payload = { 
        BAR_IN_MSB_R, 
        bar >> 8,  // msb
        bar & 0xFF // lsb
    };

    HAL_CHECK(hal::write(*m_i2c, device_address, slp_payload, hal::never_timeout()));
}

/*
* @brief Set altitude offset in OFF_H_R
* @param offset Offset value in meters, from -127 to 128
*/
void mpl311::set_altitude_offset(int8_t offset)
{
    std::array<hal::byte, 2> offset_payload = { OFF_H_R, hal::byte(offset) };
    HAL_CHECK(hal::write(*m_i2c, device_address, offset_payload, hal::never_timeout()));
}

}  // namespace hal::mpl311
