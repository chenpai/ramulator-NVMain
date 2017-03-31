/*******************************************************************************
* Author : chenpai 
*     2016.6.28   
********************************************************************************/

#ifndef __MEMCONTROL_VICTIMTAG_H__
#define __MEMCONTROL_VICTIMTAG_H__

#include "src/MemoryController.h"
#include "Utils/Caches/CacheBank.h"
#include "MemControl/LO-Cache/LO-Cache.h"
#include <vector>
#include <algorithm>
namespace NVM {

class NVMain;

class VictimTag : public MemoryController
{
  public:
    VictimTag( );
    ~VictimTag( );


    void SetConfig( Config *conf, bool createChildren = true );

    bool IssueAtomic( NVMainRequest *req );
    bool IsIssuable( NVMainRequest *request, FailReason *reason = NULL );
    bool IssueCommand( NVMainRequest *req );
    bool IssueFunctional( NVMainRequest *req );
    bool RequestComplete( NVMainRequest *req );

    void Cycle( ncycle_t );

    void RegisterStats( );
    void CalculateStats( );

    NVMain *GetMainMemory( );

 private:
    NVMain *mainMemory;
    //AbstractVictimTag **drcChannels;
	LO_Cache **drcChannels;
    ncounter_t numChannels;
	vector<uint64_t> vic[4];
    void Retranslate( NVMainRequest *req );
	double hit_rate;
	uint64_t DRCLatency;
	uint64_t vicTaghits;
};

};

#endif
