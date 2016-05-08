// vim: set expandtab:set tabstop=2
/*
 * tesla
 *
 * Copyright (C) 2012 Philippe Cornet <phil.cornet@gmail.com>
 * Copyright (C) 2013 Maxime Hadjinlian <maxime.hadjinlian@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#pragma once

#define _XOPEN_SOURCE 500
#include <libusb-1.0/libusb.h> // for libusb_endpoint_direction::LIBUSB_END...
#include <stdint.h>            // for uint8_t
#include <stdio.h>             // for fflush, printf, stdout
#include <time.h>              // for tm

#define DPRINTF(level, ...) \
	do { \
		if (_debug >= level) \
		printf(__VA_ARGS__); \
		fflush(stdout); \
	} while(0)

// DEV_ID and PID
#define OWL_VENDOR_ID 0x0fde
#define CM160_DEV_ID 0xca05

// Got them from the output of lsusb -v
#define BULK_EP_IN 0x82
#define BULK_EP_OUT 0x01

#define CONFIGURATION 1
#define INTERFACE 0

#define CP210X_IFC_ENABLE 0x00
#define CP210X_SET_BAUDRATE 0x1E

#define UART_ENABLE 0x0001
#define UART_DISABLE 0x0000

#define REQTYPE LIBUSB_REQUEST_TYPE_VENDOR | \
		LIBUSB_ENDPOINT_OUT | \
		LIBUSB_RECIPIENT_INTERFACE

// CM160 protocol
#define HISTORY_SIZE 65536 // 30 * 24 * 60 = 43200 theoric history size
#define FRAME_ID_LIVE 0x51
#define FRAME_ID_DB 0x59
#define MAX_FD 4
#define FRAME_SIZE 11 // in bytes
#define VOLT 230

struct record_data {
	struct tm date;
	uint_fast16_t watts;
	double amps;
};

struct record_history {
	struct record_data *records;
	uint_fast16_t nb_record;
	uint_fast8_t recording_history;
};

static const uint_fast8_t ID_MSG[FRAME_SIZE] = {
	0xA9, 0x49, 0x44, 0x54, 0x43, 0x4D, 0x56, 0x30, 0x30, 0x31, 0x01 };
static const uint_fast8_t WAIT_MSG[FRAME_SIZE] = {
	0xA9, 0x49, 0x44, 0x54, 0x57, 0x41, 0x49, 0x54, 0x50, 0x43, 0x52 };
static const uint_fast8_t EMPTY_MSG[FRAME_SIZE] = {
	0x59, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x50 };

extern int _debug;

void sigint_handler(const int sig);
