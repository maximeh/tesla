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

#include "tesla.h"
#include "rrd_helpers.h"

/* struct cm160_device owl_dev; */

struct record_data *history[HISTORY_SIZE] = { 0 };

int dumping_history = 1;
int rec_id = -1;

int _debug = 0;
char DBPATH[PATH_MAX] = "/var/lib/tesla.rrd";

static const char ID_MSG[11] = { 0xA9, 0x49, 0x44, 0x54, 0x43, 0x4D, 0x56, 0x30,
        0x30, 0x31, 0x01
};

static const char WAIT_MSG[11] =
    { 0xA9, 0x49, 0x44, 0x54, 0x57, 0x41, 0x49, 0x54,
        0x50, 0x43, 0x52
};

void sigint_handler(const int sig);
static int dump_data(struct record_data *rec);
static void decode(const unsigned char *frame, struct record_data *rec);
static int prepare_device(void);
static int scan_usb(void);
static int process(unsigned char *frame);
static int get_data(void);
static inline void usage();

void
sigint_handler(const int sig)
{
        DPRINTF(2, "\nClosing connection with the device\n");

        libusb_release_interface(dev_handle, INTERFACE);
        libusb_reset_device(dev_handle);
        libusb_close(dev_handle);
        libusb_exit(ctx);

        exit(0);
}

static int
dump_data(struct record_data *rec)
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

        RRD_update(DBPATH, (unsigned int)rec->watts, (long)epoch);
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
scan_usb(void)
{
        const int ret = libusb_init(&ctx);
        if (ret < 0) {
                fprintf(stderr, "%s\n", libusb_strerror(ret));
                return -1;
        }
        libusb_set_debug(ctx, _debug > 3 ? 3 : _debug);

        const size_t cnt = libusb_get_device_list(ctx, &devs);
        if (cnt < 0) {
                fprintf(stderr, "Could get device list: %s\n",
                        libusb_strerror(ret));
                return -1;
        }

        dev_handle =
            libusb_open_device_with_vid_pid(ctx, OWL_VENDOR_ID, CM160_DEV_ID);
        libusb_free_device_list(devs, 1);
        if (!dev_handle)
                return -1;
        return 0;
}

static int
process(unsigned char *frame)
{
        int i, ret;
        unsigned char data[1] = { 0x00 };
        unsigned int checksum = 0;
        static int last_valid_month = 0;

        struct record_data *rec = calloc(1, sizeof(struct record_data));

        for (i = 0; i < 79; ++i)
                DPRINTF(2, "-");
        DPRINTF(2, "-\n");
        for (i = 0; i < 10; ++i)
                DPRINTF(2, "0x%02x - ", frame[i]);
        DPRINTF(2, "0x%02x\n", frame[10]);

        if (!strncmp((char *)frame, ID_MSG, 11)) {
                DPRINTF(2, "received ID MSG\n");
                data[0] = 0x5A;
        } else if (!strncmp((char *)frame, WAIT_MSG, 11)) {
                DPRINTF(2, "received WAIT MSG\n");
                data[0] = 0xA5;
        }

        int transferred;
        if (data[0] == 0xA5 || data[0] == 0x5A) {
                ret =
                    libusb_bulk_transfer(dev_handle, BULK_EP_OUT, (char *)&data,
                                         sizeof(data), &transferred, 1000);
                if (ret < 0) {
                        fprintf(stderr, "ERROR: bulk_write returned %d (%s)\n",
                                ret, libusb_strerror(ret));
                        return -1;
                }
                DPRINTF(2, "wrote %d bytes: %08x\n", transferred,
                        (const char *)&data);
                return 0;
        }
        // We don't care abouth LIVE frame, each LIVE frame is sent a little
        // while and after (10/15s) as an HISTORY one.
        // This allow us to take the HISTORY on the device into account
        if ((dumping_history && frame[0] == FRAME_ID_LIVE)
            || rec_id >= HISTORY_SIZE) {
                dumping_history = 0;
                for (i = 0; i <= rec_id; ++i) {
                        DPRINTF(1, "Dump history... done. (%d/%d stored)\n",
                                i + 1, rec_id + 1);
                        if (dump_data(history[i])) {
                                fprintf(stderr,
                                        "ERROR: Could not dump current data.\n");
                                return -1;
                        }
                        free(history[i]);
                }
                return 0;
        }

        if (frame[0] != FRAME_ID_DB) {
                fprintf(stderr, "ERROR: Invalid ID 0x%x\n", frame[0]);
                for (i = 0; i < 10; ++i)
                        DPRINTF(2, "0x%02x - ", frame[i]);
                DPRINTF(2, "0x%02x\n", frame[10]);
                return 0;
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
                return -1;
        }

        decode(frame, rec);

        // There seem to be an issues with the month, some time they wind up getting
        // crazy value.
        // Don't know why...
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

        if (dump_data(rec)) {
                fprintf(stderr, "ERROR: Could not dump current data.\n");
                return -1;
        }
        free(rec);
        return 0;
}

static int
get_data(void)
{
        unsigned char buffer[512];
        int transferred;
        memset(buffer, 0, sizeof(buffer));

        // The call here is blocking and we did set an infinite timeout
        // The device is sending wait message regularly, so we can just loop on
        // this without consumming CPU nor needing to poll().
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
                if (process(bufptr)) {
                        fprintf(stderr, "ERROR: process detected an error.\n");
                        return -1;
                }
                bufptr += 11;
        }
        return 0;
}

static int
prepare_device(void)
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

        // Set the baudrate at the correct speed to talk to the device
        int baudrate = 250000;
        libusb_control_transfer(dev_handle, REQTYPE, CP210X_IFC_ENABLE,
                                UART_ENABLE, 0, NULL, 0, 500);
        libusb_control_transfer(dev_handle, REQTYPE, CP210X_SET_BAUDRATE, 0, 0,
                                (char *)&baudrate, sizeof(baudrate), 500);
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

        signal(SIGINT, sigint_handler);

        int c;
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

        // Check that we are the root user
        if (geteuid()) {
                fprintf(stderr, "ERROR: You need to be root.\n");
                return -1;
        }
        if (argc == 1)
                realpath(argv[0], DBPATH);

        if (access(DBPATH, F_OK) == -1) {
                DPRINTF(1, "Creating db: %s\n", DBPATH);
                if (RRD_create(DBPATH, 60))
                        return -1;
        }
        DPRINTF(2, "Using DB: %s\n", DBPATH);

        DPRINTF(1, "Please plug your CM160 device...\n");
        while (scan_usb())
                sleep(5);

        if (prepare_device())
                return -1;

        DPRINTF(1, "Start acquiring data...\n");

        while (!get_data()) ;

        const int ret = libusb_release_interface(dev_handle, INTERFACE);
        if (ret) {
                fprintf(stderr, "Cannot release interface.\n");
                return -1;
        }
        libusb_close(dev_handle);
        libusb_exit(ctx);

        return 0;
}
