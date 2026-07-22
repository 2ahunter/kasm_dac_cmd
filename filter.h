#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>
#include <math.h>

/* filter values are in order of a0 b0*/
// Declarations only (tells the compiler these exist somewhere else)
extern const double filter_0[];
extern const double filter_250[];
extern const double filter_500[];
extern const double filter_1K[];
extern const double filter_2K[];

/**
 * @brief  Simple low pass filter of command data
 * @param  data The command value to be filtered. 
 * @param  coefficients: pointer to an array of filter coefficients
 * @return The filtered command value.
 * @note  This function requires an update period of 500 usec 
 * @author  Aaron Hunter
 */
uint16_t filter(uint16_t data, const double *coefficients);

#endif