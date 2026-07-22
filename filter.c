#include "filter.h"

static uint16_t y_k1 = 0; // Previous output value
/* filter values are in order of a0 b0*/
const double filter_2K[2] = {-0.8528642033144647, 0.1471}; // Coefficients for a 2KHz low pass filter
const double filter_1K[2] = {-0.9235064717231085, 0.07649}; // Coefficients for a 1KHz low pass filter
const double filter_500[2] = {-0.9609924410332833, 0.03901}; // Coefficients for a 500Hz low pass filter
const double filter_250[2] = {-0.9803022192330707, 0.0197}; // Coefficients for a 250Hz low pass filter
const double filter_0[2] = {0,1.0}; // no filtering

/**
 * @brief  Simple low pass filter of command data
 * @param  data The command value to be filtered. 
 * @param  coefficients: pointer to an array of filter coefficients
 * @return The filtered command value.
 * @author  Aaron Hunter
 */
uint16_t filter(uint16_t data, const double *coefficients){

    /* y = a0 * y(k-1) + b0 * u(k)*/
    uint16_t y = (uint16_t)(-coefficients[0] * (double) y_k1 + coefficients[1] * (double) data);
    y_k1 = y; // Update previous output value
    return y;
}