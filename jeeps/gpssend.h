#ifndef gpssend_h
#define gpssend_h


#include "jeeps/gps.h"

#define GPS_ARB_LEN 1024

  int32  GPS_Serial_Write_Packet(gpsdevh* fd, const GPS_Packet& packet);
  bool  GPS_Serial_Send_Ack(gpsdevh* fd, GPS_Packet* tra, GPS_Packet* rec);

  void   GPS_Make_Packet(GPS_Packet* packet, US type, UC* data, uint32 n);


#endif
