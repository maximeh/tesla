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
#ifdef USE_RRD
#include "rrd_helpers.h"
#elif defined USE_KEEN
#include "curl_helpers.h"
#endif

struct cm160_device owl_dev;
static struct record_data rec;

struct record_data* history[HISTORY_SIZE];
int dumping_history = 1;
int rec_id = 0;
pthread_t thread;

int do_verbose = 0;
#ifdef USE_RRD
char DBPATH[PATH_MAX] = "/var/lib/tesla.rrd";
#elif defined USE_KEEN
char *keenio_key = "";
char *keenio_project = "";
#endif

static char ID_MSG[11] = { 0xA9, 0x49, 0x44, 0x54, 0x43, 0x4D, 0x56, 0x30,
	0x30, 0x31, 0x01 };
static char WAIT_MSG[11] = { 0xA9, 0x49, 0x44, 0x54, 0x57, 0x41, 0x49, 0x54,
	0x50, 0x43, 0x52 };

void sigint_handler (int sig);
static int dump_data (struct record_data *rec);
static void decode (unsigned char *frame, struct record_data *rec);
static int prepare_device (void);
static int scan_usb (void);
static int process (unsigned char *frame);
static int get_data (void);
static inline void usage ();

int verbose (const char* format, ...)
{
	if (do_verbose == 0 ) return 0;
	va_list args;
	va_start(args, format);
	int ret = vfprintf(stdout, format, args);
	va_end(args);
	return ret;
}

int debug (const char* format, ...)
{
	if (do_verbose < 2) return 0;
	va_list args;
	va_start(args, format);
	int ret = vfprintf(stderr, format, args);
	va_end(args);
	return ret;
}

void
sigint_handler (int sig)
{
	debug ("\nClosing connection with the device\n");

	if (owl_dev.hdev)
	{
		usb_release_interface (owl_dev.hdev, 0);
		usb_close (owl_dev.hdev);
	}

	exit (0);
}

static int
dump_data (struct record_data *rec)
{
#ifdef USE_RRD
	debug ("Writing to %s\n", DBPATH);
#endif
	time_t epoch;
	struct tm *time_utc;
	epoch = mktime (&(rec->date));
	if (epoch == -1)
	{
		fprintf (stderr, "ERROR: mktime detected an error.\n");
		return -1;
	}
	//Convert to UTC
	time_utc = gmtime (&epoch);
	epoch = mktime (time_utc);
	if (epoch == -1)
	{
		fprintf (stderr, "ERROR: mktime detected an error.\n");
		return -1;
	}

#ifdef USE_RRD
	RRD_update (DBPATH, (unsigned int)rec->watts, (long)epoch);
#elif defined USE_KEEN
	if (strlen(keenio_key) && strlen(keenio_project))
		curl_update ((unsigned int)rec->watts, time_utc);
#endif

	return 0;
}

static void
decode (unsigned char *frame, struct record_data *rec)
{
	int volt = 230;
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
scan_usb (void)
{
	usb_init ();
	usb_find_busses ();
	usb_find_devices ();

	struct usb_bus *bus = NULL;
	struct usb_device *dev = NULL;
	struct usb_bus *busses = usb_get_busses ();

	for (bus = busses; bus; bus = bus->next)
	{
		for (dev = bus->devices; dev; dev = dev->next)
		{
			if (dev->descriptor.idVendor == OWL_VENDOR_ID &&
					dev->descriptor.idProduct == CM160_DEV_ID)
			{
				owl_dev.usb_dev = dev;
				return 0;
			}
		}
	}
	return 1;
}

static int
process (unsigned char *frame)
{
	int i, ret;
	unsigned char data[1] = { 0x00 };
	unsigned int checksum = 0;
	static int last_valid_month = 0;

	for(i=0; i<79; i++) debug("-");
	debug("-\n");
	for (i=0; i<10; i++) debug ("0x%02x - ", frame[i]);
	debug ("0x%02x\n", frame[10]);

	if (strncmp((char *)frame, ID_MSG, 11) == 0)
	{
		debug ("received ID MSG\n");
		data[0] = 0x5A;
	}
	else if (strncmp ((char *)frame, WAIT_MSG, 11) == 0)
	{
		debug ("received WAIT MSG\n");
		data[0] = 0xA5;
	}

	if (data[0] == 0xA5 || data[0] == 0x5A)
	{
		ret = usb_bulk_write (owl_dev.hdev, owl_dev.epout, (const char *)&data,
				sizeof (data), 1000);
		if (ret < 0)
		{
			fprintf (stderr, "ERROR: bulk_write returned %d (%s)\n", ret, usb_strerror ());
			return -1;
		}
		debug ("wrote %d bytes: %08x\n", ret, (const char *)&data);
		return 0;
	}

	// We don't care abouth LIVE frame, each LIVE frame is sent a little while
	// after (10/15s) as an HISTORY one.
	// This allow us to take the HISTORY on the device into account
	if ((dumping_history && frame[0] == FRAME_ID_LIVE) || rec_id >= HISTORY_SIZE)
  {
    dumping_history = 0;
    for (i=0; i < rec_id; i++)
    {
      verbose ("Dump history... done. (%d/%d stored)\n", i, rec_id);
      if (dump_data (history[i]))
      {
        fprintf(stderr, "ERROR: Could not dump current data.\n");
        return -1;
      }
      // If we send data too fast it can cause problem.
      sleep (2);
    }
    return 0;
  }

	if(frame[0] != FRAME_ID_DB)
	{
		fprintf (stderr, "ERROR: Invalid ID 0x%x\n", frame[0]);
		for (i=0; i<10; i++) debug ("0x%02x - ", frame[i]);
		debug ("0x%02x\n", frame[10]);
		return 0;
	}

	// Compute checksum (sumup the 10 first elements)
	for (i = 0; i < 10; i++)
		checksum += frame[i];

	// Leave only the least significant byte
	checksum &= 0xff;

	// Checksum should be egal the 10nth element of the frame
	if (checksum != frame[10])
	{
		fprintf (stderr, "ERROR: Invalid checksum: expected 0x%x, got 0x%x\n",
				frame[10], checksum);
		return -1;
	}

	memset (&rec, 0, sizeof (struct record_data));
	decode (frame, &rec);

	// There seem to be an issues with the month, some time they wind up getting
	// crazy value.
	// Don't know why...
	if (rec.date.tm_mon < 0 || rec.date.tm_mon > 11)
		rec.date.tm_mon = last_valid_month;
	else
		last_valid_month = rec.date.tm_mon;

	char buf[18];
	strftime (buf, sizeof (buf), "%Y/%m/%d %H:%M", &(rec.date));
	debug ("DATA: %s : %f W\n", buf, rec.watts);
  if (dumping_history)
  {
    verbose ("Dump history... (%d stored)\n", rec_id);
    history[rec_id++] = &rec;
    return 0;
  }

  if (dump_data (&rec))
	{
		fprintf(stderr, "ERROR: Could not dump current data.\n");
		return -1;
	}
  sleep (45);
	return 0;
}

static int
get_data (void)
{
	unsigned char buffer[512];
	memset (buffer, 0, sizeof (buffer));
	int ret;

	ret = usb_bulk_read (owl_dev.hdev, owl_dev.epin, (char*)buffer, sizeof(buffer), 10000);
	if( ret < 0 )
	{
		fprintf (stderr, "ERROR: bulk_read returned %d (%s)\n", ret, usb_strerror ());
		return -1;
	}
	debug ("read %d bytes: \n", ret);

	unsigned char *bufptr = (unsigned char *)buffer;
	int nb_words = ret/11; // incomplete words are resent

	debug ("nb words: %d\n", nb_words);
	while (nb_words--)
	{
		if (process (bufptr))
		{
			fprintf (stderr, "ERROR: process detected an error.\n");
			return -1;
		}
		bufptr += 11;
	}
	return 0;
}

static int
prepare_device (void)
{
	int reqtype =  USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_ENDPOINT_OUT;
	owl_dev.hdev = usb_open (owl_dev.usb_dev);
	if (!owl_dev.hdev)
	{
		fprintf (stderr, "ERROR: Failed to open device.\n");
		return -1;
	}

	usb_detach_kernel_driver_np (owl_dev.hdev, 0);

	if (usb_set_configuration (owl_dev.hdev,
				owl_dev.usb_dev->config[0].bConfigurationValue))
	{
		fprintf (stderr, "ERROR: usb_set_configuration: %s\n", usb_strerror());
		return -1;
	}

	if (usb_claim_interface (owl_dev.hdev, 0))
	{
		fprintf (stderr, "ERROR: usb_claim_interface: %s\n", usb_strerror());
		return -1;
	}

	struct usb_interface_descriptor *alt;
	alt = owl_dev.usb_dev->config->interface->altsetting;
	int nb_endpoint = alt->bNumEndpoints;
	int endpoint;
	int i;
	for (i = 0; i < nb_endpoint; i++)
	{
		endpoint = alt->endpoint[i].bEndpointAddress;
		if (endpoint & (1 << 7))
			owl_dev.epin = endpoint;
		else
			owl_dev.epout = endpoint;
	}

	// Set the baudrate at the correct speed to talk to the device
	int baudrate = 250000;
	usb_control_msg (owl_dev.hdev, reqtype, CP210X_IFC_ENABLE, UART_ENABLE, 0,
			NULL, 0, 500);
	usb_control_msg (owl_dev.hdev, reqtype, CP210X_SET_BAUDRATE, 0, 0, (char
				*)&baudrate, sizeof (baudrate), 500);
	usb_control_msg (owl_dev.hdev, reqtype, CP210X_IFC_ENABLE, UART_DISABLE, 0,
			NULL, 0, 500);
	return 0;
}

static void
usage (void)
{
#ifdef USE_RRD
	(void) fprintf (stdout, "tesla [OPTIONS] [RRD DATABASE PATH]\n");
#elif defined USE_KEEN
	(void) fprintf (stdout, "tesla [OPTIONS] API_KEY PROJECT_ID\n");
#endif
	fputs(
			"\t-d\t\tDebug output\n"
			"\t-h\t\tThis usage statement\n"
			"\t-v\t\tVerbose output\n"
			, stdout);
}

int
main (int argc, char **argv)
{

	signal (SIGINT, sigint_handler);

	int c;
	while ((c = getopt (argc, argv, "hvd")) != -1)
	{
		switch (c)
		{
			case 'v':
				/* verbose mode */
				do_verbose = 1;
				break;
			case 'd':
				/* debug mode */
				do_verbose = 2;
				break;
			case 'h':
			default:
				usage ();
				return 0;
		}
	}
	argc -= optind;
	argv += optind;

	// Check that we are the root user
	if (geteuid ())
	{
		fprintf (stderr, "ERROR: You need to be root.\n");
		return -1;
	}

#ifdef USE_RRD
	if (argc == 1)
		realpath (argv[0], DBPATH);

	if (access (DBPATH, F_OK) == -1)
	{
		verbose ("Creating db: %s\n", DBPATH);
		if (RRD_create (DBPATH, 60))
			return -1;
	}
  debug ("Using DB: %s\n", DBPATH);
#elif defined USE_KEEN
	if (argc == 2)
  {
    keenio_key = argv[0];
    keenio_project = argv[1];
  }
	debug ("API Key: %s\n", keenio_key);
	debug ("Project ID: %s\n", keenio_project);
#endif

	verbose ("Please plug your CM160 device...\n");
	while (scan_usb ())
		sleep (5);

	if (prepare_device ())
		return -1;

	verbose ("Start acquiring data...\n");
	while (! get_data ())
    ;

	if (usb_release_interface (owl_dev.hdev, 0))
	{
		fprintf (stderr, "ERROR: usb_release_interface: %s\n", usb_strerror ());
		return -1;
	}
	if (usb_close (owl_dev.hdev))
	{
		fprintf (stderr, "ERROR: usb_release_interface: %s\n", usb_strerror ());
		return -1;
	}

	return 0;
}

