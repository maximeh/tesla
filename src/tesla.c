/* vim: set expandtab:set tabstop=2 */
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
#include <getopt.h>       // for getopt, optind
#include <limits.h>       // for PATH_MAX
#include <signal.h>       // for signal, SIGINT
#include <stddef.h>       // for size_t
#include <stdlib.h>       // for free, calloc, exit, realpath
#include <string.h>       // for strncmp, memset, strlen
#include <unistd.h>       // for access, F_OK
#include "rrd_helpers.h"  // for RRD_create, RRD_update

int _debug = 0;

volatile sig_atomic_t stop = 0;

void
sigint_handler(const int sig)
{
        stop = 1;
}

static int
dump_data(char *dbpath, struct record_data *rec)
{
        time_t epoch;
        struct tm *time_utc;

        epoch = mktime(&(rec->date));
        if (epoch == -1) {
                fprintf(stderr, "ERROR: mktime detected an error.\n");
                return -1;
        }

        /* Convert to UTC (That's what RRD eats, it will convert to local
           timezone using tz env variable) */
        time_utc = gmtime(&epoch);
        epoch = mktime(time_utc);
        if (epoch == -1) {
                fprintf(stderr, "ERROR: mktime detected an error.\n");
                return -1;
        }

        /* We don't really care if it fails, we'll try again and that's it */
        RRD_update(dbpath, (unsigned int)rec->watts, (long)epoch);
        return 0;
}

static void
decode(const unsigned char *frame, struct record_data *rec)
{
        const int volt = 230;
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
        rec->watts = rec->amps * volt;
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

        const size_t cnt = libusb_get_device_list(ctx, &devs);
        if (cnt == 0) {
                fprintf(stderr, "Could not get device list: %s\n",
                        libusb_strerror(ret));
                return -1;
        }

        *dev_handle =
            libusb_open_device_with_vid_pid(ctx, OWL_VENDOR_ID, CM160_DEV_ID);
        libusb_free_device_list(devs, 1);
        if (!*dev_handle) {
                fprintf(stderr, "%s\n", libusb_strerror(ret));
                return -1;
        }
        return 0;
}

static int
process(char *dbpath, unsigned char *frame, libusb_device_handle *dev_handle)
{
        int i, ret = 0;
        unsigned char data = 0x00;
        unsigned int checksum = 0;
        static int last_valid_month = 0;

        static struct record_data *history[HISTORY_SIZE] = { 0 };
        static int dumping_history = 1;
        static int rec_id = -1;

        struct record_data *rec = calloc(1, sizeof(struct record_data));

        for (i = 0; i < 79; ++i)
                DPRINTF(2, "-");
        DPRINTF(2, "-\n");
        for (i = 0; i < 10; ++i)
                DPRINTF(2, "0x%02x - ", frame[i]);
        DPRINTF(2, "0x%02x\n", frame[10]);

        if (!strncmp((char *)frame, EMPTY_MSG, 11)) {
                DPRINTF(2, "received EMPTY MSG\n");
                goto out;
        } else if (!strncmp((char *)frame, ID_MSG, 11)) {
                DPRINTF(2, "received ID MSG\n");
                data = 0x5A;
        } else if (!strncmp((char *)frame, WAIT_MSG, 11)) {
                DPRINTF(2, "received WAIT MSG\n");
                data = 0xA5;
        }

        int transferred;
        if (data == 0xA5 || data == 0x5A) {
                ret =
                    libusb_bulk_transfer(dev_handle, BULK_EP_OUT, &data,
                                         sizeof(char), &transferred, 1000);
                if (ret < 0) {
                        fprintf(stderr, "ERROR: bulk_write returned %d (%s)\n",
                                ret, libusb_strerror(ret));
                        ret = -1;
                        goto out;
                }
                DPRINTF(2, "wrote %d bytes: 0x%02x\n", transferred, data);
                goto out;
        }

        if ((dumping_history && frame[0] == FRAME_ID_LIVE)
            || rec_id >= HISTORY_SIZE) {
                dumping_history = 0;
                for (i = 0; i <= rec_id; ++i) {
                        DPRINTF(1, "Dump history... done. (%d/%d stored)\n",
                                i + 1, rec_id + 1);
                        if (dump_data(dbpath, history[i])) {
                                fprintf(stderr,
                                        "ERROR: Could not dump current data.\n");
                                ret = -1;
                                goto out;
                        }
                        free(history[i]);
                }
                return 0;
        }

        if (dumping_history == 0 && frame[0] == FRAME_ID_DB) {
                fprintf(stderr, "Don't care about DB frame if not dumping.\n");
                fprintf(stderr, "ERROR: Invalid ID 0x%x\n", frame[0]);
                for (i = 0; i < 10; ++i)
                        DPRINTF(2, "0x%02x - ", frame[i]);
                DPRINTF(2, "0x%02x\n", frame[10]);
                goto out;
        }

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
                ret = -1;
                goto out;
        }

        decode(frame, rec);

        /* There seem to be an issues with the month, some time they wind up
         * getting crazy value. Don't know why... */
        if (rec->date.tm_mon < 0 || rec->date.tm_mon > 11)
                rec->date.tm_mon = last_valid_month;
        else
                last_valid_month = rec->date.tm_mon;

        char buf[18];
        strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M", &(rec->date));
        DPRINTF(2, "DATA: %s : %f W\n", buf, rec->watts);
        if (dumping_history) {
                DPRINTF(1, "Dump history... (%d stored)\n", rec_id + 2);
                history[++rec_id] = rec;
                return 0;
        }

        if (dump_data(dbpath, rec)) {
                fprintf(stderr, "ERROR: Could not dump current data.\n");
                ret = -1;
                goto out;
        }

out:
        free(rec);
        return ret;
}

static int
get_data(char* dbpath, libusb_device_handle *dev_handle)
{
        unsigned char buffer[512];
        int transferred;
        memset(buffer, 0, sizeof(buffer));

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
        DPRINTF(2, "read %d bytes: \n", transferred);

        unsigned char *bufptr = (unsigned char *)buffer;
        int nb_words = transferred / 11;        // incomplete words are resent

        DPRINTF(2, "nb words: %d\n", nb_words);
        while (nb_words--) {
                if (process(dbpath, bufptr, dev_handle)) {
                        fprintf(stderr, "ERROR: process detected an error.\n");
                        return -1;
                }
                bufptr += 11;
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
        libusb_control_transfer(dev_handle, REQTYPE, CP210X_IFC_ENABLE,
                                UART_ENABLE, 0, NULL, 0, 500);
        libusb_control_transfer(dev_handle, REQTYPE, CP210X_SET_BAUDRATE, 0, 0,
                        (void *)&baudrate, sizeof(baudrate), 500);
        libusb_control_transfer(dev_handle, REQTYPE, CP210X_IFC_ENABLE,
                                UART_DISABLE, 0, NULL, 0, 500);
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
        char dbpath[PATH_MAX] = "/var/lib/tesla.rrd";
        libusb_context *ctx = NULL;
        libusb_device_handle *dev_handle = NULL;

        signal(SIGINT, sigint_handler);

        int c, ret = 0;
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
        ret = scan_usb(ctx, &dev_handle);
        if (ret) {
                fprintf(stderr, "We found the device but could not open it.\n");
                return -1;
        }

        if (prepare_device(dev_handle)) {
                fprintf(stderr, "Could not prepare the device.\n");
                return -1;
        }

        DPRINTF(1, "Start acquiring data...\n");

        while (!stop)
        {
                ret = get_data(dbpath, dev_handle);
                if (ret) {
                        fprintf(stderr, "Error getting data.\n");
                        return -1;
                }
        }

        DPRINTF(2, "\nClosing connection with the device\n");
        ret = libusb_release_interface(dev_handle, INTERFACE);
        if (ret) {
                fprintf(stderr, "Cannot release interface.\n");
                return -1;
        }
        libusb_reset_device(dev_handle);
        libusb_close(dev_handle);
        libusb_exit(ctx);

        return 0;
}
