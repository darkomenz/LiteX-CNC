/********************************************************************
* Description:  encoder.c
*               A Litex-CNC component that canm be used to measure 
*               position by counting the pulses generated by a 
*               quadrature encoder. 
*
* Author: Peter van Tol <petertgvantol AT gmail DOT com>
* License: GPL Version 2
*    
* Copyright (c) 2022 All rights reserved.
*
********************************************************************/

/** This program is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General
    Public License as published by the Free Software Foundation.
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    THE AUTHORS OF THIS LIBRARY ACCEPT ABSOLUTELY NO LIABILITY FOR
    ANY HARM OR LOSS RESULTING FROM ITS USE.  IT IS _EXTREMELY_ UNWISE
    TO RELY ON SOFTWARE ALONE FOR SAFETY.  Any machinery capable of
    harming persons must have provisions for completely removing power
    from all motors, etc, before persons enter any danger area.  All
    machinery must be designed to comply with local and national safety
    codes, and the authors of this software can not, and do not, take
    any responsibility for such compliance.

    This code was written as part of the LiteX-CNC project.
*/
#include <stdio.h>
#include <limits.h>

#include "rtapi.h"
#include "rtapi_app.h"
#include "litexcnc.h"

#include "encoder.h"


int litexcnc_encoder_init(litexcnc_t *litexcnc, cJSON *config) {

    // Declarations
    int r;
    size_t i;
    const cJSON *encoder_config = NULL;
    const cJSON *encoder_instance_config = NULL;
    const cJSON *encoder_instance_name = NULL;
    char base_name[HAL_NAME_LEN + 1];   // i.e. <board_name>.<board_index>.pwm.<pwm_name>
    char name[HAL_NAME_LEN + 1];        // i.e. <base_name>.<pin_name>

    // Parse the contents of the config-json
    encoder_config = cJSON_GetObjectItemCaseSensitive(config, "encoders");
    if (cJSON_IsArray(encoder_config)) {
        // Store the amount of encoder instances on this board
        litexcnc->encoder.num_instances = cJSON_GetArraySize(encoder_config);
        litexcnc->config.num_encoder_instances = litexcnc->encoder.num_instances;
        
        // Allocate the module-global HAL shared memory
        litexcnc->encoder.instances = (litexcnc_encoder_instance_t *)hal_malloc(litexcnc->encoder.num_instances * sizeof(litexcnc_encoder_instance_t));
        if (litexcnc->encoder.instances == NULL) {
            LITEXCNC_ERR_NO_DEVICE("Out of memory!\n");
            r = -ENOMEM;
            return r;
        }

        // Create the pins and params in the HAL
        i = 0;
        cJSON_ArrayForEach(encoder_instance_config, encoder_config) {
            // Get pointer to the encoder instance
            litexcnc_encoder_instance_t *instance = &(litexcnc->encoder.instances[i]);
            
            // Create the basename
            encoder_instance_name = cJSON_GetObjectItemCaseSensitive(encoder_instance_config, "name");
            if (cJSON_IsString(encoder_instance_name) && (encoder_instance_name->valuestring != NULL)) {
                rtapi_snprintf(base_name, sizeof(base_name), "%s.encoder.%s", litexcnc->fpga->name, encoder_instance_name->valuestring);
            } else {
                rtapi_snprintf(base_name, sizeof(base_name), "%s.encoder.%02zu", litexcnc->fpga->name, i);
            }

            // Create the pins
            // - counts
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "counts"); 
            r = hal_pin_s32_new(name, HAL_IN, &(instance->hal.pin.counts), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // - index_enable
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "index_enable"); 
            r = hal_pin_bit_new(name, HAL_IN, &(instance->hal.pin.index_enable), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // - index_pulse
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "index_pulse"); 
            r = hal_pin_bit_new(name, HAL_OUT, &(instance->hal.pin.index_pulse), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // - position
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "position"); 
            r = hal_pin_float_new(name, HAL_OUT, &(instance->hal.pin.position), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // - reset
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "reset"); 
            r = hal_pin_bit_new(name, HAL_IN, &(instance->hal.pin.reset), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // - velocity
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "velocity"); 
            r = hal_pin_float_new(name, HAL_OUT, &(instance->hal.pin.velocity), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // - velocity_rpm
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "velocity_rpm"); 
            r = hal_pin_float_new(name, HAL_OUT, &(instance->hal.pin.velocity_rpm), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // - velocity_rpm
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "overflow_occurred"); 
            r = hal_pin_bit_new(name, HAL_OUT, &(instance->hal.pin.overflow_occurred), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_pins; }
            // Create the params
            // - position_scale
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "position_scale"); 
            r = hal_param_float_new(name, HAL_RW, &(instance->hal.param.position_scale), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_params; }
            // - position_scale
            rtapi_snprintf(name, sizeof(name), "%s.%s", base_name, "x4_mode"); 
            r = hal_param_bit_new(name, HAL_RW, &(instance->hal.param.x4_mode), litexcnc->fpga->comp_id);
            if (r < 0) { goto fail_params; }
            
            // Increase counter to proceed to the next encoder
            i++;
        }
    }

    return 0;

fail_pins:
    LITEXCNC_ERR_NO_DEVICE("Error adding pin '%s', aborting\n", name);
    return r;

fail_params:
    LITEXCNC_ERR_NO_DEVICE("Error adding param '%s', aborting\n", name);
    return r;
}


uint8_t litexcnc_encoder_prepare_write(litexcnc_t *litexcnc, uint8_t **data, long period) {
    /* ENCODER PREPARE WRITE
     * This function assembles the data which is written to the FPGA:
     *  - the `index enable`-flag, as set by the hal pin;
     *  - the `reset index pulse`-flag. At this moment the index pulse is automatically
     *    reset by the driver as soon as the pin_Z is HIGH has been read. This means that
     *    during the next cycle of the thread the pin will be read as LOW. Possibly this
     *    method can be refined optionally let the user reset the pin_Z manually. This
     *    might be necessary when there are two parallel threads running at the same time. 
     *
     * When there are no encoders defined, this function direclty returns.
     */

    // Sanity check: are there any instances of the encoder defined in the config?
    if (litexcnc->encoder.num_instances == 0) {
        return 0;
    }

    // Declaration of shared variables
    uint8_t mask;

    // Index enable (shared register)
    mask = 0x80;
    for (size_t i=LITEXCNC_BOARD_ENCODER_SHARED_INDEX_ENABLE_WRITE_SIZE(litexcnc)*8; i>0; i--) {
        // The counter i can have a value outside the range of possible instances. We only
        // should add data from existing instances
        if (i < litexcnc->encoder.num_instances) {
            *(*data) |= *(litexcnc->encoder.instances[i-1].hal.pin.index_enable)?mask:0;
        }
        // Modify the mask for the next. When the mask is zero (happens in case of a 
        // roll-over), we should proceed to the next byte and reset the mask.
        mask >>= 1;
        if (!mask) {
            mask = 0x80;  // Reset the mask
            (*data)++; // Proceed the buffer to the next element
        }
    }

    // Reset index pulse (shared register)
    mask = 0x80;
    for (size_t i=LITEXCNC_BOARD_ENCODER_SHARED_RESET_INDEX_PULSE_WRITE_SIZE(litexcnc)*8; i>0; i--) {
        // The counter i can have a value outside the range of possible instances. We only
        // should add data from existing instances
        if (i < litexcnc->encoder.num_instances) {
            *(*data) |= *(litexcnc->encoder.instances[i-1].hal.pin.index_pulse)?mask:0;
        }
        // Modify the mask for the next. When the mask is zero (happens in case of a 
        // roll-over), we should proceed to the next byte and reset the mask.
        mask >>= 1;
        if (!mask) {
            mask = 0x80;  // Reset the mask
            (*data)++; // Proceed the buffer to the next element
        }
    }

    return 0;
}


uint8_t litexcnc_encoder_process_read(litexcnc_t *litexcnc, uint8_t** data, long period) {
    /* ENCODER PROCESS READ
     *
     */

    // Global check for changed variables and pre-calucating data
    if (litexcnc->encoder.memo.period != period) { 
        // - Calculate the reciprocal once here, to avoid multiple divides later
        litexcnc->encoder.data.recip_dt = 1.0 / (period * 0.000000001);
        litexcnc->encoder.memo.period = period;
    }
        
    // Sanity check: are there any instances of the encoder defined in the config?
    if (litexcnc->encoder.num_instances == 0) {
        return 0;
    }

    // Declaration of shared variables
    uint8_t mask;

    // Index pulse (shared register)
    mask = 0x80;
    for (size_t i=LITEXCNC_BOARD_ENCODER_SHARED_INDEX_PULSE_READ_SIZE(litexcnc)*8; i>0; i--) {
        // The counter i can have a value outside the range of possible instances. We only
        // should add data to existing instances
        if (i < litexcnc->encoder.num_instances) {
            uint8_t index_pulse = (*(*data) & mask)?1:0;
            // Reset the index enable on positive edge of the index pulse
            // NOTE: the FPGA only sets the index pulse when a raising flank has been detected
            if (index_pulse) {
                *(litexcnc->encoder.instances[i-1].hal.pin.index_enable) = 0;
            }
            // Set the index pulse
            *(litexcnc->encoder.instances[i-1].hal.pin.index_pulse) = index_pulse;
        }
        // Modify the mask for the next. When the mask is zero (happens in case of a 
        // roll-over), we should proceed to the next byte and reset the mask.
        mask >>= 1;
        if (!mask) {
            mask = 0x80;  // Reset the mask
            (*data)++; // Proceed the buffer to the next element
        }
    }
    
    // Process all instances:
    // - read data
    // - calculate derived data
    for (size_t i=0; i < litexcnc->encoder.num_instances; i++) {
        // Get pointer to the stepgen instance
        litexcnc_encoder_instance_t *instance = &(litexcnc->encoder.instances[i]);

        // Instance check for changed variables and pre-calucating data
        // - position scnale
        if (instance->hal.param.position_scale != instance->memo.position_scale) {
            // Prevent division by zero
            if ((instance->hal.param.position_scale > -1e-20) && (instance->hal.param.position_scale < 1e-20)) {
		        // Value too small, take a safe value
		        instance->hal.param.position_scale = 1.0;
	        }
            instance->data.position_scale_recip = 1.0 / instance->hal.param.position_scale;
            instance->memo.position_scale = instance->hal.param.position_scale; 
        }

        // Read the data and store it on the instance
        // - store the previous counts (required for roll-over detection)
        int32_t counts_old = *(instance->hal.pin.counts);
        // - convert received data to struct
        litexcnc_encoder_instance_read_data_t instance_data;
        memcpy(&instance_data, *data, sizeof(litexcnc_encoder_instance_read_data_t));
        *data += sizeof(litexcnc_encoder_instance_read_data_t);

        // - store the counts from the FPGA to the driver (keep in mind the endianess). Also
        //   take into account whether we are in x4_mode or not.
        if (instance->hal.param.x4_mode) {
            *(instance->hal.pin.counts) = (int32_t)be32toh((uint32_t)instance_data.counts);
        } else {
            *(instance->hal.pin.counts) = ((int32_t)be32toh((uint32_t)instance_data.counts)) / 4;
        }

        // Calculate the new position based on the counts
        // - store the previous position (requered for the velocity calculation)
        float position_old = *(instance->hal.pin.position);
        // - when an index pulse has been received the roll-over protection is disabled,
        //   as it is known the encoder is reset to 0 and it is not possible to roll-over
        //   within one period (assumption is that the period is less then 15 minutes).
        if (*(instance->hal.pin.index_pulse)) {
            *(instance->hal.pin.position) = *(instance->hal.pin.counts) * instance->data.position_scale_recip;
            *(instance->hal.pin.overflow_occurred) = false;
        } else {
            // Roll-over detection; it assumed when the the difference between previous value
            // and next value is larger then 0.5 times the max-range, that roll-over has occurred.
            // In this case, we switch to a incremental calculation of the position. This method is
            // less accurate then the absolute calculation of the position. Once overflow has
            // occurred, the only way to reset to absolute calculation is by the occurrence of a
            // index_pulse.
            int64_t difference = (int64_t) *(instance->hal.pin.counts) - counts_old;
            if ((difference < INT_MIN) || (difference > INT_MAX)) {
                // When overflow occurs, the difference will be in order magnitude of 2^32-1, however
                // a signed integer can only allow for changes of half that size. Because we calculate
                // in 64-bit, we can detect changes which are larger and thus correct for that.
                *(instance->hal.pin.overflow_occurred) = true;
                // Compensate for the roll-over
                if (difference < 0) {
                    difference += UINT_MAX;
                } else {
                    difference -= UINT_MAX;
                }
            }
            if (*(instance->hal.pin.overflow_occurred)) {
                *(instance->hal.pin.position) = *(instance->hal.pin.position) + difference * instance->data.position_scale_recip;
            } else {
                *(instance->hal.pin.position) = *(instance->hal.pin.counts) * instance->data.position_scale_recip;
            }

        }

        // Calculate the new speed based on the new position (running average). The
        // running average is not modified when an index-pulse is received, as this
        // means there is a large jump in position and thus to a large theoretical 
        // speed.
        size_t velocity_pointer = 0;
        if (!(*(instance->hal.pin.index_pulse))) {
            // Replace the element in the array
            instance->memo.velocity[velocity_pointer] = (*(instance->hal.pin.position) - position_old) * litexcnc->encoder.data.recip_dt;
            // Sum the array and divide by the size of the array
            float average = 0.0;
            for (size_t j=0; j < LITEXCNC_ENCODER_POSITION_AVERAGE_SIZE; j++) {average += instance->memo.velocity[j];};
            *(instance->hal.pin.velocity) = average * 0.125;
            *(instance->hal.pin.velocity_rpm) = *(instance->hal.pin.velocity) * 60.0;
            // Increase the pointer to the next element, revert to the beginning of
            // the array
            if (velocity_pointer++ >= sizeof(instance->memo.velocity)) {velocity_pointer=0;};
        }
    }

    return 0;
}