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


#include "MemControl/LO-Cache/LO-Cache.h"
#include "include/NVMHelpers.h"
#include "NVM/nvmain.h"
#include "src/EventQueue.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <cassert>


using namespace NVM;


LO_Cache::LO_Cache( )
{
    //decoder->GetTranslationMethod( )->SetOrder( 5, 1, 4, 3, 2, 6 );

    std::cout << "Created a Latency Optimized DRAM Cache!" << std::endl;

    drcQueueSize = 32;
    starvationThreshold = 4;

    mainMemory = NULL;
    mainMemoryConfig = NULL;
    functionalCache = NULL;
    drc_read_hits = 0;
    drc_hits = 0;
    drc_miss = 0;
    drc_fills = 0;
    drc_evicts = 0;

    rb_hits = 0;
    rb_miss = 0;
    starvation_precharges = 0;

    perfectFills = false;
    max_addr = 0;

    psInterval = 0;
	averageHitLatency = 0.0;
    averageHitQueueLatency = 0.0;
    averageMissLatency = 0.0;
    averageMissQueueLatency = 0.0;
    averageMMLatency = 0.0;
    averageMMQueueLatency = 0.0;
    averageFillLatency = 0.0;
    averageFillQueueLatency = 0.0;

    measuredHitLatencies = 0;
    measuredHitQueueLatencies = 0;
    measuredMissLatencies = 0;
    measuredMissQueueLatencies = 0;
    measuredMMLatencies = 0;
    measuredMMQueueLatencies = 0;
    measuredFillLatencies = 0;
    measuredFillQueueLatencies = 0;
    MLP = 0;
	mlp_count = 0;
    DRCWriteToMem = 0;	
	L3WriteToMem = 0;
    //memset( &hit_count, 0, sizeof(hit_count) );

    /*
    *  Queue options: One queue for all requests 
    *  or a second queue for fill/write requests
    */
    InitQueues( 1 );

    drcQueue = &(transactionQueues[0]);
}

LO_Cache::~LO_Cache( )
{

}

void LO_Cache::SetConfig( Config *conf, bool createChildren )
{
    ncounter_t rows;

    if( conf->KeyExists( "StarvationThreshold" ) )
        starvationThreshold = static_cast<ncounter_t>( conf->GetValue( "StarvationThreshold" ) );
    if( conf->KeyExists( "DRCQueueSize" ) )
        drcQueueSize = static_cast<ncounter_t>( conf->GetValue( "DRCQueueSize" ) );

    if( conf->KeyExists( "PerfectFills" ) && conf->GetString( "PerfectFills" ) == "true" )
        perfectFills = true;
    if( conf->KeyExists( "TemFill" ) && conf->GetString( "TemFill" ) == "true" )
        TemFill = true;
	else
		TemFill = false;
    if( conf->KeyExists( "isPBMonitor" ) && conf->GetString( "isPBMonitor" ) == "true" )
        isPBMonitor = true;
	else
		isPBMonitor = false;			
    ranks = static_cast<ncounter_t>( conf->GetValue( "RANKS" ) );
    banks = static_cast<ncounter_t>( conf->GetValue( "BANKS" ) );
    rows  = static_cast<ncounter_t>( conf->GetValue( "ROWS" ) );


    functionalCache = new CacheBank**[ranks];
	BLP = new bool*[ranks];
	blp_value = 0;
	blp_count = 0;
    for( ncounter_t i = 0; i < ranks; i++ )
    {
        functionalCache[i] = new CacheBank*[banks];
		BLP[i] = new bool[banks];
        for( ncounter_t j = 0; j < banks; j++ )
        {
            /*
             *  The LO-Cache has the data tag (8 bytes) along with 64
             *  bytes for the cache line. The cache is direct mapped,
             *  so we will have up to 28 cache lines + tags per row,
             *  an assoc of 1, and cache line size of 64 bytes.
             */
            functionalCache[i][j] = new CacheBank( rows * 28, 1, 64);
			BLP[i][j] = 0;
        }
    }

    MemoryController::SetConfig( conf, createChildren );

    SetDebugName( "LO-Cache", conf );
}

void LO_Cache::RegisterStats( )
{
	AddStat(drc_read_hits);
    AddStat(drc_hits);
    AddStat(drc_miss);
    AddStat(drc_hitrate);
    AddStat(drc_fills);
    //AddStat(drc_evicts);
	AddStat(DRCWriteToMem);
	AddStat(L3WriteToMem);
    //AddStat(rb_hits);
    //AddStat(rb_miss);
    //AddStat(starvation_precharges);
    AddStat(averageHitLatency);
    //AddStat(measuredHitLatencies);
    AddStat(averageHitQueueLatency);
    //AddStat(measuredHitQueueLatencies);

    AddStat(averageMissLatency);
    //AddStat(measuredMissLatencies);
    //AddStat(averageMissQueueLatency);
    //AddStat(measuredMissQueueLatencies);

    //AddStat(averageMMLatency);
    //AddStat(measuredMMLatencies);
    //AddStat(averageMMQueueLatency);
    //AddStat(measuredMMQueueLatencies);

    AddStat(averageFillLatency);
    //AddStat(measuredFillLatencies);
    AddStat(averageFillQueueLatency);
    //AddStat(measuredFillQueueLatencies);
    MemoryController::RegisterStats( );
}

void LO_Cache::SetMainMemory( NVMain *mm )
{
    mainMemory = mm;
}

void LO_Cache::CalculateLatency( NVMainRequest *req, double *average, 
        uint64_t *measured )
{
    (*average) = (( (*average) * static_cast<double>(*measured))
                    + static_cast<double>(req->completionCycle)
                    - static_cast<double>(req->issueCycle))
                 / static_cast<double>((*measured)+1);
    (*measured) += 1;
}

void LO_Cache::CalculateQueueLatency( NVMainRequest *req, double *average, 
        uint64_t *measured )
{
    (*average) = (( (*average) * static_cast<double>(*measured))
                    + static_cast<double>(req->issueCycle)
                    - static_cast<double>(req->arrivalCycle))
                 / static_cast<double>((*measured)+1);
    (*measured) += 1;
}

void LO_Cache::CalculateMissLatency( NVMainRequest *req, double *average, 
        uint64_t *measured )
{
    (*average) = (( (*average) * static_cast<double>(*measured))
                    + static_cast<double>(req->completionCycle)
                    - static_cast<double>(req->arrivalCycle))
                 / static_cast<double>((*measured)+1);
    (*measured) += 1;
}

bool LO_Cache::IssueAtomic( NVMainRequest *req )
{
    uint64_t row, bank, rank, subarray;

    req->address.GetTranslatedAddress( &row, NULL, &bank, &rank, NULL, &subarray );

    if( req->address.GetPhysicalAddress() > max_addr ) max_addr = req->address.GetPhysicalAddress( );

    /*
    *  Check for a hit for statistical purposes first.
    */
    if( req->type == WRITE || req->type == WRITE_PRECHARGE || functionalCache[rank][bank]->Present( req->address ) )
    {
        drc_hits++;
    }
    else
    {
        /*
         *  Simply install this cache line, evicting another cache line 
         *  if needed.
         */
        NVMDataBlock dummy;

        if( functionalCache[rank][bank]->SetFull( req->address ) )
        {
            NVMAddress victim;

            (void)functionalCache[rank][bank]->ChooseVictim( req->address, &victim );
            (void)functionalCache[rank][bank]->Evict( victim, &dummy );

            drc_evicts++;
        }

        drc_miss++;
        drc_fills++;

        (void)functionalCache[rank][bank]->Install( req->address, dummy );
    }


    return true;
}

bool LO_Cache::IssueFunctional( NVMainRequest *req )
{
    uint64_t bank, rank;

    req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, NULL, NULL );

    /* Write always hits. */
    //if( req->type == WRITE || req->type == WRITE_PRECHARGE )
      //  return true;

    /* Reads hit if they are in the cache. */
    return functionalCache[rank][bank]->Present( req->address );
}

bool LO_Cache::IsIssuable( NVMainRequest * /*request*/, FailReason * /*fail*/ )
{
    bool rv = true;

    /*
     *  Limit the number of commands in the queue. This will stall the caches/CPU.
     */ 
    if( drcQueue->size( ) >= drcQueueSize )
    {
        rv = false;
    }

    return rv;
}

bool LO_Cache::IssueCommand( NVMainRequest *req )
{
	//cout<<req->address.GetPhysicalAddress( )<<endl;
    bool rv = true;
    req->arrivalCycle = GetEventQueue()->GetCurrentCycle();
    if( req->address.GetPhysicalAddress() > max_addr ) max_addr = req->address.GetPhysicalAddress( );

    ncounter_t rank, bank;
    req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, NULL, NULL );
	//predictor: if DRAM cache miss,send request to both memory and DRAM cache
	if(req->type==READ && req->L3fillDRC == 0)
	{
		if(!IssueFunctional(req))
		{
			req->tag=PSEUDO;
            /* Issue to memory. */
            NVMainRequest *memReq = new NVMainRequest( );
			NVMainRequest *originalReq = new NVMainRequest( );

            *memReq = *req;
            memReq->owner = this;
            memReq->tag = DRC_MEMREAD;
            memReq->type = READ;
			*originalReq = *req; 
  
            assert( outstandingFills.count( req ) == 0 );
            outstandingFills.insert( std::pair<NVMainRequest*, NVMainRequest*>( memReq, originalReq ) );
  
            if (mainMemory->IsIssuable( memReq, NULL )) {
                mainMemory->IssueCommand( memReq );
            } else {
                mainMemory->EnqueuePendingMemoryRequests( memReq );
            }
  
            drc_miss++;
			//if(req->coreid==0)
			//cout<<"send to mem:"<<req->address.GetPhysicalAddress( )<<"time "<<GetEventQueue()->GetCurrentCycle()<<endl;
		}
	}

    if( perfectFills && (req->type == WRITE || req->type == WRITE_PRECHARGE) )
    {
        uint64_t rank, bank;
        NVMDataBlock dummy;
        req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, NULL, NULL );

        if( functionalCache[rank][bank]->SetFull( req->address ) )
        {
            NVMAddress victim;

            (void)functionalCache[rank][bank]->ChooseVictim( req->address, &victim );
            (void)functionalCache[rank][bank]->Evict( victim, &dummy );

            drc_evicts++;
        }

        (void)functionalCache[rank][bank]->Install( req->address, dummy );

        drc_fills++;

        GetEventQueue()->InsertEvent( EventResponse, this, req, 
                                      GetEventQueue()->GetCurrentCycle()+1 );

        //delete req;
    }
    else
    {		
		if(TemFill)
		{
			if(req->type == READ||req->L3fillDRC == 1)
			{
				if(req->L3fillDRC == 1&&req->type ==READ)
				{
				  if(IssueFunctional(req)==false)
				  {
					req->owner = this;			
					req->tag = DRC_FILL;
					req->type = WRITE;
					Enqueue( 0, req );
				  }
				  else
					delete req;
				}
				else 		
				{
				  Enqueue( 0, req );
				}
			}
			else if(req->type == WRITE&&req->L3fillDRC == 0)
			{
				if(functionalCache[rank][bank]->Present( req->address ))
					Enqueue( 0, req );
				else
				{
					if(mainMemory->IsIssuable(req,NULL))
						mainMemory->IssueCommand( req );
					else
						mainMemory->EnqueuePendingMemoryRequests( req );
					L3WriteToMem++;
				}
			}
			else
			{
				delete req;
			}			
		}
		else
		{
			if((req->type == READ&&req->L3fillDRC == 0)||req->type == WRITE)
			{
				if(isPBMonitor&&req->type == WRITE)
				{
					if(functionalCache[rank][bank]->Present( req->address )||req->bypass==0)
						Enqueue( 0, req );
					else
					{
						if(mainMemory->IsIssuable(req,NULL))
							mainMemory->IssueCommand( req );
						else
							mainMemory->EnqueuePendingMemoryRequests( req );
						L3WriteToMem++;
					}
				}
				else
					Enqueue( 0, req );
			}
			else
			{
				delete req;
			}
		}
	}
    
    return rv;
}

bool LO_Cache::RequestComplete( NVMainRequest *req )
{
    bool rv = false;
	//req->completionCycle = GetEventQueue()->GetCurrentCycle();
    if( req->type == REFRESH )
    {
        ProcessRefreshPulse( req );
    }
    else if( req->owner == this )
    {
        if( req->tag == DRC_FILL )
        {
			req->completionCycle = GetEventQueue()->GetCurrentCycle();
            /* Install the missed request */
            uint64_t rank, bank;
            NVMDataBlock dummy;

            req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, NULL, NULL );
				
            if( functionalCache[rank][bank]->SetFull( req->address ) )
            {
                NVMAddress victim;
				bool dirty ;
                dirty = functionalCache[rank][bank]->ChooseVictim( req->address, &victim );
                (void)functionalCache[rank][bank]->Evict( victim, &dummy );

                drc_evicts++;
				if(dirty)
				{
		            NVMainRequest *dirtyWrite = new NVMainRequest( );	
		            dirtyWrite->address = victim;
		            dirtyWrite->owner = this;
		            dirtyWrite->type = WRITE;
		
					if(mainMemory->IsIssuable(dirtyWrite,NULL))
						mainMemory->IssueCommand( dirtyWrite );
					else
					  mainMemory->EnqueuePendingMemoryRequests( dirtyWrite );
					DRCWriteToMem++;
				}
            }

            (void)functionalCache[rank][bank]->Install( req->address, dummy );

            drc_fills++;
        CalculateLatency( req, &averageFillLatency, 
                &measuredFillLatencies );

        CalculateQueueLatency( req, &averageFillQueueLatency, 
                &measuredFillQueueLatencies );
		
		//	cout <<"Filled request"<<req->address.GetPhysicalAddress()<<endl;
        }
        /*
         *  Intercept memory read requests from misses to create a fill request.
         */
        else if( req->tag == DRC_MEMREAD )
        {
			//if(TemFill == true)
			//	req->bypass = 1;
			if(req->bypass == 0)
			{
	            /* Issue as a fill request. */
	            NVMainRequest *fillReq = new NVMainRequest( );	
	            *fillReq = *req;
	            fillReq->owner = this;
	            fillReq->tag = DRC_FILL;
	            fillReq->type = WRITE;
	            fillReq->arrivalCycle = GetEventQueue()->GetCurrentCycle();
	
	            Enqueue( 0, fillReq );
			}

            /* Find the original request and send back to requestor. */
            assert( outstandingFills.count( req ) > 0 );
            NVMainRequest *originalReq = outstandingFills[req];
		    originalReq->completionCycle = GetEventQueue()->GetCurrentCycle();	
			uint64_t rank, bank;
            req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, NULL, NULL );

			//CalculateLatency( req, &averageMMLatency, &measuredMMLatencies );
			//CalculateQueueLatency( req, &averageMMQueueLatency, &measuredMMQueueLatencies );
			//cout<<"arrive DRC:"<<req->address.GetPhysicalAddress( )<<"time "<<GetEventQueue()->GetCurrentCycle()<<endl;
			CalculateMissLatency( originalReq, &averageMissLatency, &measuredMissLatencies );
			//CalculateQueueLatency( originalReq, &averageMissQueueLatency, &measuredMissQueueLatencies );
		
            outstandingFills.erase( req );
			
            GetParent( )->RequestComplete( originalReq );
			BLP[rank][bank]=0;
            rv = false;
        }
        else
        {
            // Unknown tag is a problem.
            //assert( false );
        }

        delete req;
        rv = true;
    }
    /*
     *  Intercept read and write requests from parent modules
     */
    else
    {
		req->completionCycle = GetEventQueue()->GetCurrentCycle();
        uint64_t rank, bank;

        req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, NULL, NULL );

        if( req->type == WRITE || req->type == WRITE_PRECHARGE )
        {
            /*
             *  LOCache has no associativity -- Just replace whatever is in the set.
             */
            NVMDataBlock dummy;
            bool hit = functionalCache[rank][bank]->Present( req->address );
			if(hit)
				functionalCache[rank][bank]->Write( req->address,dummy);
			else
			{
				if( functionalCache[rank][bank]->SetFull( req->address ) )
				{
					NVMAddress victim;
				    bool dirty ;	
					dirty = functionalCache[rank][bank]->ChooseVictim( req->address, &victim );
					(void)functionalCache[rank][bank]->Evict( victim, &dummy );
	
					drc_evicts++;
					if(dirty)
					{
			            NVMainRequest *dirtyWrite = new NVMainRequest( );	
			            dirtyWrite->address = victim;
			            dirtyWrite->owner = this;
			            dirtyWrite->type = WRITE;
			
						if(mainMemory->IsIssuable(dirtyWrite,NULL))
							mainMemory->IssueCommand( dirtyWrite );
						else
						  mainMemory->EnqueuePendingMemoryRequests( dirtyWrite );
						DRCWriteToMem++;
					}
				}
				drc_fills++;
				dummy.dirty = true;
				(void)functionalCache[rank][bank]->Install( req->address, dummy );
			}
			drc_hits++;
            /* Send back to requestor. */
            rv = false;

			CalculateLatency( req, &averageHitLatency, &measuredHitLatencies );
			CalculateQueueLatency( req, &averageHitQueueLatency, 
                &measuredHitQueueLatencies );
			GetParent( )->RequestComplete( req );
			BLP[rank][bank]=0;

        }
        else if( req->type == READ || req->type == READ_PRECHARGE )
        {
            /* Check for a hit. */
            bool hit = functionalCache[rank][bank]->Present( req->address );

            /* On a miss, send to main memory. */
            if( !hit )
            {
				if(req->tag!=PSEUDO)
				{
	              /* Issue as a fill request. */
	              NVMainRequest *memReq = new NVMainRequest( );
	
	              *memReq = *req;
	              memReq->owner = this;
	              memReq->tag = DRC_MEMREAD;
	              memReq->type = READ;
	              memReq->arrivalCycle = GetEventQueue()->GetCurrentCycle();
	
	              assert( outstandingFills.count( req ) == 0 );
	              outstandingFills.insert( std::pair<NVMainRequest*, NVMainRequest*>( memReq, req ) );
	
	              if (mainMemory->IsIssuable( memReq, NULL )) {
	                  mainMemory->IssueCommand( memReq );
	              } else {
	                  mainMemory->EnqueuePendingMemoryRequests( memReq );
	              }
	              drc_miss++;
				}
				else
				  delete req;

                //cout << "Missed request for 0x" <<req->address.GetPhysicalAddress()<< std::endl;
            }
            else
            {
				//cout<<"read hit "<<req->address.GetPhysicalAddress()<<endl;
				if(req->tag==PSEUDO)
				{
				  drc_miss--;
				}
                /* Send back to requestor. */
                rv = false;
				drc_read_hits++;
                drc_hits++;
				CalculateLatency( req, &averageHitLatency, &measuredHitLatencies );
				CalculateQueueLatency( req, &averageHitQueueLatency, 
                &measuredHitQueueLatencies );
				NVMDataBlock dummy;
				functionalCache[rank][bank]->Read(req->address,&dummy);
                GetParent( )->RequestComplete( req );
				BLP[rank][bank]=0;
                //cout << "Hit request for 0x" << req->address.GetPhysicalAddress()<<endl;
            }
        }
        else
        {
            assert( false );
        }

    }

    return rv;
}

void LO_Cache::Cycle( ncycle_t steps )
{
    NVMainRequest *nextRequest = NULL;

    /* Check for starved requests BEFORE row buffer hits. */
    if( FindStarvedRequest( *drcQueue, &nextRequest ) )
    {
        rb_miss++;
        starvation_precharges++;
    }
    /* Check for row buffer hits. */
    else if( FindRowBufferHit( *drcQueue, &nextRequest) )
    {
		if(!(nextRequest->tag == DRC_FILL))
        rb_hits++;
    }
    /* Find the oldest request that can be issued. */
    else if( FindOldestReadyRequest( *drcQueue, &nextRequest ) )
    {
        rb_miss++;
    }
    /* Find requests to a bank that is closed. */
    else if( FindClosedBankRequest( *drcQueue, &nextRequest ) )
    {
        rb_miss++;
    }
    else
    {
        nextRequest = NULL;
    }

    /* Issue the commands for this transaction. */
    if( nextRequest != NULL )
    {
        //cout << "Enqueueing request for 0x" <<nextRequest->address.GetPhysicalAddress() <<endl;

        IssueMemoryCommands( nextRequest );
		ncounter_t rank, bank;
		nextRequest->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, NULL, NULL );
		if(BLP[rank][bank]==0)
		  BLP[rank][bank]=1;
    }

    /* Issue any commands in the command queues. */
    CycleCommandQueues( );

    MemoryController::Cycle( steps );
	int count=0;
	for(uint64_t i=0;i<ranks;i++)
	  for(uint64_t j=0;j<banks;j++)
	  {
		  if(BLP[i][j]==1 )
			count++;
	  }
	if(count!=0)
	{
		blp_value = (( blp_value * static_cast<double>(blp_count))+ count)
                 / static_cast<double>(blp_count + 1);
		blp_count += 1;
	}
	if((transactionQueues[0].size()+count)!=0)
	{
		MLP = (( MLP * static_cast<double>(mlp_count))+transactionQueues[0].size()+count)
                 / static_cast<double>(mlp_count + 1);
		mlp_count += 1;
	}

}

void LO_Cache::CalculateStats( )
{
	hits = drc_hits;
    misss = drc_miss;
	HitLatency = averageHitLatency; 
	HitQueueLatency	= averageHitQueueLatency;
    MissLatency = averageMissLatency;
	MissQueueLatency = averageMissQueueLatency;
    drc_hitrate = 0.0;
    if( drc_hits+drc_miss > 0 )
        drc_hitrate = static_cast<float>(drc_hits) / static_cast<float>(drc_miss+drc_hits);

    MemoryController::CalculateStats( );

	cout<<"BLP:"<<blp_value<<endl;
	//cout<<"blp_count:"<<blp_count<<endl;
	cout<<"MLP:"<<MLP<<endl;
	//cout<<"mlp_count:"<<mlp_count<<endl;
	//unsigned int m,n; 
    for(unsigned j = 0; j < 2; j++ )
    {

      //functionalCache[0][j]->cache_print();
    }
//    uint64_t notem = 0,hastem =0;
//	//cout<<"DRC_idle: ";
//	for( m = 0; m < ranks; m++ )
//	  for( n = 0; n < banks; n++ )
//	  {
//		uint64_t tems=0;
//		notem+=functionalCache[m][n]->NoTemporalCount(tems);
//		hastem+=tems;
//		//cout<<DRC_idle[i][j]<<" ";
//	  }
//	cout<<endl<<"NoTemporalCount = "<<notem<<" hasTemCount = "<<hastem<<endl;
//	cout<<"DRCRuning: "<<DRCRuning<<endl;
//	uint64_t TemporalCounters[100];
//	memset(TemporalCounters,0,sizeof(TemporalCounters));
//	unsigned int i,j,k; 
//	for( i = 0; i < ranks; i++ )
//	  for( j = 0; j < banks; j++ )
//	    for( k = 0; k < 100; k++ )
//			TemporalCounters[k]+=functionalCache[i][j]->temporals_counters[k];
//	cout<<"Stat for Temporal:"<<endl;
//	for( k = 0; k < 100; k++ )
//	{
//		if(TemporalCounters[k]!=0)
//		  cout<<" Temporal"<<k<<" has "<<TemporalCounters[k];
//	}
	cout<<endl;	
}

void LO_Cache::CreateCheckpoint( std::string dir )
{
    /* Use our statName as the file to write in the checkpoint directory. */
    for( ncounter_t rankIdx = 0; rankIdx < ranks; rankIdx++ )
    {
        for( ncounter_t bankIdx = 0; bankIdx < banks; bankIdx++ )
        {
            std::stringstream cpt_file;
            cpt_file.str("");
            cpt_file << dir << "/" << statName << "_r";
            cpt_file << rankIdx << "_b" << bankIdx;

            std::ofstream cpt_handle;
            
            cpt_handle.open( cpt_file.str().c_str(), std::ofstream::out | std::ofstream::trunc | std::ofstream::binary );

            if( !cpt_handle.is_open( ) )
            {
                std::cout << "LO_Cache: Warning: Could not open checkpoint file: " << cpt_file << "!" << std::endl;
            }
            else
            {
                /* Iterate over cache sets, since they may not be allocated contiguously. */
                for( uint64_t set = 0; set < functionalCache[rankIdx][bankIdx]->numSets; set++ )
                {
                    cpt_handle.write( (const char *)(functionalCache[rankIdx][bankIdx]->cacheEntry[set]), 
                                      sizeof(CacheEntry)*functionalCache[rankIdx][bankIdx]->numAssoc );
                }

                cpt_handle.close( );
            }

            /* Write checkpoint information. */
            /* Note: For future compatability only at the memory. This is not read during restoration. */
            std::string cpt_info = cpt_file.str() + ".json";

            cpt_handle.open( cpt_info.c_str(), std::ofstream::out | std::ofstream::trunc | std::ofstream::binary );

            if( !cpt_handle.is_open() )
            {
                std::cout << "LO_Cache: Warning: Could not open checkpoint info file: " << cpt_info << "!" << std::endl;
            }
            else
            {
                std::string cpt_info_str = "{\n\t\"Version\": 1\n}";
                cpt_handle.write( cpt_info_str.c_str(), cpt_info_str.length() ); 

                cpt_handle.close();
            }
        }
    }

    NVMObject::CreateCheckpoint( dir );
}

void LO_Cache::RestoreCheckpoint( std::string dir )
{
    for( ncounter_t rankIdx = 0; rankIdx < ranks; rankIdx++ )
    {
        for( ncounter_t bankIdx = 0; bankIdx < banks; bankIdx++ )
        {
            std::stringstream cpt_file;
            cpt_file.str("");
            cpt_file << dir << "/" << statName << "_r";
            cpt_file << rankIdx << "_b" << bankIdx;

            std::ifstream cpt_handle;

            cpt_handle.open( cpt_file.str().c_str(), std::ifstream::ate | std::ifstream::binary );

            if( !cpt_handle.is_open( ) )
            {
                std::cout << "LO_Cache: Warning: Could not open checkpoint file: " << cpt_file << "!" << std::endl;
            }
            else
            {
                std::streampos expectedSize = sizeof(CacheEntry)*functionalCache[rankIdx][bankIdx]->numSets*functionalCache[rankIdx][bankIdx]->numAssoc;
                if( cpt_handle.tellg( ) != expectedSize )
                {
                    std::cout << "LO_Cache: Warning: Expected checkpoint size differs from DRAM cache configuration. Skipping restore." << std::endl;
                }
                else
                {
                    cpt_handle.close( );

                    cpt_handle.open( cpt_file.str().c_str(), std::ifstream::in | std::ifstream::binary );

                    /* Iterate over cache sets, since they may not be allocated contiguously. */
                    for( uint64_t set = 0; set < functionalCache[rankIdx][bankIdx]->numSets; set++ )
                    {
                        cpt_handle.read( (char *)(functionalCache[rankIdx][bankIdx]->cacheEntry[set]), 
                                          sizeof(CacheEntry)*functionalCache[rankIdx][bankIdx]->numAssoc );
                    }

                    cpt_handle.close( );

                    std::cout << "LO_Cache: Checkpoint read " << (sizeof(CacheEntry)*functionalCache[rankIdx][bankIdx]->numAssoc*functionalCache[rankIdx][bankIdx]->numSets) << " bytes." << std::endl;
                }
            }
        }
    }

    NVMObject::RestoreCheckpoint( dir );
}
