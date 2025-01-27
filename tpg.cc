/*
    National Geographic Topo! TPG file support (Waypoints/Routes)
    Contributed to gpsbabel by Alex Mottram

    For Topo! version 2.x.  Routes are currently not implemented.

    Copyright (C) 2002 Alex Mottram, geo_alexm at cox-internet.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */

#include "tpg.h"

#include <cctype>           // for isalnum
#include <cstring>          // for memcmp

#include <QChar>            // for QChar
#include <QString>          // for QString

#include "defs.h"
#include "gbfile.h"         // for gbfwrite, gbfgetint16, gbfputint16, gbfclose, gbfgetdbl, gbfgetpstr, gbfopen_le, gbfputdbl, gbfgetint32, gbfputc, gbfputpstr, gbfread
#include "jeeps/gpsmath.h"  // for GPS_Lookup_Datum_Index, GPS_Math_Known_Datum_To_WGS84_M, GPS_Math_WGS84_To_Known_Datum_M
#include "mkshort.h"        // for MakeShort


#define MYNAME	"TPG"

#define MAXTPGSTRINGSIZE	256
#define MAXTPGOUTPUTPINS	65535

int
TpgFormat::valid_tpg_header(char* header, int len)
{
  unsigned char header_bytes[] = { 0xFF, 0xFF, 0x01, 0x00, 0x0D,
                                   0x00, 0x43, 0x54, 0x6F, 0x70,
                                   0x6F, 0x57, 0x61, 0x79, 0x70,
                                   0x6F, 0x69, 0x6E, 0x74
                                 };
  if (len != 19) {
    return (-1);
  }

  return memcmp(header_bytes, header, len);
}

void
TpgFormat::tpg_common_init()
{
  tpg_datum_idx = GPS_Lookup_Datum_Index(tpg_datum_opt);
  if (tpg_datum_idx < 0) {
    fatal(MYNAME ": Datum '%s' is not recognized.\n", qPrintable(tpg_datum_opt.get()));
  }
}

void
TpgFormat::rd_init(const QString& fname)
{
  tpg_common_init();
  tpg_file_in = gbfopen_le(fname, "rb", MYNAME);
}

void
TpgFormat::rd_deinit()
{
  gbfclose(tpg_file_in);
}

void
TpgFormat::wr_init(const QString& fname)
{
  tpg_common_init();
  tpg_file_out = gbfopen_le(fname, "wb", MYNAME);
  mkshort_handle = new MakeShort;
  waypt_out_count = 0;
}

void
TpgFormat::wr_deinit()
{
  delete mkshort_handle;
  gbfclose(tpg_file_out);
}

void
TpgFormat::read()
{
  char buff[MAXTPGSTRINGSIZE + 1];
  double amt;

  short int pointcount = gbfgetint16(tpg_file_in);

  /* the rest of the header */
  gbfread(&buff[0], 19, 1, tpg_file_in);

  if (valid_tpg_header(buff, 19) != 0) {
    fatal(MYNAME ": input file does not appear to be a valid .TPG file.\n");
  }


  while (pointcount--) {
    auto* wpt_tmp = new Waypoint;

    /* pascal-like shortname */
    wpt_tmp->shortname = gbfgetpstr(tpg_file_in);

    /* for some very odd reason, signs on longitude are swapped */
    /* coordinates are in NAD27/CONUS datum                     */

    /* 8 bytes - longitude, sign swapped  */
    double lon = gbfgetdbl(tpg_file_in);

    /* 8 bytes - latitude */
    double lat = gbfgetdbl(tpg_file_in);

    /* swap sign before we do datum conversions */
    lon *= -1.0;

    /* 2 bytes - elevation in feet */
    double elev = FEET_TO_METERS(gbfgetint16(tpg_file_in));

    /* convert incoming NAD27/CONUS coordinates to WGS84 */
    GPS_Math_Known_Datum_To_WGS84_M(
      lat,
      lon,
      0.0,
      &wpt_tmp->latitude,
      &wpt_tmp->longitude,
      &amt,
      tpg_datum_idx);

    wpt_tmp->altitude = elev;


    /* 4 bytes? */
    (void) gbfgetint32(tpg_file_in);

    /* pascal-like description */
    wpt_tmp->description = gbfgetpstr(tpg_file_in);

    /* 2 bytes */
    (void) gbfgetint16(tpg_file_in);

    waypt_add(wpt_tmp);
  }
}

void
TpgFormat::tpg_waypt_pr(const Waypoint* wpt)
{
  double lon;
  double lat;
  double amt;
  char ocount;
  QString shortname;
  QString description;
  int i;

  /* our personal waypoint counter */
  waypt_out_count++;

  /* this output format pretty much requires a description
   * and a shortname
   */

  if ((wpt->shortname.isEmpty()) || (global_opts.synthesize_shortnames)) {
    if (!wpt->description.isEmpty()) {
      if (global_opts.synthesize_shortnames) {
        shortname = mkshort_handle->mkshort_from_wpt(wpt);
      } else {
        shortname = wpt->description;
      }
    } else {
      /* no description available */
      shortname = "";
    }
  } else {
    shortname = wpt->shortname;
  }
  if (wpt->description.isEmpty()) {
    if (!shortname.isEmpty()) {
      description = shortname;
    } else {
      description = "";
    }
  } else {
    description = wpt->description;
  }

  /* convert lat/long to NAD27/CONUS datum */
  GPS_Math_WGS84_To_Known_Datum_M(
    wpt->latitude,
    wpt->longitude,
    0.0,
    &lat,
    &lon,
    &amt,
    tpg_datum_idx);


  /* swap the sign back *after* the datum conversion */
  lon *= -1.0;

  /* convert meters back to feets */
  auto elev = (short int) METERS_TO_FEET(wpt->altitude);

  /* 1 bytes stringsize for shortname */
  char c = shortname.length();
  ocount = 0;
  /*
   * It's reported the only legal characters are upper case
   * A-Z and 0-9.  Wow.   We have to make two passes: one to
   * count and one to output.
   */
  for (i = 0; i < c; i++) {
    char oc = shortname[i].toUpper().cell();
    if (isalnum(oc) || oc == ' ') {
      ocount++;
    }
  }

  gbfwrite(&ocount, 1, 1, tpg_file_out);

  for (i = 0; i < c; i++) {
    char oc = shortname[i].toUpper().cell();
    if (isalnum(oc) || oc == ' ') {
      gbfputc(oc, tpg_file_out);
    }
  }

  /* 8 bytes - longitude */
  gbfputdbl(lon, tpg_file_out);

  /* 8 bytes - latitude */
  gbfputdbl(lat, tpg_file_out);

  /* 2 bytes - elevation_feet */
  gbfputint16(elev, tpg_file_out);

  /* 4 unknown bytes */
  /* these unknown 4 are probably point properties (color, icon, etc..) */
  unsigned char unknown4[] = { 0x78, 0x56, 0x34, 0x12 };
  gbfwrite(unknown4, 1, 4, tpg_file_out);

  /* pascal-like description */
  gbfputpstr(description, tpg_file_out);

  /* and finally 2 unknown bytes */

  if (waypt_out_count == waypt_count()) {
    /* last point gets 0x0000 instead of 0x0180 */
    gbfputint16(0, tpg_file_out);
  } else {
    /* these 2 appear to be constant across test files */
    unsigned char unknown2[] = { 0x01, 0x80 };
    gbfwrite(unknown2, 1, 2, tpg_file_out);
  }
}

void
TpgFormat::write()
{
  unsigned char header_bytes[] = { 0xFF, 0xFF, 0x01, 0x00, 0x0D,
                                   0x00, 0x43, 0x54, 0x6F, 0x70,
                                   0x6F, 0x57, 0x61, 0x79, 0x70,
                                   0x6F, 0x69, 0x6E, 0x74
                                 };

  int s = waypt_count();

  if (global_opts.synthesize_shortnames) {
    mkshort_handle->set_length(32);
    mkshort_handle->set_whitespace_ok(true);
    mkshort_handle->set_mustupper(true);
  }

  if (s > MAXTPGOUTPUTPINS) {
    fatal(MYNAME ": attempt to output too many points (%d).  The max is %d.  Sorry.\n", s, MAXTPGOUTPUTPINS);
  }

  /* write the waypoint count */
  gbfputint16(s, tpg_file_out);

  /* write the rest of the header */
  gbfwrite(header_bytes, 1, 19, tpg_file_out);

  auto tpg_waypt_pr_lambda = [this](const Waypoint* waypointp)->void {
    tpg_waypt_pr(waypointp);
  };
  waypt_disp_all(tpg_waypt_pr_lambda);
}
