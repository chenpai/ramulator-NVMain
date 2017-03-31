/*******************************************************************************
* Author : chenpai 
*   6.24
*******************************************************************************/

#ifndef __MEMCONTROL_BEAR_H__
#define __MEMCONTROL_BEAR_H__

#include "src/MemoryController.h"
#include "Utils/Caches/CacheBank.h"
#include "MemControl/LO-Cache/LO-Cache.h"
#include <map>
namespace NVM {

class NVMain;

class BEAR : public MemoryController
{
  public:
    BEAR( );
    ~BEAR( );


    void SetConfig( Config *conf, bool createChildren = true );

    bool IssueAtomic( NVMainRequest *req );
    bool IsIssuable( NVMainRequest *request, FailReason *reason = NULL );
    bool IssueCommand( NVMainRequest *req );
    bool IssueFunctional( NVMainRequest *req );
    bool RequestComplete( NVMainRequest *req );
	bool IsPBSampel(NVMainRequest *req);
	bool IsBABSampel(NVMainRequest *req);

    void Cycle( ncycle_t );

    void RegisterStats( );
    void CalculateStats( );

    NVMain *GetMainMemory( );
	map<uint64_t,short> HMP;
	uint64_t predicttrue;
	uint64_t predictfalse;
 private:
    NVMain *mainMemory;
    LO_Cache **drcChannels;
	//LH_Cache **drcChannels;
    ncounter_t numChannels;

    void Retranslate( NVMainRequest *req );
	double hit_rate;
	uint64_t DRCLatency;
	int BABMissCounter,BABCounter,PBMissCounter,PBCounter;
	bool modebit;
	int runtimechange;
	unsigned int seed;
};

};

#endif
