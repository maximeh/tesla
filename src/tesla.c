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

#include "tesla.h"
#include "rrd_helpers.h"
#include <getopt.h>  // for getopt, optind
#include <limits.h>  // for PATH_MAX
#include <signal.h>  // for signal, SIGINT, sig_atomic_t
#include <stdio.h>   // for fprintf, stderr, NULL, fputs, stdout
#include <stdlib.h>  // for calloc, free, realpath
#include <string.h>  // for memcmp, memcpy
#include <time.h>    // for tm, mktime, gmtime, strftime, time_t
#include <unistd.h>  // for access, F_OK, ssize_t

int _debug = 0;

volatile sig_atomic_t stop = 0;

void
sigint_handler(const int sig)
{
	stop = 1;
}

static int
append_record_rrd(const char *dbpath, struct record_data *rec)
{
	time_t epoch;
	struct tm *time_utc;

	epoch = mktime(&(rec->date));
	if (epoch == -1) {
		fprintf(stderr, "ERROR: mktime detected an error.\n");
		return -1;
	}

	/* Convert to UTC (That's what RRD eats, it will convert to local
	 * timezone using tz env variable) */
	time_utc = gmtime(&epoch);
	epoch = mktime(time_utc);
	if (epoch == -1) {
		fprintf(stderr, "ERROR: mktime detected an error.\n");
		return -1;
	}

	/* We don't really care if it fails, we'll try again and that's it */
	RRD_update(dbpath, rec->watts, epoch);
	return 0;
}

static void
decode(const uint_fast8_t *frame, struct record_data *rec)
{
	/* Device send only 13 for 2013, struct tm, take its year from 1900 */
	rec->date.tm_year = frame[1] + 100;
	/* struct tm is 0-11, the device send 1-12 */
	rec->date.tm_mon = frame[2] - 1;
	rec->date.tm_mday = frame[3];
	rec->date.tm_hour = frame[4];
	rec->date.tm_min = frame[5];
	/* mean intensity during one minute */
	rec->amps = (frame[8] + (frame[9] << 8)) * 0.07;
	/* mean power during one minute */
	rec->watts = rec->amps * VOLT;
}

static int
scan_usb(libusb_context *ctx, libusb_device_handle **dev_handle)
{
	libusb_device **devs;

	const int ret = libusb_init(&ctx);
	if (ret < 0) {
		fprintf(stderr, "%s\n", libusb_strerror(ret));
		return -1;
	}
	libusb_set_debug(ctx, _debug > 3 ? 3 : _debug);

	const ssize_t cnt = libusb_get_device_list(ctx, &devs);
	if (cnt == 0) {
		fprintf(stderr, "Could not get device list: %s\n",
				libusb_strerror(ret));
		return -1;
	}

	*dev_handle = libusb_open_device_with_vid_pid(ctx,
			OWL_VENDOR_ID, CM160_DEV_ID);
	libusb_free_device_list(devs, 1);
	if (!*dev_handle) {
		fprintf(stderr, "Could not find an OWL CM160 plugged.\n");
		return -1;
	}
	return 0;
}

static int
init_history(struct record_history **history)
{
	*history = calloc(1, sizeof(**history));
	if (!*history) {
		fprintf(stderr, "ERROR: Could not init history.\n");
		return -1;
	}

	(*history)->records = calloc(HISTORY_SIZE, sizeof(*(*history)->records));
	if (!(*history)->records) {
		fprintf(stderr, "ERROR: Could not init history->records.\n");
		return -1;
	}

	(*history)->recording_history = 1;
	(*history)->nb_record = 0;
	return 0;
}

static void
free_history_record(struct record_history **history)
{
	free((*history)->records);
	(*history)->records = NULL;
}

static void
free_history(struct record_history **history)
{
	free_history_record(history);
	free(*history);
	*history = NULL;
}

static int
write_history_rrd(const char *dbpath, struct record_history *hist)
{
	uint_fast16_t i;
	int ret = 0;
	hist->recording_history = 0;

	for (i = 0; i < hist->nb_record; ++i) {
		DPRINTF(1, "Write history... (%lu/%lu stored)\n",
				i, hist->nb_record);
		if (append_record_rrd(dbpath, hist->records + i)) {
			fprintf(stderr, "ERROR: Could not dump current data.\n");
			ret = -1;
			goto free_error;
		}
	}

	ret = 0;

free_error:
	free_history_record(&hist);
	return ret;
}

static int
append_record_history(struct record_history *hist, struct record_data *rec)
{
	DPRINTF(1, "Recording history... (%lu stored)\n", hist->nb_record);
	memcpy(hist->records+hist->nb_record, rec, sizeof(*rec));
	hist->nb_record++;
	return 0;
}

static int
frame_valid(uint_fast8_t * frame)
{
	uint_fast8_t checksum = 0;
	int i;

	// Compute checksum (sumup the 10 first elements)
	for (i = 0; i < 10; ++i)
		checksum += frame[i];

	// Leave only the least significant byte
	checksum &= 0xff;

	// Checksum should be egal the 10nth element of the frame
	if (checksum != frame[10]) {
		fprintf(stderr,
			"ERROR: Invalid checksum: expected 0x%x, got 0x%x\n",
			frame[10], checksum);
		return -1;
	}
	return 0;
}

static int
ack_frame(libusb_device_handle *dev_handle, uint_fast8_t *frame)
{
	int transferred, ret = 0;
	uint_fast8_t data = 0;

	if (!memcmp(frame, EMPTY_MSG, 11)) {
		DPRINTF(2, "received EMPTY MSG\n");
		return 1;
	} else if (!memcmp(frame, ID_MSG, 11)) {
		DPRINTF(2, "received ID MSG\n");
		data = 0x5A;
	} else if (!memcmp(frame, WAIT_MSG, 11)) {
		DPRINTF(2, "received WAIT MSG\n");
		data = 0xA5;
	} else {
		return 0;
	}

	ret = libusb_bulk_transfer(dev_handle, BULK_EP_OUT,
			&data, sizeof(data), &transferred, 1000);
	if (ret < 0) {
		fprintf(stderr, "ERROR: bulk_write returned %d (%s)\n",
				ret, libusb_strerror(ret));
		return -1;
	}
	DPRINTF(2, "Wrote %d bytes: 0x%02x\n", transferred, data);
	return 1;
}

static int
process_frame(const char *dbpath, uint_fast8_t *frame,
		struct record_history *hist, libusb_device_handle *dev_handle)
{
	int i, ret = 0;
	static int last_valid_month = 0;

	for (i = 0; i < 73; ++i)
		DPRINTF(2, "-");
	DPRINTF(2, "-\n");
	for (i = 0; i < 10; ++i)
		DPRINTF(2, "0x%02x - ", frame[i]);
	DPRINTF(2, "0x%02x\n", frame[10]);

	/* If a frame is acked, we need to treat the next one */
	ret = ack_frame(dev_handle, frame);
	if (ret < 0)
		return ret;
	if (ret > 0)
		return 0;

	/* Either the history dump is finished (we have received a live frame)
	 * or the history is full */
	if ((hist->recording_history && frame[0] == FRAME_ID_LIVE)
			|| hist->nb_record >= HISTORY_SIZE)
		return write_history_rrd(dbpath, hist);

	/* The frame is history, but we are not recording/dumping it, so we
	 * don't care. Wait for the next frame. */
	if (hist->recording_history == 0 && frame[0] == FRAME_ID_DB) {
		fprintf(stderr, "Received DB frame, expected LIVE frame.\n");
		fprintf(stderr, "WARNING: Invalid ID 0x%x\n", frame[0]);
		return 0;
	}

	if (frame_valid(frame))
		return -1;

	struct record_data rec = {0};
	decode(frame, &rec);

	/* There seem to be an issues with the month, some time they wind up
	 * getting crazy value. Don't know why... */
	if (rec.date.tm_mon < 0 || rec.date.tm_mon > 11)
		rec.date.tm_mon = last_valid_month;
	else
		last_valid_month = rec.date.tm_mon;

	char buf[18];
	strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M", &(rec.date));
	DPRINTF(2, "DATA: %s : %lu W\n", buf, rec.watts);
	if (hist->recording_history)
		return append_record_history(hist, &rec);

	if (append_record_rrd(dbpath, &rec)) {
		fprintf(stderr, "ERROR: Could not append current record.\n");
		ret = -1;
	}

	return ret;
}

static int
get_data(const char* dbpath, struct record_history *hist,
		libusb_device_handle *dev_handle)
{
	/* The buffer size is arbitrary */
	uint_fast8_t buffer[512] = {0};
	uint_fast8_t *bufptr = &buffer[0];
	int transferred;

	/* The call here is blocking and we did set an infinite timeout. The
	 * device is sending wait message regularly, so we can just loop on
	 * this without consumming CPU nor needing to poll(). */
	const int ret = libusb_bulk_transfer(dev_handle, BULK_EP_IN, buffer,
			sizeof(buffer), &transferred, 0);
	if (ret) {
		fprintf(stderr, "ERROR: bulk_read returned %d (%s)\n", ret,
				libusb_strerror(ret));
		return -1;
	}
	DPRINTF(2, "read %d bytes.\n", transferred);

	/* incomplete frames are resent */
	int nb_frames = transferred / FRAME_SIZE;

	DPRINTF(2, "Treat nb frames: %d\n", nb_frames);
	while (nb_frames--) {
		if (process_frame(dbpath, bufptr, hist, dev_handle)) {
			fprintf(stderr, "ERROR: process detected an error.\n");
			return -1;
		}
		bufptr += FRAME_SIZE;
	}
	return 0;
}

static int
prepare_device(libusb_device_handle *dev_handle)
{
	if (libusb_kernel_driver_active(dev_handle, INTERFACE)) {
		DPRINTF(2, "Kernel Driver Active.\n");
		if (libusb_detach_kernel_driver(dev_handle, 0)) {
			fprintf(stderr, "Could not detach the interface.\n");
			return -1;
		}
	}

	libusb_set_configuration(dev_handle, CONFIGURATION);
	if (libusb_claim_interface(dev_handle, INTERFACE)) {
		fprintf(stderr, "Could not claim the interface.\n");
		return -1;
	}

	DPRINTF(2, "Claimed interface.\n");

	/* Set the baudrate at the correct speed to talk to the device */
	int baudrate = 250000;
	libusb_control_transfer(dev_handle, REQTYPE,
			CP210X_IFC_ENABLE, UART_ENABLE, 0, NULL, 0, 500);
	libusb_control_transfer(dev_handle, REQTYPE,
			CP210X_SET_BAUDRATE, 0, 0,
			(void *)&baudrate, sizeof(baudrate), 500);
	libusb_control_transfer(dev_handle, REQTYPE,
			CP210X_IFC_ENABLE, UART_DISABLE, 0, NULL, 0, 500);
	return 0;
}

static void
usage(void)
{
	(void)fprintf(stdout, "tesla [OPTIONS] [RRD DATABASE PATH]\n");
	fputs("\t-d\t\tDebug output\n"
			"\t-h\t\tThis usage statement\n", stdout);
}

int
main(int argc, char **argv)
{
	char dbpath[PATH_MAX] = "/tmp/tesla.rrd";
	libusb_context *ctx = NULL;
	libusb_device_handle *dev_handle = NULL;

	signal(SIGINT, sigint_handler);

	int c = 0;
	while ((c = getopt(argc, argv, "hd")) != -1) {
		switch (c) {
		case 'd':
			++_debug;
			break;
		case 'h':
		default:
			usage();
			return 0;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 1)
		realpath(argv[0], dbpath);

	if (access(dbpath, F_OK) == -1) {
		DPRINTF(1, "Creating db: %s\n", dbpath);
		if (RRD_create(dbpath, 60))
			return -1;
	}
	DPRINTF(2, "Using DB: %s\n", dbpath);

	DPRINTF(1, "Please plug your CM160 device...\n");
	if (scan_usb(ctx, &dev_handle))
		return 1;

	if (prepare_device(dev_handle))
		return 1;

	DPRINTF(1, "Start acquiring data...\n");

	struct record_history *hist;
	if (init_history(&hist))
		return 1;

	while (!stop)
		if (get_data(dbpath, hist, dev_handle))
			stop = 1;

	DPRINTF(2, "\nClosing connection with the device\n");
	if (libusb_release_interface(dev_handle, INTERFACE))
		fprintf(stderr, "Cannot release interface.\n");
	libusb_reset_device(dev_handle);
	libusb_close(dev_handle);
	libusb_exit(ctx);

	free_history(&hist);
	return 0;
}
