/*******************************************************************************
* Author : chenpai 
*   2016.7.2
*******************************************************************************/

#ifndef __MEMCONTROL_DYPB_H__
#define __MEMCONTROL_DYPB_H__

#include "src/MemoryController.h"
#include "Utils/Caches/CacheBank.h"
#include "MemControl/LO-Cache/LO-Cache.h"
#include <map>
namespace NVM {

class NVMain;

class DYPB : public MemoryController
{
  public:
    DYPB( );
    ~DYPB( );


    void SetConfig( Config *conf, bool createChildren = true );

    bool IssueAtomic( NVMainRequest *req );
    bool IsIssuable( NVMainRequest *request, FailReason *reason = NULL );
    bool IssueCommand( NVMainRequest *req );
    bool IssueFunctional( NVMainRequest *req );
    bool RequestComplete( NVMainRequest *req );

    void Cycle( ncycle_t );
	bool IsPBSampel(NVMainRequest *req);
	bool IsBABSampel(NVMainRequest *req);
    void RegisterStats( );
    void CalculateStats( );

    NVMain *GetMainMemory( );
 private:
	CacheBank **wMonitor,**rMonitor;
	uint64_t *wSumCounter,*wMissCounter,*wPos1Counter,*wPos2Counter,*wPos3Counter;
	uint64_t *rSumCounter,*rMissCounter,*rPos1Counter,*rPos2Counter,*rPos3Counter;
    NVMain *mainMemory;
    //AbstractDYPB **drcChannels;
	LO_Cache **drcChannels;
    ncounter_t numChannels;
	uint64_t *monitor_evicts;
    void Retranslate( NVMainRequest *req );
	double hit_rate;
	uint64_t DRCLatency;
	double *Pw,*Pr;
	unsigned int seed;
	bool modebit;
	int BABMissCounter,BABCounter,PBMissCounter,PBCounter;

};

};

#endif
