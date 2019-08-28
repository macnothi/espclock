#ifndef _CRTC_H
#define _CRTC_H

#include "ISyncProvider.h"

#if defined(ESP8266)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif
#include <Wire.h>  // must be incuded here so that Arduino library object file references work
#include <RtcDS3231.h>    // makuna RTC library


class CRTC : public ISyncProvider
{
public:

  CRTC();

  virtual ~CRTC();

  bool setup();

  void setTime(time_t t);

  virtual time_t now();

  void setSyncProvider(ISyncProvider* pSyncProvider);
  void setSyncInterval(time_t interval);
  

private:
  ISyncProvider* m_pSyncProvider;
  uint32_t syncInterval;
  uint32_t nextSyncTime;

  time_t sync(time_t t, bool force=false);

  RtcDS3231<TwoWire> Rtc;
  
};

#endif
