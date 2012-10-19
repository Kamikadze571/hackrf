/*
 * Copyright 2012 Jared Boone <jared@sharebrained.com>
 *
 * This file is part of HackRF.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "hackrf.h"

#include <stdlib.h>

#include <libusb.h>
#include <pthread.h>

// TODO: Factor this into a shared #include so that firmware can use
// the same values.
typedef enum {
	HACKRF_VENDOR_REQUEST_SET_TRANSCEIVER_MODE = 1,
	HACKRF_VENDOR_REQUEST_MAX2837_WRITE = 2,
	HACKRF_VENDOR_REQUEST_MAX2837_READ = 3,
	HACKRF_VENDOR_REQUEST_SI5351C_WRITE = 4,
	HACKRF_VENDOR_REQUEST_SI5351C_READ = 5,
	HACKRF_VENDOR_REQUEST_SAMPLE_RATE_SET = 6,
	HACKRF_VENDOR_REQUEST_BASEBAND_FILTER_BANDWIDTH_SET = 7,
} hackrf_vendor_request;

typedef enum {
	HACKRF_TRANSCEIVER_MODE_RECEIVE = 1,
	HACKRF_TRANSCEIVER_MODE_TRANSMIT = 2,
} hackrf_transceiver_mode;

struct hackrf_device {
	libusb_device_handle* usb_device;
	struct libusb_transfer** transfers;
	hackrf_sample_block_cb_fn callback;
	pthread_t transfer_thread;
	uint32_t transfer_count;
	uint32_t buffer_size;
	bool streaming;
};

static const uint16_t hackrf_usb_vid = 0x1d50;
static const uint16_t hackrf_usb_pid = 0x604b;

static libusb_context* g_libusb_context = NULL;

static int free_transfers(hackrf_device* device) {
	if( device->transfers != NULL ) {
		// libusb_close() should free all transfers referenced from this array.
		for(uint32_t transfer_index=0; transfer_index<device->transfer_count; transfer_index++) {
			if( device->transfers[transfer_index] != NULL ) {
				libusb_free_transfer(device->transfers[transfer_index]);
				device->transfers[transfer_index] = NULL;
			}
		}
		free(device->transfers);
		device->transfers = NULL;
	}
	return HACKRF_SUCCESS;
}

static int allocate_transfers(hackrf_device* const device) {
	if( device->transfers == NULL ) {
	    device->transfers = calloc(device->transfer_count, sizeof(struct libusb_transfer));
		if( device->transfers == NULL ) {
			return HACKRF_ERROR_NO_MEM;
		}
	
	    for(uint32_t transfer_index=0; transfer_index<device->transfer_count; transfer_index++) {
	        device->transfers[transfer_index] = libusb_alloc_transfer(0);
	        if( device->transfers[transfer_index] == NULL ) {
	            return HACKRF_ERROR_LIBUSB;
	        }
        
			libusb_fill_bulk_transfer(
				device->transfers[transfer_index],
				device->usb_device,
				0,
				(unsigned char*)malloc(device->buffer_size),
				device->buffer_size,
				NULL,
				device,
				0
			);
        
	        if( device->transfers[transfer_index]->buffer == NULL ) {
	            return HACKRF_ERROR_NO_MEM;
	        }
	    }
	
		return HACKRF_SUCCESS;
	} else {
		return HACKRF_ERROR_BUSY;
	}
}

static int prepare_transfers(
	hackrf_device* device,
	const uint_fast8_t endpoint_address,
	libusb_transfer_cb_fn callback
) {
	if( device->transfers != NULL ) {
	    for(uint32_t transfer_index=0; transfer_index<device->transfer_count; transfer_index++) {
			device->transfers[transfer_index]->endpoint = endpoint_address;
			device->transfers[transfer_index]->callback = callback;
			
	        int error = libusb_submit_transfer(device->transfers[transfer_index]);
	        if( error != 0 ) {
	            return HACKRF_ERROR_LIBUSB;
			}
		}
		return HACKRF_SUCCESS;
	} else {
		// This shouldn't happen.
		return HACKRF_ERROR_OTHER;
	}
}
/*
static int cancel_transfers(hackrf_device* device) {
	if( device->transfers != NULL ) {
	    for(uint32_t transfer_index=0; transfer_index<device->transfer_count; transfer_index++) {
			libusb_cancel_transfer(device->transfers[transfer_index]);
		}
		return HACKRF_SUCCESS;
	} else {
		// This shouldn't happen.
		return HACKRF_ERROR_OTHER;
	}
}
*/
int hackrf_init() {
	const int libusb_error = libusb_init(&g_libusb_context);
	if( libusb_error != 0 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		return HACKRF_SUCCESS;
	}
}

int hackrf_exit() {
	if( g_libusb_context != NULL ) {
		libusb_exit(g_libusb_context);
		g_libusb_context = NULL;
	}
	
	return HACKRF_SUCCESS;
}

int hackrf_open(hackrf_device** device) {
	if( device == NULL ) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	
	// TODO: Do proper scanning of available devices, searching for
	// unit serial number (if specified?).
    libusb_device_handle* usb_device = libusb_open_device_with_vid_pid(g_libusb_context, hackrf_usb_vid, hackrf_usb_pid);
    if( usb_device == NULL ) {
        return HACKRF_ERROR_NOT_FOUND;
    }
    
    //int speed = libusb_get_device_speed(usb_device);
	// TODO: Error or warning if not high speed USB?

    int result = libusb_set_configuration(usb_device, 1);
    if( result != 0 ) {
    	libusb_close(usb_device);
        return HACKRF_ERROR_LIBUSB;
    }

    result = libusb_claim_interface(usb_device, 0);
    if( result != 0 ) {
    	libusb_close(usb_device);
        return HACKRF_ERROR_LIBUSB;
    }

	hackrf_device* lib_device = NULL;
	lib_device = malloc(sizeof(*lib_device));
	if( lib_device == NULL ) {
		libusb_release_interface(usb_device, 0);
    	libusb_close(usb_device);
		return HACKRF_ERROR_NO_MEM;
	}
	
	lib_device->usb_device = usb_device;
	lib_device->transfers = NULL;
	lib_device->callback = NULL;
	lib_device->transfer_thread = 0;
	lib_device->transfer_count = 1024;
	lib_device->buffer_size = 16384;
	lib_device->streaming = false;
	
	result = allocate_transfers(lib_device);
	if( result != 0 ) {
		free(lib_device);
		libusb_release_interface(usb_device, 0);
    	libusb_close(usb_device);
		return HACKRF_ERROR_NO_MEM;
	}
	
	*device = lib_device;
	
	return HACKRF_SUCCESS;
}

static int hackrf_set_transceiver_mode(hackrf_device* device, hackrf_transceiver_mode value) {
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		HACKRF_VENDOR_REQUEST_SET_TRANSCEIVER_MODE,
		value,
		0,
		NULL,
		0,
		0
	);
	
	if( result != 0 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		return HACKRF_SUCCESS;
	}
}

int hackrf_max2837_read(hackrf_device* device, uint8_t register_number, uint16_t* value) {
	if( register_number >= 32 ) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		HACKRF_VENDOR_REQUEST_MAX2837_READ,
		0,
		register_number,
		(unsigned char*)value,
		2,
		0
	);
	
	if( result < 2 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		return HACKRF_SUCCESS;
	}
}

int hackrf_max2837_write(hackrf_device* device, uint8_t register_number, uint16_t value) {
	if( register_number >= 32 ) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	if( value >= 0x400 ) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		HACKRF_VENDOR_REQUEST_MAX2837_WRITE,
		value,
		register_number,
		NULL,
		0,
		0
	);
	
	if( result != 0 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		return HACKRF_SUCCESS;
	}
}

int hackrf_si5351c_read(hackrf_device* device, uint16_t register_number, uint16_t* value) {
	if( register_number >= 256 ) {
		return HACKRF_ERROR_INVALID_PARAM;
	}

	uint8_t temp_value = 0;
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		HACKRF_VENDOR_REQUEST_SI5351C_READ,
		0,
		register_number,
		(unsigned char*)&temp_value,
		1,
		0
	);
	
	if( result < 1 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		*value = temp_value;
		return HACKRF_SUCCESS;
	}
}

int hackrf_si5351c_write(hackrf_device* device, uint16_t register_number, uint16_t value) {
	if( register_number >= 256 ) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	if( value >= 256 ) {
		return HACKRF_ERROR_INVALID_PARAM;
	}
	
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		HACKRF_VENDOR_REQUEST_SI5351C_WRITE,
		value,
		register_number,
		NULL,
		0,
		0
	);
	
	if( result != 0 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		return HACKRF_SUCCESS;
	}
}

int hackrf_sample_rate_set(hackrf_device* device, const uint32_t sampling_rate_hz) {
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		HACKRF_VENDOR_REQUEST_SAMPLE_RATE_SET,
		sampling_rate_hz & 0xffff,
		sampling_rate_hz >> 16,
		NULL,
		0,
		0
	);
	
	if( result != 0 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		return HACKRF_SUCCESS;
	}
}

int hackrf_baseband_filter_bandwidth_set(hackrf_device* device, const uint32_t bandwidth_hz) {
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		HACKRF_VENDOR_REQUEST_BASEBAND_FILTER_BANDWIDTH_SET,
		bandwidth_hz & 0xffff,
		bandwidth_hz >> 16,
		NULL,
		0,
		0
	);
	
	if( result != 0 ) {
		return HACKRF_ERROR_LIBUSB;
	} else {
		return HACKRF_SUCCESS;
	}
}

static void* transfer_threadproc(void* arg) {
	hackrf_device* device = (hackrf_device*)arg;
	
    struct timeval timeout = { 0, 500000 };

    while( device->streaming ) {
        int error = libusb_handle_events_timeout(g_libusb_context, &timeout);
        if( error != 0 ) {
			device->streaming = false;
        }
    }
	
	return NULL;
}

static void hackrf_libusb_transfer_callback(struct libusb_transfer* usb_transfer) {
	hackrf_device* device = (hackrf_device*)usb_transfer->user_data;

    if( usb_transfer->status == LIBUSB_TRANSFER_COMPLETED ) {
		hackrf_transfer transfer = {
			.device = device,
			.buffer = usb_transfer->buffer,
			.buffer_length = usb_transfer->length,
			.valid_length = usb_transfer->actual_length,
		};

		if( device->callback(&transfer) == 0 ) {
	    	libusb_submit_transfer(usb_transfer);
			return;
		}
	}

	device->streaming = false;
}

static int kill_transfer_thread(hackrf_device* device) {
	device->streaming = false;
	
	if( device->transfer_thread != 0 ) {
		void* value = NULL;
		int result = pthread_join(device->transfer_thread, &value);
		if( result != 0 ) {
			return HACKRF_ERROR_THREAD;
		}
		device->transfer_thread = 0;
	}

	return HACKRF_SUCCESS;
}

static int create_transfer_thread(
	hackrf_device* device,
	const uint8_t endpoint_address,
	hackrf_sample_block_cb_fn callback
) {
	if( device->transfer_thread == 0 ) {
		int result = prepare_transfers(
			device, endpoint_address,
			hackrf_libusb_transfer_callback
		);
		if( result != HACKRF_SUCCESS ) {
			return result;
		}
	
		device->callback = callback;
		device->streaming = true;

		result = pthread_create(&device->transfer_thread, 0, transfer_threadproc, device);
		if( result != 0 ) {
			return HACKRF_ERROR_THREAD;
		}
	} else {
		return HACKRF_ERROR_BUSY;
	}
	
	return HACKRF_SUCCESS;
}

bool hackrf_is_streaming(hackrf_device* device) {
	return device->streaming;
}

int hackrf_start_rx(hackrf_device* device, hackrf_sample_block_cb_fn callback) {
	const uint8_t endpoint_address = LIBUSB_ENDPOINT_IN | 1;
	int result = hackrf_set_transceiver_mode(device, HACKRF_TRANSCEIVER_MODE_RECEIVE);
	if( result == HACKRF_SUCCESS ) {
		create_transfer_thread(device, endpoint_address, callback);
	}
	return result;
}

int hackrf_stop_rx(hackrf_device* device) {
	return kill_transfer_thread(device);
}

int hackrf_start_tx(hackrf_device* device, hackrf_sample_block_cb_fn callback) {
	const uint8_t endpoint_address = LIBUSB_ENDPOINT_OUT | 2;
	int result = hackrf_set_transceiver_mode(device, HACKRF_TRANSCEIVER_MODE_TRANSMIT);
	if( result == HACKRF_SUCCESS ) {
		result = create_transfer_thread(device, endpoint_address, callback);
	}
	return result;
}

int hackrf_stop_tx(hackrf_device* device) {
	return kill_transfer_thread(device);
}

int hackrf_close(hackrf_device* device) {
	if( device != NULL ) {
		int result = hackrf_stop_rx(device);
		if( result ) {
			return result;
		}
	
		result = hackrf_stop_tx(device);
		if( result ) {
			return result;
		}
	
		if( device->usb_device != NULL ) {
		    result = libusb_release_interface(device->usb_device, 0);
		    if( result ) {
		        return HACKRF_ERROR_LIBUSB;
		    }
    
		    libusb_close(device->usb_device);
			
			device->usb_device = NULL;
		}
		
		free_transfers(device);
		
		free(device);
	}
	
	return HACKRF_SUCCESS;
}

const char* hackrf_error_name(enum hackrf_error errcode) {
	switch(errcode) {
	case HACKRF_SUCCESS:
		return "HACKRF_SUCCESS";
		
	case HACKRF_ERROR_INVALID_PARAM:
		return "HACKRF_ERROR_INVALID_PARAM";
	
	case HACKRF_ERROR_NOT_FOUND:
		return "HACKRF_ERROR_NOT_FOUND";
	
	case HACKRF_ERROR_BUSY:
		return "HACKRF_ERROR_BUSY";
		
	case HACKRF_ERROR_NO_MEM:
		return "HACKRF_ERROR_NO_MEM";
	
	case HACKRF_ERROR_LIBUSB:
		return "HACKRF_ERROR_LIBUSB";
	
	case HACKRF_ERROR_THREAD:
		return "HACKRF_ERROR_THREAD";
	
	case HACKRF_ERROR_OTHER:
		return "HACKRF_ERROR_OTHER";
	
	default:
		return "HACKRF unknown error";
	}
}
