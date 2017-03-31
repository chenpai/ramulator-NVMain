/*******************************************************************************
* 2016.4.11
* Author : 
* Chen pai  
********************************************************************************/

#include "MemControl/ATcache/ATcache.h"
#include "Decoders/DRCDecoder/DRCDecoder.h"
#include "Decoders/LHDecoder/LHDecoder.h"
#include "MemControl/LH-Cache/LH-Cache.h"
#include "include/NVMHelpers.h"
#include "NVM/nvmain.h"
#include <assert.h>
#include "src/EventQueue.h"
using namespace NVM;
//#define DBGATCACHE
ATcache::ATcache( )
{
    atCache = NULL;

    atCachePrefetch = 0;
    atCacheWrites = 0;
    atCacheHits = 0;
    atCacheMisses = 0;
    atCacheForceEvicts = 0;

    psInterval = 0;
}

ATcache::~ATcache( )
{
}

void ATcache::SetConfig( Config *conf, bool createChildren )
{
    /* Initialize DRAM Cache channels */
    if( conf->KeyExists( "DRC_CHANNELS" ) )
        numChannels = static_cast<ncounter_t>( conf->GetValue( "DRC_CHANNELS" ) );
    else
        numChannels = 1;

    /* ATcache Setup */
    uint64_t atSets, atAssoc;

    atSets = 512;
    if( conf->KeyExists( "ATcacheSets" ) ) 
        atSets = static_cast<uint64_t>( conf->GetValue( "ATcacheSets" ) );

    atAssoc = 4;
    if( conf->KeyExists( "ATcacheAssoc" ) ) 
        atAssoc = static_cast<uint64_t>( conf->GetValue( "ATcacheAssoc" ) );

    atCacheQueueSize = 32; //32
    if( conf->KeyExists( "ATcacheQueueSize" ) ) 
        atCacheQueueSize = static_cast<uint64_t>( conf->GetValue( "ATcacheQueueSize" ) );

    uint64_t atCacheLatency = 2;
    if( conf->KeyExists( "ATcacheLatency" ) ) 
        atCacheLatency = static_cast<uint64_t>( conf->GetValue( "ATcacheLatency" ) );

    if( createChildren )
    {
        /* Initialize off-chip memory */
        std::string configFile;
        Config *mainMemoryConfig;

        configFile  = NVM::GetFilePath( conf->GetFileName( ) );
        configFile += conf->GetString( "MM_CONFIG" );

        mainMemoryConfig = new Config( );
        mainMemoryConfig->Read( configFile );

        mainMemory = new NVMain( );
		mainMemory->SetParent( this );
        
		EventQueue *mainMemoryEventQueue = new EventQueue( );
        mainMemory->SetEventQueue( mainMemoryEventQueue );
		GetGlobalEventQueue( )->AddSystem( mainMemory, mainMemoryConfig );
		mainMemory->SetConfig( mainMemoryConfig, "offChipMemory", createChildren );

        std::vector<NVMObject_hook *>& childNodes = GetChildren( );

        childNodes.clear();
        std::string drcVariant = "LH_Cache";
        if( conf->KeyExists( "DRCVariant" ) ) 
            drcVariant = conf->GetString( "DRCVariant" );

        drcChannels = new LH_Cache*[numChannels];
        for( ncounter_t i = 0; i < numChannels; i++ )
        {
			//-------
		    /* Setup the translation method for DRAM cache decoders. */
            int channels, ranks, banks, rows, cols, subarrays,ignorebits;
            
            if( conf->KeyExists( "MATHeight" ) )
            {
                rows = conf->GetValue( "MATHeight" );
                subarrays = conf->GetValue( "ROWS" ) / conf->GetValue( "MATHeight" );
            }
            else
            {
                rows = conf->GetValue( "ROWS" );
                subarrays = 1;
            }

            cols = conf->GetValue( "COLS" );
            banks = conf->GetValue( "BANKS" );
            ranks = conf->GetValue( "RANKS" );
            channels = conf->GetValue( "DRC_CHANNELS" );
			ignorebits = conf->GetValue( "IgnoreBits" );
            TranslationMethod *drcMethod = new TranslationMethod();
            drcMethod->SetBitWidths( NVM::mlog2( rows ),
                                     NVM::mlog2( cols ),
                                     NVM::mlog2( banks ),
                                     NVM::mlog2( ranks ),
                                     NVM::mlog2( channels ),
                                     NVM::mlog2( subarrays )
                                     );
            drcMethod->SetCount( rows, cols, banks, ranks, channels, subarrays );
            drcMethod->SetAddressMappingScheme(conf->GetString( "AddressMappingScheme" ) );

            /* When selecting a child, use the channel field from a DRC decoder. */
            LHDecoder *drcDecoder = new LHDecoder( );
            drcDecoder->SetConfig( config, createChildren );
            drcDecoder->SetTranslationMethod( drcMethod );
            drcDecoder->SetDefaultField( CHANNEL_FIELD );
			drcDecoder->SetIgnoreBits(ignorebits);
            SetDecoder( drcDecoder );
					
            drcChannels[i] = dynamic_cast<LH_Cache *>( 
                    MemoryControllerFactory::CreateNewController( drcVariant ));

            drcChannels[i]->SetMainMemory( mainMemory );

            drcChannels[i]->SetID( static_cast<unsigned int>(i) );

            std::stringstream formatter;
            formatter.str( "" );
            formatter << StatName( ) << "." << conf->GetString( "DRCVariant" ) << i;
            drcChannels[i]->StatName( formatter.str() ); 

            drcChannels[i]->SetParent( this );
            AddChild( drcChannels[i] );

            drcChannels[i]->SetConfig( conf, createChildren );
			drcChannels[i]->RegisterStats( );
        }

        atCache = new CacheBank( atSets, atAssoc, 64 ); 
        atCache->isATcache = true;

        atCache->SetParent( this );
        AddChild( atCache );

        atCache->SetReadTime( atCacheLatency );
        atCache->SetWriteTime( atCacheLatency );
		AddChild( mainMemory );
		for(uint64_t i=0;i<atSets;i++)
		  for(uint64_t j=0;j<atAssoc;j++)
			 atCache->cacheEntry[i][j].data.tags=new uint64_t[29];
    }

    std::cout << "Created a ATcache!" << std::endl;
}

void ATcache::RegisterStats( )
{
    AddStat(atCachePrefetch);
    AddStat(atCacheWrites);
    AddStat(atCacheHits);
    AddStat(atCacheMisses);
    AddStat(atCacheForceEvicts);
	AddUnitStat(AT_hit_rate, "%");
}

bool ATcache::QueueFull( NVMainRequest * )
{
    return (atCacheQueue.size() >= atCacheQueueSize);
}

bool ATcache::IssueAtomic( NVMainRequest * )
{
    return true;
}

bool ATcache::IssueCommand( NVMainRequest *req )
{
    bool rv = false;
	//cout<<"ATissue:"<<req->address.GetPhysicalAddress( )<<endl;
    /* Make sure there is space in the ATcache's queue */
    if( atCacheQueue.size( ) < atCacheQueueSize )
    {
		Retranslate( req );
        NVMainRequest *atReq = new NVMainRequest( );
        CacheRequest *creq = new CacheRequest;

        *atReq = *req;
        atReq->tag = ATCACHE_READ;
        atReq->reqInfo = static_cast<void *>( creq );
        atReq->owner = this;

        creq->optype = CACHE_READ;
        creq->address = req->address; 

        creq->owner = this;
        creq->originalRequest = req;

        atCacheQueue.push( atReq );

#ifdef DBGATCACHE
        std::cout << "Enqueued a request to the ATcache. " << std::endl;
#endif

        rv = true;
    }

    return rv;
}

bool ATcache::RequestComplete( NVMainRequest *req )
{
    bool rv = false;

    if( req->owner == this )
    {
        if( req->tag == ATCACHE_READ )
        {
            CacheRequest *cacheReq = static_cast<CacheRequest *>( req->reqInfo );

#ifdef DBGATCACHE
            std::cout << "ATcache read complete. Hit = " 
                << cacheReq->hit << std::endl;
#endif

            /*  ATcache hit . */
            if( cacheReq->hit )
            {
                /* Issue to DRC only data access. */
                uint64_t chan;

                cacheReq->originalRequest->address.GetTranslatedAddress( 
                        NULL, NULL, NULL, NULL, &chan, NULL );
                assert( chan < numChannels );
				if(req->ATprefetch==true)
					cacheReq->originalRequest->ATprefetch = true;
				cacheReq->originalRequest->tag = ATCACHE_HIT;
				
				if(drcChannels[chan]->IsIssuable( cacheReq->originalRequest ))
					drcChannels[chan]->IssueCommand( cacheReq->originalRequest );
				else
					pendingDRCRequests.push(cacheReq->originalRequest );

                atCacheHits++;
            }
            /* ATcache miss. */
            else
            {
                /* 
                 *  Issue to DRC both tag and data access.
                 *  Writes go to DRC since they don't miss.
                 */
                uint64_t chan;

                cacheReq->originalRequest->address.GetTranslatedAddress( 
                        NULL, NULL, NULL, NULL, &chan, NULL );
				assert( chan < numChannels );
				cacheReq->originalRequest->tag = ATCACHE_MISS;
			
				if(drcChannels[chan]->IsIssuable( cacheReq->originalRequest ))
					drcChannels[chan]->IssueCommand( cacheReq->originalRequest );
				else
					pendingDRCRequests.push(cacheReq->originalRequest );
               
				atCacheMisses++;
            }
			delete cacheReq;
        }
        else if( req->tag == ATCACHE_WRITE )
        {
            /* Just delete the cache request struct. */
            CacheRequest *creq = static_cast<CacheRequest *>( req->reqInfo );
			if( creq->optype == CACHE_EVICT )
			  atCacheForceEvicts++;
#ifdef DBGATCACHE
            std::cout << "Wrote to the ATcache." << std::endl;
#endif
            delete creq;
        }
        delete req;
    }
	else
	{
	    /* 
	     *  We handle DRC and NVMain source requests. If the request is 
	     *  somewhere in the DRC hierarchy, send to the LH_Cache, otherwise
	     *  back to NVMain.
	     */
	    bool drcRequest = false;
	
	    for( ncounter_t i = 0; i < numChannels; i++ )
	    {
	        if( req->owner == drcChannels[i] )
	        {
	            drcRequest = true;
	            break;
	        }
	    }		
		if( req->tag == ATCACHE_HIT )
		{
			if(req->ATprefetch == true)
			{
				/* Install next four SetTags to ATcache. */
	            NVMainRequest *atFill = new NVMainRequest( );
	            CacheRequest *fillCReq = new CacheRequest( );

				for(int i=0;i<4;i++)
				  for(int j=0;j<29;j++)
				  {
					fillCReq->settags[i][j] = req->settags[i][j];
					//if(req->settags[i][j]!=0)
					//	cout<<"settag"<<i<<"="<<req->settags[i][j]<<endl;
				  }
	            fillCReq->optype = CACHE_WRITE;
	            fillCReq->address = req->address; 
	
	            fillCReq->owner = this;
	            fillCReq->originalRequest = NULL;
	
	            *atFill = *req;
	            atFill->owner = this;
	            atFill->reqInfo = static_cast<void *>( fillCReq );
	            atFill->tag = ATCACHE_WRITE;
	
	            atCachePrefetch++;
	            atCacheWrites++;
	
	            atCacheFillQueue.push( atFill );				
			}
			rv = GetParent( )->RequestComplete( req );
		}
		else if( req->tag == DRC_ACCESS ||req->tag == DRC_FILL )
		{
			/* Install SetTags to ATcache. */
            NVMainRequest *atFill = new NVMainRequest( );
            CacheRequest *fillCReq = new CacheRequest( );
            //fillCReq->data.settags = reinterpret_cast<uint8_t *>( req->data.settags );
			for(int i=0;i<4;i++)
			  for(int j=0;j<29;j++)
				fillCReq->settags[i][j] = req->settags[i][j];
            fillCReq->optype = CACHE_WRITE;
            fillCReq->address = req->address; 

            fillCReq->owner = this;
            fillCReq->originalRequest = NULL;

            *atFill = *req;
            atFill->owner = this;
            atFill->reqInfo = static_cast<void *>( fillCReq );
            atFill->tag = ATCACHE_WRITE;

            atCacheWrites++;
            //atCacheMisses++;

            atCacheFillQueue.push( atFill );
			if(!(req->tag == DRC_FILL) )
				rv = GetParent( )->RequestComplete( req );
#ifdef DBGATCACHE
                std::cout << "Adding new SetTags 0x" 
                    << fillCReq->address.GetPhysicalAddress( )
                    << std::dec << std::endl;
#endif
		}
		else if(drcRequest)
		{
	        uint64_t chan;
	
	        /* Retranslate incase the request was rerouted. */
	        Retranslate( req );
	        req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
	
	        rv = drcChannels[chan]->RequestComplete( req );
	
		}
		else 
		{
			rv = GetParent( )->RequestComplete( req );
		}

	}
    return rv;
}

void ATcache::Cycle( ncycle_t steps)
{
    /* Issue ATcache commands */
    if( atCache && (!atCacheQueue.empty( ) || !atCacheFillQueue.empty()) )
    {
        /* Give priority to write to install new atCache entires */
        if( !atCacheFillQueue.empty() )
        {
            if( atCache->IsIssuable( atCacheFillQueue.front(), NULL ) && IsIssu( atCacheFillQueue.front(), NULL ) )
            {
                atCache->IssueCommand( atCacheFillQueue.front() );
                atCacheFillQueue.pop( );

#ifdef DBGATCACHE
                std::cout << "Issued a fill to the ATcache." << std::endl;
#endif
            }
        }
        else
        {
            if( atCache->IsIssuable( atCacheQueue.front(), NULL ) && IsIssu( atCacheQueue.front(), NULL ) )
            {
                atCache->IssueCommand( atCacheQueue.front() );
                atCacheQueue.pop( );

#ifdef DBGATCACHE
                std::cout << "Issued a probe to the ATcache." << std::endl;
#endif
            }
        }
    }
	if( !pendingDRCRequests.empty() )
	{
		NVMainRequest *staleDRCReq = pendingDRCRequests.front();
        uint64_t chan;
        staleDRCReq->address.GetTranslatedAddress( 
                    NULL, NULL, NULL, NULL, &chan, NULL );
        assert( chan < numChannels );
		if(drcChannels[chan]->IsIssuable( staleDRCReq, NULL ))
		{
			drcChannels[chan]->IssueCommand( staleDRCReq );
			pendingDRCRequests.pop();
		}
    }

	uint64_t i;
    for( i = 0; i < numChannels; i++ )
    {
        drcChannels[i]->Cycle( steps );
    }
    mainMemory->Cycle( steps );
}

void ATcache::CalculateStats( )
{
    uint64_t i;
	AT_hit_rate = 0;
	uint64_t dramcachehit = 0;
    for( i = 0; i < numChannels; i++ )
    {
        drcChannels[i]->CalculateStats( );
		dramcachehit += drcChannels[i]->drcHits;
    }
	AT_hit_rate = static_cast<double>(atCacheHits)/static_cast<double>(dramcachehit);
	AT_hit_rate *=100;
    mainMemory->CalculateStats( );	
}

void ATcache::Retranslate( NVMainRequest *req )
{
    uint64_t col, row, bank, rank, chan, subarray;

    GetDecoder()->Translate( req->address.GetPhysicalAddress(), &row, &col, &bank, &rank, &chan, &subarray );
    req->address.SetTranslatedAddress( row, col, bank, rank, chan, subarray );
}

bool ATcache::IsIssuable( NVMainRequest * req, FailReason * /*fail*/ )
{
	if(QueueFull(req))
	  return false;
	return IsIssu(req,NULL);
}

bool ATcache::IsIssu( NVMainRequest * req, FailReason * /*fail*/ )
{
    uint64_t chan;
    Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );

    return drcChannels[chan]->IsIssuable( req );	
}
