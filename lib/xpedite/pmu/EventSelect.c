///////////////////////////////////////////////////////////////////////////////////////////////
//
// Logic to validate and build Event select objects from PMU requests
//
// The list of programmable pmu events can be represented using two types
// 1. PMUCtlRequest - Programmer friendly model for pmu events
// 2. EventSelect - Machine friendly model for pmu events
//
// This file provide logic to transform request objects to event select objects
//
// Author: Manikandan Dhamodharan, Morgan Stanley
//
///////////////////////////////////////////////////////////////////////////////////////////////

#include <xpedite/pmu/EventSelect.h>
#include <xpedite/pmu/Formatter.h>

/*************************************************************************
* Logic to build bitmask for programming pmu events
**************************************************************************/

static uint32_t buildPerfEvtSelBitmask(const PMUGpEvent* e_) {
  typedef struct
  {
    union {
      uint32_t    _value;
      struct {
        unsigned char _eventSelect  : 8;
        unsigned char _unitMask     : 8;
        unsigned char _user         : 1;
        unsigned char _kernel       : 1;
        unsigned char _edgeDetect   : 1;
        unsigned char _pinControl   : 1;
        unsigned char _interruptEn  : 1;
        unsigned char _anyThread    : 1;
        unsigned char _enable       : 1;
        unsigned char _invertCMask  : 1;
        unsigned char _counterMask  : 8;
      } _f;
    };
  } PerfEvtSelReg;
 
  PerfEvtSelReg r;
  r._f._eventSelect  = e_->_eventSelect;
  r._f._unitMask     = e_->_unitMask;
  r._f._user         = e_->_user != 0;
  r._f._kernel       = e_->_kernel != 0;
  r._f._edgeDetect   = e_->_edgeDetect != 0;
  r._f._pinControl   = 0;
  r._f._interruptEn  = 0;
  r._f._anyThread    = e_->_anyThread != 0;
  r._f._enable       = 1;
  r._f._invertCMask  = e_->_invertCMask != 0;
  r._f._counterMask  = e_->_counterMask;
  return r._value;
}

static const PMUFixedEvent* findFixedEvtForCtr(unsigned char ctrIndex_, const PMUFixedEvent* fixedEvents_,
    unsigned char fixedEvtCount_) {
  unsigned i;
  for(i=0; i < fixedEvtCount_; ++i) {
    if(fixedEvents_[i]._ctrIndex == ctrIndex_) {
      return &fixedEvents_[i];
    }
  }
  return NULL;
}

static unsigned char feEnablemask(const PMUFixedEvent* fixedEvents_) {
  return (
    fixedEvents_->_user && fixedEvents_->_kernel ? 3 : (
      fixedEvents_->_user ? 2 : 1
    )
  );
}

static uint32_t buildFixedEvtSelBitmask(const PMUFixedEvent* fixedEvents_, unsigned char fixedEvtCount_) {
  typedef struct
  {
    union {
      uint32_t    _value;
      struct {
        unsigned char _enable0       : 2;
        unsigned char _anyThread0    : 1;
        unsigned char _interruptEn0  : 1;
        unsigned char _enable1       : 2;
        unsigned char _anyThread1    : 1;
        unsigned char _interruptEn1  : 1;
        unsigned char _enable2       : 2;
        unsigned char _anyThread2    : 1;
        unsigned char _interruptEn2  : 1;
        uint32_t      _reservedBits  : 20;
      } _f;
    };
  } FixedEvtSelReg;

  const PMUFixedEvent* evt0 = findFixedEvtForCtr(0, fixedEvents_, fixedEvtCount_);
  const PMUFixedEvent* evt1 = findFixedEvtForCtr(1, fixedEvents_, fixedEvtCount_);
  const PMUFixedEvent* evt2 = findFixedEvtForCtr(2, fixedEvents_, fixedEvtCount_);

  FixedEvtSelReg r;
  r._f._enable0      = evt0 ? feEnablemask(evt0) : 0;
  r._f._anyThread0   = 0;
  r._f._interruptEn0 = 0;
  r._f._enable1      = evt1 ? feEnablemask(evt1) : 0;
  r._f._anyThread1   = 0;
  r._f._interruptEn1 = 0;
  r._f._enable2      = evt2 ? feEnablemask(evt2) : 0;
  r._f._anyThread2   = 0;
  r._f._interruptEn2 = 0;
  r._f._reservedBits = 0;
  return r._value;
}

static uint32_t buildFixedEvtGlobalCtlBitmask(const PMUFixedEvent* fixedEvents_, unsigned char fixedEvtCount_) {
  int i;
  uint32_t value = 0;
  for(i=0; i < fixedEvtCount_; ++i) {
    if(fixedEvents_[i]._ctrIndex < XPEDITE_PMC_CTRL_FIXED_EVENT_MAX) {
      value |= 0x1 << fixedEvents_[i]._ctrIndex;
    }
    else {
      XPEDITE_LOG("invalid request - Fixed event counter index(%u) excceds %d\n", 
        (unsigned)fixedEvents_[i]._ctrIndex, XPEDITE_PMC_CTRL_FIXED_EVENT_MAX);
      return 0;
    }
  }
  return value;
}

/*************************************************************************
* Logic to process pmu requests form userspace
**************************************************************************/

int buildEventSet(const PMUCtlRequest* request_, EventSelect* eventSelect_) {

  unsigned i;
  memset(eventSelect_, 0, sizeof(*eventSelect_));

  if(request_->_fixedEvtCount > XPEDITE_PMC_CTRL_FIXED_EVENT_MAX) {
    XPEDITE_LOG("invalid request - max available fixed event counters %d, recieved (%u)\n",
      XPEDITE_PMC_CTRL_FIXED_EVENT_MAX, (unsigned)request_->_fixedEvtCount);
    return -1;
  }

  if(request_->_gpEvtCount > XPEDITE_PMC_CTRL_GP_EVENT_MAX) {
    XPEDITE_LOG("invalid request - general purpose event cannot exeed %d, recieved (%u)\n", 
      XPEDITE_PMC_CTRL_GP_EVENT_MAX, (unsigned)request_->_gpEvtCount);
    return -1;
  }

  if(request_->_offcoreEvtCount > XPEDITE_PMC_CTRL_OFFCORE_EVENT_MAX) {
    XPEDITE_LOG("invalid request - offcore event cannot exeed %d, recieved (%u)\n", 
      XPEDITE_PMC_CTRL_OFFCORE_EVENT_MAX, (unsigned)request_->_offcoreEvtCount);
    return -1;
  }

  for(i=0; i< request_->_gpEvtCount; ++i) {
    eventSelect_->_gpEvtSel[i] = buildPerfEvtSelBitmask(&request_->_gpEvents[i]);
    logRequest(i, &request_->_gpEvents[i], eventSelect_->_gpEvtSel[i]);
  }
  eventSelect_->_gpEvtCount = request_->_gpEvtCount;

  for(i=0; i< request_->_offcoreEvtCount; ++i) {
    eventSelect_->_offcoreEvtSel[i] = request_->_offcoreEvents[i];
    logOffcoreRequest(i, request_->_offcoreEvents[i]);
  }
  eventSelect_->_offcoreEvtCount = request_->_offcoreEvtCount;

  if(request_->_fixedEvtCount) {
    eventSelect_->_fixedEvtGlobalCtl = buildFixedEvtGlobalCtlBitmask(request_->_fixedEvents, request_->_fixedEvtCount);
    if(!eventSelect_->_fixedEvtGlobalCtl) {
      return -1;
    }
    eventSelect_->_fixedEvtSel = buildFixedEvtSelBitmask(request_->_fixedEvents, request_->_fixedEvtCount);
  }
  return 0;
}
