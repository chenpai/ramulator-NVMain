/*******************************************************************************
* 2016.4.11
* Author  : 
* Chen pai   
********************************************************************************/

#ifndef __UTILS_ATCACHE_H__
#define __UTILS_ATCACHE_H__

#include "Utils/Caches/CacheBank.h"
#include "src/Config.h"
#include "src/MemoryController.h"
#include "MemControl/MemoryControllerFactory.h"
#include <queue>

namespace NVM {

#define ATCACHE_READ        tagGen->CreateTag("ATCACHE_READ")
#define ATCACHE_WRITE       tagGen->CreateTag("ATCACHE_WRITE")
#define ATCACHE_FORCE_EVICT tagGen->CreateTag("ATCACHE_FORCE_EVICT")
//#define ATCACHE_HIT         tagGen->CreateTag("ATCACHE_HIT")
//#define ATCACHE_MISS        tagGen->CreateTag("ATCACHE_MISS")

class NVMain;
class LH_Cache;

class ATcache : public MemoryController
{
  public:
    ATcache( );
    ~ATcache( );

    void SetConfig( Config *conf, bool createChildren = true );

    bool QueueFull( NVMainRequest *request );
    bool IsIssuable( NVMainRequest *request, FailReason *reason = NULL );
    bool IssueAtomic( NVMainRequest *req );
    bool IssueCommand( NVMainRequest *req );
    bool RequestComplete( NVMainRequest *req );
	bool IsIssu( NVMainRequest * req, FailReason * /*fail*/ );
    void Cycle( ncycle_t );

    void RegisterStats( );
    void CalculateStats( );
	void Retranslate( NVMainRequest *req );
  private:
    CacheBank *atCache;
    std::queue<NVMainRequest *> atCacheQueue;
    std::queue<NVMainRequest *> atCacheFillQueue;
    uint64_t atCacheQueueSize;

    NVMain *mainMemory;
    LH_Cache **drcChannels;
    ncounter_t numChannels;
	std::queue<NVMainRequest *> pendingDRCRequests;

    /* Stats. */
    uint64_t atCachePrefetch, atCacheWrites;
    uint64_t atCacheHits, atCacheMisses;
    uint64_t atCacheForceEvicts;
    uint64_t atCacheMemReads;
	double AT_hit_rate;
};

};

#endif
