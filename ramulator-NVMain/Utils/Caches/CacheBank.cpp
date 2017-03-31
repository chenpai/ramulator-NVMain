/*******************************************************************************
* Copyright (c) 2012-2014, The Microsystems Design Labratory (MDL)
* Department of Computer Science and Engineering, The Pennsylvania State University
* All rights reserved.
* 
* This source code is part of NVMain - A cycle accurate timing, bit accurate
* energy simulator for both volatile (e.g., DRAM) and non-volatile memory
* (e.g., PCRAM). The source code is free and you can redistribute and/or
* modify it by providing that the following conditions are met:
* 
*  1) Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer.
* 
*  2) Redistributions in binary form must reproduce the above copyright notice,
*     this list of conditions and the following disclaimer in the documentation
*     and/or other materials provided with the distribution.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
* Author list: 
*   Matt Poremba    ( Email: mrp5060 at psu dot edu 
*                     Website: http://www.cse.psu.edu/~poremba/ )
*******************************************************************************/

#include "Utils/Caches/CacheBank.h"
#include "include/NVMHelpers.h"
#include "src/EventQueue.h"
#include <fstream>
#include <iostream>
#include <cassert>

using namespace NVM;

CacheBank::CacheBank( uint64_t sets, uint64_t assoc, uint64_t lineSize )
{
    uint64_t i, j;

    cacheEntry = new CacheEntry* [ sets ];
    for( i = 0; i < sets; i++ )
    {
        cacheEntry[i] = new CacheEntry[assoc];
        for( j = 0; j < assoc; j++ )
        {
            /* Clear valid bit, dirty bit, etc. */
            cacheEntry[i][j].flags = CACHE_ENTRY_NONE;
			cacheEntry[i][j].temporal = 0;
        }
    }

    numSets = sets;
    numAssoc = assoc;
    cachelineSize = lineSize;
    LH_numSets = 2048;//chushihua
	LH_numAssoc = 4;
    state = CACHE_IDLE;
    stateTimer = 0;
	offset = (uint64_t)mlog2( (int)cachelineSize );

    decodeClass = NULL;
    decodeFunc = NULL;
    SetDecodeFunction( this, 
            static_cast<CacheSetDecoder>(&NVM::CacheBank::DefaultDecoder) );

    readTime = 1; // 1 cycle
    writeTime = 1;  // 1 cycle
	no_temporals = 0;
    isMissMap = false;
	isATcache = false;
	isTemMonitor = false;
	memset(temporals_counters,0,sizeof(temporals_counters));
}

CacheBank::~CacheBank( )
{
    uint64_t i;

    for( i = 0; i < numSets; i++ )
    {
        delete [] cacheEntry[i];
    }

    delete [] cacheEntry;
}

void CacheBank::SetDecodeFunction( NVMObject *dcClass, CacheSetDecoder dcFunc )
{
    decodeClass = dcClass;
    decodeFunc = dcFunc;
}

uint64_t CacheBank::DefaultDecoder( NVMAddress &addr )
{
	return (addr.GetPhysicalAddress( ) >> 10/*offset*/) % numSets;
}

uint64_t CacheBank::SetID( NVMAddress& addr )
{
    /*
     *  By default we'll just chop off the bits for the cacheline and use the
     *  least significant bits as the set address, and the remaining bits are 
     *  the tag bits.
     */
    uint64_t setID;

    //if( isMissMap )
    //    setID = (addr.GetPhysicalAddress( )) % numSets;
    if(isTemMonitor )
	{
		setID = ((addr.GetPhysicalAddress( ) >> 6/*offset*/)%7500)/30;
		//cout<<"setID"<<setID<<endl;
	}
	else
		setID = (decodeClass->*decodeFunc)( addr );

    return setID;
}

CacheEntry *CacheBank::FindSet( NVMAddress& addr )
{
    /*
     *  By default we'll just chop off the bits for the cacheline and use the
     *  least significant bits as the set address, and the remaining bits are 
     *  the tag bits.
     */
    uint64_t setID = SetID( addr );

    return cacheEntry[setID];
}

bool CacheBank::Present( NVMAddress& addr )
{
    CacheEntry *set = FindSet( addr );
    bool found = false;

    for( uint64_t i = 0; i < numAssoc; i++ )
    {
        if( (set[i].address.GetPhysicalAddress( )>>offset) == (addr.GetPhysicalAddress( )>>offset)&& (set[i].flags & CACHE_ENTRY_VALID ) )
        {
            found = true;
            break;
        }
    }

    return found;
}

bool CacheBank::AT_Present( NVMAddress& addr,bool & prefetch )
{
    CacheEntry *set = FindSet( addr );
    bool found = false;
	uint64_t AT_tag,addr_tag;
    for( uint64_t i = 0; i < numAssoc && !found; i++ )
    {
		AT_tag = (set[i].address.GetPhysicalAddress( )>>offset) % LH_numSets;
		addr_tag = (addr.GetPhysicalAddress( )>>offset) % LH_numSets;
		if( AT_tag == addr_tag && (set[i].flags & CACHE_ENTRY_VALID ) )
        {
			for( uint64_t j = 0; j < LH_numAssoc; j++ )
			{
				if( set[i].data.tags[j]>>offset == addr.GetPhysicalAddress( )>>offset/* && (set[i].data.tags[j].flags & CACHE_ENTRY_VALID )*/ )
				{
					found = true;
					if(set[i].access==1)
					  prefetch = 0;
					else
					  prefetch = 1;
					break;
				}

			}
        }
    }
    return found;
}

bool CacheBank::AT_Present_set( NVMAddress& addr )
{
    CacheEntry *set = FindSet( addr );
    bool found = false;
	uint64_t AT_tag,addr_tag;
    for( uint64_t i = 0; i < numAssoc ; i++ )
    {
		AT_tag = (set[i].address.GetPhysicalAddress( )>>offset) % LH_numSets;
		addr_tag = (addr.GetPhysicalAddress( )>>offset) % LH_numSets;
		if( AT_tag == addr_tag && (set[i].flags & CACHE_ENTRY_VALID ) )
        {
			found = true;
			break;
        }
    }
    return found;
}

bool CacheBank::SetFull( NVMAddress& addr )
{
    CacheEntry *set = FindSet( addr );
    bool rv = true;

    for( uint64_t i = 0; i < numAssoc; i++ )
    {
        /* If there is an invalid entry (e.g., not used) the set isn't full. */
        if( !(set[i].flags & CACHE_ENTRY_VALID) )
        {
            rv = false;
            break;
        }
    }

    return rv;
}

bool CacheBank::Install( NVMAddress& addr, NVMDataBlock& data )
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    assert( !Present( addr ) );
    for( uint64_t i = 0; i < numAssoc; i++ )
    {
	    if( !(set[i].flags & CACHE_ENTRY_VALID) )
        {
		    set[i].address = addr;
            set[i].data = data;
            set[i].flags |= CACHE_ENTRY_VALID; 
			set[i].temporal = 0;
			if(data.dirty == true)
				set[i].flags |= CACHE_ENTRY_DIRTY;
		    /* Move cache entry to MRU position */
            CacheEntry tmp;

            tmp.flags = set[i].flags;
            tmp.address = set[i].address;
            tmp.data = set[i].data;
			tmp.temporal = set[i].temporal;
            for( uint64_t j = i; j > 0; j-- )
            {
                set[j].flags = set[j-1].flags;
                set[j].address = set[j-1].address;
                set[j].data = set[j-1].data;
				set[j].temporal = set[j-1].temporal;
            }

            set[0].flags = tmp.flags;
            set[0].address = tmp.address;
            set[0].data = tmp.data;
			set[0].temporal = tmp.temporal;
            rv = true;
	        break;
        }
    }

    return rv;
}

bool CacheBank::AT_Install( NVMAddress& addr,uint64_t (*settags)[29],int num,uint64_t original)
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;
	
    //assert(!AT_Present( addr ,prefetch ));

    for( uint64_t i = 0; i < numAssoc; i++ )
    {
//		for(uint64_t j=0;j<LH_numAssoc;j++)
//			cout<<"set["<<i<<"]"<<set[i].data.tags[j]<<endl;
        if( !(set[i].flags & CACHE_ENTRY_VALID) )
        {
            set[i].address = addr;
			for(uint64_t j=0;j<LH_numAssoc;j++)
				set[i].data.tags[j] = settags[num][j];
            set[i].flags |= CACHE_ENTRY_VALID; 
			if(original==addr.GetPhysicalAddress( ))
				set[i].access = 1;
			else
				set[i].access = 0;
            rv = true;
            break;
        }
    }

    return rv;
}

bool CacheBank::Read( NVMAddress& addr, NVMDataBlock *data )
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    //assert( Present( addr )||AT_Present( addr, prefetch ) );
	uint64_t AT_tag,addr_tag;
	//cout<<"READ"<<endl;
	//for( uint64_t i = 0; i < numAssoc; i++ )
	//  cout<<i<<":"<<set[i].address.GetPhysicalAddress( )<<endl;
    for( uint64_t i = 0; i < numAssoc; i++ )
    {
		if(isATcache)
		{
			AT_tag = (set[i].address.GetPhysicalAddress( )>>offset) % LH_numSets;
			addr_tag = (addr.GetPhysicalAddress( )>>offset) % LH_numSets;

		}
		else
		{
			AT_tag = set[i].address.GetPhysicalAddress( )>>offset ;
			addr_tag = addr.GetPhysicalAddress( )>>offset;			
		}
        if( AT_tag ==addr_tag && (set[i].flags & CACHE_ENTRY_VALID) )
        {
            *data = set[i].data;
            rv = true;
			set[i].temporal ++;

            /* Move cache entry to MRU position */
            CacheEntry tmp;

            tmp.flags = set[i].flags;
            tmp.address = set[i].address;
            tmp.data = set[i].data;
			tmp.temporal = set[i].temporal;

            for( uint64_t j = i; j > 0; j-- )
            {
                set[j].flags = set[j-1].flags;
                set[j].address = set[j-1].address;
                set[j].data = set[j-1].data;
				set[j].temporal = set[j-1].temporal;
            }

            set[0].flags = tmp.flags;
            set[0].address = tmp.address;
            set[0].data = tmp.data;
			set[0].temporal = tmp.temporal;
        }
    }
	//for( uint64_t i = 0; i < numAssoc; i++ )
	//  cout<<i<<":"<<set[i].address.GetPhysicalAddress( )<<endl;

    return rv;
}

bool CacheBank::Write( NVMAddress& addr, NVMDataBlock& data )
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    //assert( Present( addr )||AT_Present( addr, prefetch) );
	uint64_t AT_tag,addr_tag;
	//cout<<"WRITE"<<endl;
	//for( uint64_t i = 0; i < numAssoc; i++ )
	//  cout<<i<<":"<<set[i].address.GetPhysicalAddress( )<<endl;
    for( uint64_t i = 0; i < numAssoc; i++ )
    {
		if(isATcache)
		{
			AT_tag = (set[i].address.GetPhysicalAddress( )>>offset) % LH_numSets;
			addr_tag = (addr.GetPhysicalAddress( )>>offset) % LH_numSets;

		}
		else
		{
			AT_tag = set[i].address.GetPhysicalAddress( )>>offset ;
			addr_tag = addr.GetPhysicalAddress( )>>offset;			
		}
        if( AT_tag ==addr_tag && (set[i].flags & CACHE_ENTRY_VALID) )	
        {
            set[i].data = data;
            set[i].flags |= CACHE_ENTRY_DIRTY;
            rv = true;
			set[i].temporal ++;
            /* Move cache entry to MRU position */
            CacheEntry tmp;

            tmp.flags = set[i].flags;
            tmp.address = set[i].address;
            tmp.data = set[i].data;
			tmp.temporal = set[i].temporal;

            for( uint64_t j = i; j > 0; j-- )
            {
                set[j].flags = set[j-1].flags;
                set[j].address = set[j-1].address;
                set[j].data = set[j-1].data;
				set[j].temporal = set[j-1].temporal;
            }

            set[0].flags = tmp.flags;
            set[0].address = tmp.address;
            set[0].data = tmp.data;
			set[0].temporal = tmp.temporal;
        }
    }
	//for( uint64_t i = 0; i < numAssoc; i++ )
	//  cout<<i<<":"<<set[i].address.GetPhysicalAddress( )<<endl;
    return rv;
}

/* 
 *  Updates data without changing dirty bit or LRU position
 *  Returns true if the block was found and updated.
 */
bool CacheBank::UpdateData( NVMAddress& addr, NVMDataBlock& data )
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    assert( Present( addr ) );

    for( uint64_t i = 0; i < numAssoc; i++ )
    {		
        if( (set[i].address.GetPhysicalAddress( )>>offset) == (addr.GetPhysicalAddress( )>>offset )
            && (set[i].flags & CACHE_ENTRY_VALID) )
        {
            set[i].data = data;
            rv = true;
        }
    }

    return rv;
}

bool CacheBank::AT_UpdateData( NVMAddress& addr, uint64_t settags[4][29], int num )
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    //assert( AT_Present( addr, prefetch) );
	uint64_t AT_tag,addr_tag;
    for( uint64_t i = 0; i < numAssoc; i++ )
    {
		AT_tag = (set[i].address.GetPhysicalAddress( )>>offset) % LH_numSets;
		addr_tag = (addr.GetPhysicalAddress( )>>offset) % LH_numSets;
        if( AT_tag ==addr_tag && (set[i].flags & CACHE_ENTRY_VALID) )
        {
			for(uint64_t j=0;j<LH_numAssoc;j++)
				set[i].data.tags[j] = settags[num][j];
            rv = true;
        }
    }

    return rv;
}

/* Return true if the victim data is dirty. */
bool CacheBank::ChooseVictim( NVMAddress& addr, NVMAddress *victim )
{
    bool rv = false;
    CacheEntry *set = FindSet( addr );

    assert( SetFull( addr ) );
    assert( set[numAssoc-1].flags & CACHE_ENTRY_VALID );

    *victim = set[numAssoc-1].address;
    
    if( set[numAssoc-1].flags & CACHE_ENTRY_DIRTY )
        rv = true;

    return rv;
}


bool CacheBank::Evict( NVMAddress& addr, NVMDataBlock *data )
{
    bool rv;
    CacheEntry *set = FindSet( addr );

    assert( Present( addr ) );

    rv = false; 

    for( uint64_t i = 0; i < numAssoc; i++ )
    {
		//cout<<"set["<<i<<"]"<<(set[i].address.GetPhysicalAddress( )>>offset)<<"flag:"<<set[i].flags<<endl;
        if( (set[i].address.GetPhysicalAddress( )>>offset) == (addr.GetPhysicalAddress( )>>offset) && (set[i].flags & CACHE_ENTRY_VALID) )
        {
            if( set[i].flags & CACHE_ENTRY_DIRTY )
            {
                *data = set[i].data;
                rv = true;
            }
            else
            {
                *data = set[i].data;
                rv = false;
            }
			if(set[i].temporal <100)
				temporals_counters[set[i].temporal]++;
			else
				temporals_counters[99]++;
            set[i].flags = CACHE_ENTRY_NONE;
			//cout<<"set["<<i<<"]--"<<(set[i].address.GetPhysicalAddress( )>>offset)<<"flag:"<<set[i].flags<<endl;
            break;
        }
    }

    return rv;
}

void CacheBank::SetReadTime( uint64_t rtime )
{
    readTime = rtime;
}

void CacheBank::SetWriteTime( uint64_t wtime )
{
    writeTime = wtime;
}

uint64_t CacheBank::GetReadTime( )
{
    return readTime;
}

uint64_t CacheBank::GetWriteTime( )
{
    return writeTime;
}

uint64_t CacheBank::GetAssociativity( )
{
    return numAssoc;
}

uint64_t CacheBank::GetCachelineSize( )
{
    return cachelineSize;
}

uint64_t CacheBank::GetSetCount( )
{
    return numSets;
}

double CacheBank::GetCacheOccupancy( )
{
    double occupancy;
    uint64_t valid, total;

    valid = 0;
    total = numSets*numAssoc;

    for( uint64_t setIdx = 0; setIdx < numSets; setIdx++ )
    {
        CacheEntry *set = cacheEntry[setIdx];

        for( uint64_t assocIdx = 0; assocIdx < numAssoc; assocIdx++ )
        {
            if( set[assocIdx].flags & CACHE_ENTRY_VALID )
                valid++;
        }
    }

    occupancy = static_cast<double>(valid) / static_cast<double>(total);

    return occupancy;
}

bool CacheBank::IsIssuable( NVMainRequest * /*req*/, FailReason * /*reason*/ )
{
    bool rv = false;

    /* We can issue if the cache is idle. Pretty simple */
    if( state == CACHE_IDLE )
    {
        rv = true;
    }
    else
    {
        rv = false;
    }

    return rv;
}

bool CacheBank::IssueCommand( NVMainRequest *nreq )
{
    NVMDataBlock dummy;
    CacheRequest *req = static_cast<CacheRequest *>( nreq->reqInfo );

    assert( IsIssuable( nreq, NULL ) );

    if( !IsIssuable( nreq, NULL ) )
        return false;
	bool prefetch =0;
    switch( req->optype )
    {
        case CACHE_READ:
			{
            state = CACHE_BUSY;
            stateTimer = GetEventQueue( )->GetCurrentCycle( ) + readTime;
			if(isMissMap)
				req->hit = Present( req->address );
			else if(isATcache)
			{
				req->hit = AT_Present( req->address,prefetch );
			}
			else
			    assert(false);
            if( req->hit )
			{
                Read( req->address, &(req->data) ); 
				if(prefetch == 1)
				{
					NVMAddress ad = req->address; 
					ad.SetPhysicalAddress(ad.GetPhysicalAddress( )>>offset);
					ad.SetPhysicalAddress(ad.GetPhysicalAddress( )-ad.GetPhysicalAddress( )%4);
					ad.SetPhysicalAddress(ad.GetPhysicalAddress( )<<offset);
					for(int i=0;i<4;i++)
					{
					    CacheEntry *s = FindSet( ad );
						uint64_t ATtag,addrtag;
					    for( uint64_t j = 0; j < numAssoc ; j++ )
					    {
							ATtag = (s[j].address.GetPhysicalAddress( )>>offset) % LH_numSets;
							addrtag = (ad.GetPhysicalAddress( )>>offset) % LH_numSets;
							if( ATtag == addrtag && (s[j].flags & CACHE_ENTRY_VALID ) )
					        {
								s[j].access = 1;
								break;
							}
						}
						ad.SetPhysicalAddress(ad.GetPhysicalAddress( )+(1<<offset));
					}
					nreq->ATprefetch = true; 
				}				
			}
            GetEventQueue( )->InsertEvent( EventResponse, this, nreq, stateTimer );
            break;
			}

        case CACHE_WRITE:
			{
            state = CACHE_BUSY;
            stateTimer = GetEventQueue( )->GetCurrentCycle( ) + writeTime;
			if(isATcache)
			{
				uint64_t original = req->address.GetPhysicalAddress( );
				original = original >> offset;
				original = original << offset;
				if(nreq->ATprefetch==true)
				{
					req->address.SetPhysicalAddress(req->address.GetPhysicalAddress( )+(4<<offset));
					original = 0;
				}

				req->address.SetPhysicalAddress(req->address.GetPhysicalAddress( )>>offset);
				req->address.SetPhysicalAddress(req->address.GetPhysicalAddress( )-req->address.GetPhysicalAddress( )%4);
				req->address.SetPhysicalAddress(req->address.GetPhysicalAddress( )<<offset);
				for(int i=0;i<4;i++)
				{
					req->hit = AT_Present_set( req->address );
					if( req->hit )
					{
						AT_UpdateData( req->address, req->settags ,i);
					}
					else
					{
			            if( SetFull( req->address ) )
			            {
			                NVMainRequest *mmEvict = new NVMainRequest( );
			                CacheRequest *evreq = new CacheRequest;
			                
			                ChooseVictim( req->address, &(evreq->address) );
			                Evict( evreq->address, &(evreq->data) );
			
			                evreq->optype = CACHE_EVICT;
			
			                *mmEvict = *nreq;
			                mmEvict->owner = nreq->owner;
			                mmEvict->reqInfo = static_cast<void *>( evreq );
			                mmEvict->tag = nreq->tag;
			
			                GetEventQueue( )->InsertEvent( EventResponse, this, mmEvict,
			                                               stateTimer );
			            }
			
			            AT_Install( req->address, req->settags, i ,original);
					}
					req->address.SetPhysicalAddress(req->address.GetPhysicalAddress( )+(1<<offset));
				}

			}
			else
			{
	            if( SetFull( req->address ) )
	            {
	                NVMainRequest *mmEvict = new NVMainRequest( );
	                CacheRequest *evreq = new CacheRequest;
	                
	                ChooseVictim( req->address, &(evreq->address) );
	                Evict( evreq->address, &(evreq->data) );
	
	                evreq->optype = CACHE_EVICT;
	
	                *mmEvict = *nreq;
	                mmEvict->owner = nreq->owner;
	                mmEvict->reqInfo = static_cast<void *>( evreq );
	                mmEvict->tag = nreq->tag;
	
	                GetEventQueue( )->InsertEvent( EventResponse, this, mmEvict,
	                                               stateTimer );
	            }
	
	            req->hit = Present( req->address );
	            if( req->hit ) 
	                Write( req->address, req->data );
	            else
	                Install( req->address, req->data );
			}

            GetEventQueue( )->InsertEvent( EventResponse, this, nreq, stateTimer );
            break;
			}

        default:
            std::cout << "CacheBank: Unknown operation `" << req->optype << "'!"
                << std::endl;
            break;
    }

    return true;
}

bool CacheBank::RequestComplete( NVMainRequest *req )
{
    GetParent( )->RequestComplete( req );

    state = CACHE_IDLE;

    return true;
}

void CacheBank::Cycle( ncycle_t /*steps*/ )
{
}

void CacheBank::GetSetTags( NVMAddress& addr,uint64_t settags[4][29] )
{
	uint64_t setID = SetID( addr );
	setID = setID-setID%4; 
	for(int i=0;i<4;i++)
	{
		for(uint64_t j=0;j<numAssoc;j++)
		{
			if(cacheEntry[(setID+i)%numSets][j].flags & CACHE_ENTRY_VALID )
				settags[i][j] = cacheEntry[(setID+i)%numSets][j].address.GetPhysicalAddress( );
			else
				settags[i][j] = 0;
		}
	}
}

void CacheBank::GetNextFourSetTags( NVMAddress& addr,uint64_t settags[4][29] )
{
	uint64_t setID = SetID( addr );
	setID = setID-setID%4+4; 
	for(int i=0;i<4;i++)
	{
		for(uint64_t j=0;j<numAssoc;j++)
		{
			if(cacheEntry[(setID+i)%numSets][j].flags & CACHE_ENTRY_VALID )
				settags[i][j] = cacheEntry[(setID+i)%numSets][j].address.GetPhysicalAddress( );
			else
				settags[i][j] = 0;			
//			if(settags[i][j]!=0)
//				cout<<"NextFourSetTags:"<<settags[i][j]<<endl;
		}

	}

}

int CacheBank::MRU_Present( NVMAddress& addr )
{
    CacheEntry *set = FindSet( addr );
    int found = 0;

    for( uint64_t i = 0; i < 9; i++ )
    {
        if( (set[i].address.GetPhysicalAddress( )>>offset) == (addr.GetPhysicalAddress( )>>offset)&& (set[i].flags & CACHE_ENTRY_VALID ) )
        {
            found = i+1;
            break;
        }
    }

    return found;
}

void CacheBank::cache_print( )
{
	std::ofstream in;
    in.open( "cacheout.txt" , std::ofstream::out | std::ofstream::app );
	for(uint64_t i=0;i<numSets;i++)
	{
	  in<<"Set"<<i<<":"<<endl;
	  for(uint64_t j=0;j<numAssoc;j++)
		in<<cacheEntry[i][j].address.GetPhysicalAddress( )<<"   ";
	}
}

uint64_t CacheBank::NoTemporalCount(uint64_t& tem)
{
	for(uint64_t i=0;i<numSets;i++)
	{
	  for(uint64_t j=0;j<numAssoc;j++)
		if(cacheEntry[i][j].temporal == 0)
			no_temporals++;
		else
	   	    tem++;
	}
	return no_temporals;
}

//---------- fixway cache
bool CacheBank::fixwayInstall( NVMAddress& addr, uint64_t& way )
{
	//cout<<"way"<<way<<endl;
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    assert( !Present( addr ) );

	set[way].address = addr;
    set[way].flags |= CACHE_ENTRY_VALID; 
    rv = true;

    return rv;
}

bool CacheBank::fixwayPresent( NVMAddress& addr, uint64_t& way )
{
    CacheEntry *set = FindSet( addr );
    bool found = false;

    if( (set[way].address.GetPhysicalAddress( )>>offset) == (addr.GetPhysicalAddress( )>>offset)&& (set[way].flags & CACHE_ENTRY_VALID ) )
    {
        found = true;
    }

    return found;
}

bool CacheBank::LAIssueCommand( NVMainRequest *nreq )
{
    NVMDataBlock dummy;
    CacheRequest *req = static_cast<CacheRequest *>( nreq->reqInfo );

    assert( IsIssuable( nreq, NULL ) );

    if( !IsIssuable( nreq, NULL ) )
        return false;
    switch( req->optype )
    {
        case CACHE_READ:
			{
            state = CACHE_BUSY;
            stateTimer = GetEventQueue( )->GetCurrentCycle( ) + readTime;
			req->hit = LA_Present( req->address);
            if( req->hit )
			{
                LARead( req->address, req->way);
				//LAGet( req->address, req->way);
			}
			else
			{
				if(nreq->type == WRITE)
				{
					LAInstall( req->address,req->way );
					req->hit = true;
				}
			}
            GetEventQueue( )->InsertEvent( EventResponse, this, nreq, stateTimer );
            break;
			}

        case CACHE_WRITE:
			{
            state = CACHE_BUSY;
            stateTimer = GetEventQueue( )->GetCurrentCycle( ) + writeTime;

            LAInstall( req->address,req->way);

            GetEventQueue( )->InsertEvent( EventResponse, this, nreq, stateTimer );
            break;
			}

        default:
            std::cout << "CacheBank: Unknown operation `" << req->optype << "'!"
                << std::endl;
            break;
    }

    return true;
}

bool CacheBank::LA_Present( NVMAddress& addr)
{
    CacheEntry *set = FindSet( addr );
    bool found = false;
    for( uint64_t i = 0; i < numAssoc && !found; i++ )
    {
		for( uint64_t j = 0; j < LH_numAssoc; j++ )
		{
			if( set[i].data.tags[j]>>11 == addr.GetPhysicalAddress( )>>11 )
			{
				found = true;
				break;
			}
		}
    }
    return found;
}

bool CacheBank::LAInstall( NVMAddress& addr,uint64_t &way)
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;
	if( SetFull( addr ) )
	{
		uint64_t j;
		for(j=0;j<LH_numAssoc;j++)
		{
			if(set[numAssoc-1].data.tags[j]!=0)
			  continue;
			set[numAssoc-1].data.tags[j] = addr.GetPhysicalAddress( );
			break;
		}
		if(j==LH_numAssoc)
		{
			set[numAssoc-1].data.tags[j-1] = addr.GetPhysicalAddress( );
		}
		way = set[numAssoc-1].address.GetPhysicalAddress( );
        rv = true;

	}
	else
	{
	    for( uint64_t i = 0; i < numAssoc; i++ )
	    {
			//for(uint64_t j=0;j<numAssoc;j++)
			//	cout<<"set["<<j<<"]"<<set[j].address.GetPhysicalAddress( )<<endl;
	        if( !(set[i].flags & CACHE_ENTRY_VALID) )
	        {
				//for(uint64_t j=0;j<LH_numAssoc;j++)
				//{
					set[i].data.tags[0] = addr.GetPhysicalAddress( );
				//	break;
				//}
	            set[i].flags |= CACHE_ENTRY_VALID;
				way = set[i].address.GetPhysicalAddress( );
	            rv = true;
	            break;
	        }
	    }
	}
    return rv;
}

bool CacheBank::LAGet( NVMAddress& addr,uint64_t &way)
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    for( uint64_t i = 0; i < numAssoc; i++ )
    {
		for( uint64_t k = 0; k < LH_numAssoc; k++ )
		{	
	        if( set[i].data.tags[k]>>11 == addr.GetPhysicalAddress( )>>11 && (set[i].flags & CACHE_ENTRY_VALID) )
	        {
				way = set[i].address.GetPhysicalAddress( );
	            rv = true;
				return rv;

			}
		}
	}
	return rv;

}
bool CacheBank::LARead( NVMAddress& addr,uint64_t &way)
{
    CacheEntry *set = FindSet( addr );
    bool rv = false;

    for( uint64_t i = 0; i < numAssoc; i++ )
    {
		for( uint64_t k = 0; k < LH_numAssoc; k++ )
		{	
	        if( set[i].data.tags[k]>>11 == addr.GetPhysicalAddress( )>>11 && (set[i].flags & CACHE_ENTRY_VALID) )
	        {
				//for( uint64_t i = 0; i < numAssoc; i++ )
				//	cout<<i<<":"<<set[i].address.GetPhysicalAddress( )<<endl;
				way = set[i].address.GetPhysicalAddress( );
				//cout<<"1:"<<way<<endl;
	            rv = true;
				/* Move page num to MRU position */
				//for( uint64_t j = 0; j < LH_numAssoc; j++ )
				//	cout<<i<<":"<<set[i].data.tags[j]<<endl;

				uint64_t pagenum;
				pagenum = set[i].data.tags[k];
				for( uint64_t p = k; p > 0; p-- )
					set[i].data.tags[p] = set[i].data.tags[p-1];
				set[i].data.tags[0] = pagenum;
				//for( uint64_t j = 0; j < LH_numAssoc; j++ )
				//	cout<<i<<":"<<set[i].data.tags[j]<<endl;

	            /* Move cache entry to MRU position */
	            CacheEntry tmp;
	
	            tmp.flags = set[i].flags;
	            tmp.address = set[i].address;
	            tmp.data = set[i].data;
	
	            for( uint64_t j = i; j > 0; j-- )
	            {
	                set[j].flags = set[j-1].flags;
	                set[j].address = set[j-1].address;
	                set[j].data = set[j-1].data;
	            }
	
	            set[0].flags = tmp.flags;
	            set[0].address = tmp.address;
	            set[0].data = tmp.data;
				//for( uint64_t i = 0; i < numAssoc; i++ )
				//	cout<<i<<":"<<set[i].address.GetPhysicalAddress( )<<endl;

				//way = set[i].address.GetPhysicalAddress( );
				return rv;
	        }
		}
    }

    return rv;
}

int CacheBank::MoRead( NVMAddress& addr, NVMDataBlock *data )
{
    CacheEntry *set = FindSet( addr );
    int rv = 0;
	//for( uint64_t i = 0; i < numAssoc; i++ )
	//{
	//	cout<<i<<": "<<set[i].address.GetPhysicalAddress( )<<endl;
	//}
	uint64_t AT_tag,addr_tag;
    for( uint64_t i = 0; i < numAssoc; i++ )
	{
		AT_tag = set[i].address.GetPhysicalAddress( )>>offset ;
		addr_tag = addr.GetPhysicalAddress( )>>offset;			

        if( AT_tag ==addr_tag && (set[i].flags & CACHE_ENTRY_VALID) )
        {
            *data = set[i].data;
			if(i<=4)
			  rv = 1;
			else if(i>4&&i<=9)
			  rv = 2;
			else
			  rv = 3;
			set[i].temporal ++;

            /* Move cache entry to MRU position */
            CacheEntry tmp;

            tmp.flags = set[i].flags;
            tmp.address = set[i].address;
            tmp.data = set[i].data;
			tmp.temporal = set[i].temporal;

            for( uint64_t j = i; j > 0; j-- )
            {
                set[j].flags = set[j-1].flags;
                set[j].address = set[j-1].address;
                set[j].data = set[j-1].data;
				set[j].temporal = set[j-1].temporal;
            }

            set[0].flags = tmp.flags;
            set[0].address = tmp.address;
            set[0].data = tmp.data;
			set[0].temporal = tmp.temporal;
        }
    }
    return rv;
}

