/*  GNU ddrescue - Data recovery tool
    Copyright (C) 2014-2016 Antonio Diaz Diaz.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _FILE_OFFSET_BITS 64

#include "non_posix.h"

#ifdef USE_NON_POSIX
#include <cctype>
#include <string>
#include <sys/ioctl.h>

namespace {

void sanitize_string( std::string & str )
  {
  for( unsigned i = str.size(); i > 0; --i )	// remove non-printable chars
    {
    const unsigned char ch = str[i-1];
    if( std::isspace( ch ) ) str[i-1] = ' ';
    else if( ch < 32 || ch > 126 ) str.erase( i - 1, 1 );
    }
  for( unsigned i = str.size(); i > 0; --i )	// remove duplicate spaces
    if( str[i-1] == ' ' && ( i <= 1 || i >= str.size() || str[i-2] == ' ' ) )
      str.erase( i - 1, 1 );
  }

} // end namespace

#ifdef __HAIKU__
#include <Drivers.h>

const char * device_id( const int fd )
  {
  static std::string id_str;
  char buf[256];

  if( ioctl( fd, B_GET_DEVICE_NAME, buf, sizeof buf ) != 0 ) return 0;
  buf[(sizeof buf)-1] = 0;	// make sure it is null-terminated
  id_str = (const char *)buf;
  sanitize_string( id_str );
  return id_str.c_str();
  }

#else				// use linux by default
#include <linux/hdreg.h>

const char * device_id( const int fd )
  {
  static std::string id_str;
  struct hd_driveid id;

  if( ioctl( fd, HDIO_GET_IDENTITY, &id ) != 0 ) return 0;
  id_str = (const char *)id.model;
  std::string id_serial( (const char *)id.serial_no );
  sanitize_string( id_str );
  sanitize_string( id_serial );
  if( id_str.size() || id_serial.size() )
    { id_str += "::"; id_str += id_serial; return id_str.c_str(); }
  return 0;
  }

#endif

#else	// USE_NON_POSIX

const char * device_id( const int ) { return 0; }

#endif
