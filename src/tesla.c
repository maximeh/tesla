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

struct cm160_device owl_dev;
static struct record_data rec;

int do_verbose = 0;
char DBPATH[PATH_MAX] = "/var/lib/tesla.rrd";

static char ID_MSG[11] = {
        0xA9, 0x49, 0x44, 0x54, 0x43, 0x4D, 0x56, 0x30, 0x30, 0x31, 0x01 };
static char WAIT_MSG[11] = {
        0xA9, 0x49, 0x44, 0x54, 0x57, 0x41, 0x49, 0x54, 0x50, 0x43, 0x52 };

void sigint_handler (int sig);
static void dump_data (struct record_data *rec);
static void decode (unsigned char *frame, struct record_data *rec);
static int prepare_device (void);
static int scan_usb (void);
static int process (unsigned char *frame);
static void get_data (void);
static inline void usage ();

inline int verbose (const char* format, ...)
{
  if (do_verbose == 0 ) return 0;
  va_list args;
  va_start(args, format);
  int ret = vfprintf(stdout, format, args);
  va_end(args);
  return ret;
}

inline int debug (const char* format, ...)
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
  if (!owl_dev.hdev)
    return;

  usb_release_interface (owl_dev.hdev, 0);
  usb_close (owl_dev.hdev);
  exit (0);
}

static void
dump_data (struct record_data *rec)
{
  debug ("Writing to %s\n", DBPATH);
  time_t epoch;
  epoch = mktime (&(rec->date));
  //Convert to UTC
  epoch = mktime (gmtime (&epoch));
  RRD_update (DBPATH, (unsigned int)rec->watts, (long)epoch);
}

static void
decode (unsigned char *frame, struct record_data *rec)
{
  int volt = 230;
  rec->date.tm_year = frame[1] + 100; /* Device send only 13 for 2013, struct tm, take its year from 1900 */
  rec->date.tm_mon = frame[2] - 1; /* struct tm is 0-11, the device send 1-12 */
  rec->date.tm_mday = frame[3];
  rec->date.tm_hour = frame[4];
  rec->date.tm_min = frame[5];
  /* rec->cost = (frame[6] + (frame[7] << 8)) / 100.0; */
  rec->amps = (frame[8] + (frame[9] << 8)) * 0.07; // mean intensity during one minute
  rec->watts = rec->amps * volt; // mean power during one minute
  /* rec->ah = rec->amps / 60; // -> we must devide by 60 to convert into ah and wh */
  /* rec->wh = rec->watts / 60; */
}

static int
scan_usb (void)
{
  usb_init ();
  usb_find_busses ();
  usb_find_devices ();

  struct usb_bus *bus = NULL;
  struct usb_device *dev = NULL;

  for (bus = usb_get_busses (); bus; bus = bus->next)
  {
    for (dev = bus->devices; dev; dev = dev->next)
    {
      if (dev->descriptor.idVendor == OWL_VENDOR_ID && dev->descriptor.idProduct == CM160_DEV_ID)
      {
        owl_dev.usb_dev = dev;
        return 1;
      }
    }
  }
  return 0;
}

static int
process (unsigned char *frame)
{
  int i;
  unsigned char data[1];
  unsigned int checksum = 0;
  static int last_valid_month = 0;

  if(strncmp((char *)frame, ID_MSG, 11) == 0)
  {
    debug ("received ID MSG\n");
    data[0]=0x5A;
    usb_bulk_write (owl_dev.hdev, owl_dev.epout, (const char *)&data, sizeof (data), 1000);
    return 0;
  }
  else if(strncmp ((char *)frame, WAIT_MSG, 11) == 0)
  {
    debug ("received WAIT MSG\n");
    data[0]=0xA5;
    usb_bulk_write (owl_dev.hdev, owl_dev.epout, (const char *)&data, sizeof (data), 1000);
    return 0;
  }

  // We don't care abouth LIVE frame, each LIVE frame is sent a little while
  // after (10/15s) as an HISTORY one.
  // This allow us to take the HISTORY on the device into account
  if (frame[0] == FRAME_ID_LIVE)
    return -1;

  if(frame[0] != FRAME_ID_DB)
  {
    debug ("data error: invalid ID 0x%x\n", frame[0]);
    for (i=0; i<11; i++)
      debug ("0x%02x - ", frame[i]);
    debug ("\n");
    return -1;
  }

  // Compute checksum (sumup the 10 first elements)
  for (i=0; i<10; i++)
    checksum += frame[i];
  // Leave only the least significant byte
  checksum &= 0xff;
  // Checksum should be egal the 10nth element of the frame
  if (checksum != frame[10])
  {
    debug ("data error: invalid checksum: expected 0x%x, got 0x%x\n",
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
  dump_data (&rec);
  return 0;
}

static void
get_data (void)
{
  unsigned char buffer[512];
  memset (buffer, 0, sizeof (buffer));
  int ret;

  ret = usb_bulk_read (owl_dev.hdev, owl_dev.epin, (char*)buffer, sizeof(buffer), 10000);
  if( ret < 0 )
  {
    debug ("bulk_read returned %d (%s)\n", ret, usb_strerror ());
    return;
  }
  debug ("read %d bytes: \n", ret);

  unsigned char *bufptr = (unsigned char *)buffer;
  int nb_words = ret/11; // incomplete words are resent

  debug ("nb words: %d\n", nb_words);
  while (nb_words--)
  {
    process (bufptr);
    bufptr += 11;
  }
}

static int
prepare_device (void)
{
  int reqtype =  USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_ENDPOINT_OUT;
  owl_dev.hdev = usb_open (owl_dev.usb_dev);
  if (!owl_dev.hdev)
  {
    fprintf (stderr, "failed to open device\n");
    return 1;
  }

  usb_detach_kernel_driver_np (owl_dev.hdev, 0);

  if (0 != usb_set_configuration (owl_dev.hdev, owl_dev.usb_dev->config[0].bConfigurationValue))
  {
    fprintf (stderr, "usb_set_configuration returns %s\n", usb_strerror());
    return 1;
  }

  if (0 > usb_claim_interface (owl_dev.hdev, 0))
  {
    fprintf (stderr, "Can't claim the interface.\n");
    return 1;
  }

  int nb_endpoint = owl_dev.usb_dev->config->interface->altsetting->bNumEndpoints;
  int endpoint;
  int i;
  for (i=0; i<nb_endpoint; i++)
  {
    endpoint = owl_dev.usb_dev->config->interface->altsetting->endpoint[i].bEndpointAddress;
    if (endpoint & (1 << 7))
      owl_dev.epin = endpoint;
    else
      owl_dev.epout = endpoint;
  }

  // Set the baudrate at the correct speed to talk to the device
  int baudrate = 250000;
  usb_control_msg (owl_dev.hdev, reqtype, CP210X_IFC_ENABLE, UART_ENABLE, 0, NULL, 0, 500);
  usb_control_msg (owl_dev.hdev, reqtype, CP210X_SET_BAUDRATE, 0, 0, (char *)&baudrate, sizeof (baudrate), 500);
  usb_control_msg (owl_dev.hdev, reqtype, CP210X_IFC_ENABLE, UART_DISABLE, 0, NULL, 0, 500);
  return 0;
}

int
main (int argc, char **argv)
{

  signal (SIGINT, sigint_handler);

  int c;
  while ((c = getopt (argc, argv, "hf:vdm")) != -1)
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
  if (geteuid () != 0)
  {
    fprintf (stderr, "You are not root...\n");
    return 1;
  }

  if (argc == 1)
		realpath (argv[0], DBPATH);

  if (access (DBPATH, F_OK) == -1)
  {
    verbose ("Creating db: %s\n", DBPATH);
    RRD_create (DBPATH, 60);
  }

  verbose ("Please plug your CM160 device...\n");
  while (scan_usb () == 0)
    sleep (5);

  if ( prepare_device () == 1)
    return 1;

  verbose ("Start acquiring data...\n");
  while (1)
  {
    sleep (60);
    get_data ();
  }

  usb_release_interface (owl_dev.hdev, 0);
  usb_close (owl_dev.hdev);

  fclose ( stdin );
  fclose ( stdout );
  fclose ( stderr );
  return 0;
}

static void
usage (void)
{
  (void) fprintf (stderr, "tesla [-h] [-d] [-v] <RRD DATABASE PATH>\n");
  fputs(
      "\t-h\t\tThis usage statement\n"
      "\t-v\t\tVerbose output\n"
      "\t-d\t\tDebug output\n"
      , stderr);
}
