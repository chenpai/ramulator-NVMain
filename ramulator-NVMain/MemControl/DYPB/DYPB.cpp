/*******************************************************************************
* Author : chenpai 
*   2016.9.6
*******************************************************************************/

#include "MemControl/DYPB/DYPB.h"
#include "MemControl/MemoryControllerFactory.h"
#include "Interconnect/InterconnectFactory.h"
#include "include/NVMHelpers.h"
#include "NVM/nvmain.h"
#include "Decoders/DRCDecoder/DRCDecoder.h"
#include "Decoders/LHDecoder/LHDecoder.h"
#include "src/EventQueue.h"

#include <iostream>
#include <sstream>
#include <cassert>
#include <cstdlib>

using namespace NVM;

DYPB::DYPB( )
{
    //translator->GetTranslationMethod( )->SetOrder( 5, 1, 4, 3, 2, 6 );

    std::cout << "Created a DYPB!" << std::endl;

    drcChannels = NULL;
    numChannels = 0;
	hit_rate = 0;
	DRCLatency = 0;
	BABMissCounter = 0;
	BABCounter = 0;
	PBMissCounter = 0;
	PBCounter = 0;	
	seed = 1;
}

DYPB::~DYPB( )
{
}

void DYPB::SetConfig( Config *conf, bool createChildren )
{
    /* Initialize DRAM Cache channels */
    numChannels = static_cast<ncounter_t>( conf->GetValue( "DRC_CHANNELS" ) );
    /* Monitor Setup */
    uint64_t moSets, moAssoc,corenum;

    moSets = 256;
    if( conf->KeyExists( "MonitorSets" ) ) 
        moSets = static_cast<uint64_t>( conf->GetValue( "MonitorSets" ) );

    moAssoc = 16;
    if( conf->KeyExists( "MonitorAssoc" ) ) 
        moAssoc = static_cast<uint64_t>( conf->GetValue( "MonitorAssoc" ) );
	corenum = 8;
    if( conf->KeyExists( "corenum" ) ) 
        corenum = static_cast<uint64_t>( conf->GetValue( "corenum" ) );
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
        EventQueue *mainMemoryEventQueue = new EventQueue( );
        mainMemory->SetParent( this ); 
        mainMemory->SetEventQueue( mainMemoryEventQueue );
        GetGlobalEventQueue( )->AddSystem( mainMemory, mainMemoryConfig );
        mainMemory->SetConfig( mainMemoryConfig, "offChipMemory", createChildren );

        /* Orphan the interconnect created by NVMain */
        std::vector<NVMObject_hook *>& childNodes = GetChildren( );

        childNodes.clear();

        if( !conf->KeyExists( "DRCVariant" ) )
        {
            std::cout << "Error: No DRCVariant specified." << std::endl;
            exit(1);
        }

        drcChannels = new LO_Cache*[numChannels];
		wMonitor = new CacheBank*[corenum];
		rMonitor = new CacheBank*[corenum];
        for( ncounter_t i = 0; i < numChannels; i++ )
        {
            /* Setup the translation method for DRAM cache decoders. */
            int channels, ranks, banks, rows, cols, subarrays, ignorebits;
            
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
            
            /* When selecting a child, use the channel field from a DRC decoder. */
			if(conf->GetString( "DRCVariant" )=="LH_Cache")
			{
				DRCDecoder *drcDecoder = new DRCDecoder( );
				drcDecoder->SetConfig( config, createChildren );
				drcDecoder->SetTranslationMethod( drcMethod );
				drcDecoder->SetDefaultField( CHANNEL_FIELD );
				drcDecoder->SetIgnoreBits(ignorebits);
				SetDecoder( drcDecoder );
			}
			else if(conf->GetString( "DRCVariant" )=="LO_Cache"||conf->GetString( "DRCVariant" )=="fixway_Cache")
			{
				DRCDecoder *drcDecoder = new DRCDecoder( );
				drcDecoder->SetConfig( config, createChildren );
				drcDecoder->SetTranslationMethod( drcMethod );
				drcDecoder->SetDefaultField( CHANNEL_FIELD );
				drcDecoder->SetIgnoreBits(ignorebits);
				SetDecoder( drcDecoder );
			}
			else
			{
				assert(false);
			}
            /* Initialize a DRAM cache channel. */
            std::stringstream formatter;

            drcChannels[i] = dynamic_cast<LO_Cache *>( MemoryControllerFactory::CreateNewController(conf->GetString( "DRCVariant" )) );
            drcChannels[i]->SetMainMemory( mainMemory );

            formatter.str( "" );
            formatter << StatName( ) << "." << conf->GetString( "DRCVariant" ) << i;
            drcChannels[i]->SetID( static_cast<int>(i) );
            drcChannels[i]->StatName( formatter.str() ); 

            drcChannels[i]->SetParent( this );
            AddChild( drcChannels[i] );

            drcChannels[i]->SetConfig( conf, createChildren );
            drcChannels[i]->RegisterStats( );
        }
		Pw = new double[corenum];
		Pr = new double[corenum];
		wSumCounter = new uint64_t[corenum];
		wMissCounter= new uint64_t[corenum];
		wPos1Counter = new uint64_t[corenum];
		wPos2Counter = new uint64_t[corenum];
		wPos3Counter = new uint64_t[corenum];
		rSumCounter = new uint64_t[corenum];
		rMissCounter= new uint64_t[corenum];
		rPos1Counter = new uint64_t[corenum];
		rPos2Counter = new uint64_t[corenum];
		rPos3Counter = new uint64_t[corenum];
		monitor_evicts = new uint64_t[corenum];

		for( ncounter_t i = 0; i < corenum; i++ )
		{
			wMonitor[i] = new CacheBank( moSets, moAssoc, 64 );
			wMonitor[i]->isTemMonitor = true;
			rMonitor[i] = new CacheBank( moSets, moAssoc, 64 );
			rMonitor[i]->isTemMonitor = true;
			Pw[i]=1;
			Pr[i]=1;
			wSumCounter[i]=0;
			wMissCounter[i]=0;
			wPos1Counter[i]=0;
			wPos2Counter[i]=0;
			wPos3Counter[i]=0;
			rSumCounter[i]=0;
			rMissCounter[i]=0;
			rPos1Counter[i]=0;
			rPos2Counter[i]=0;
			rPos3Counter[i]=0;
		}
        /* Add mainMemory as the last child */
        AddChild( mainMemory );
    }

    /* DRC Variant will call base SetConfig */
    //MemoryController::SetConfig( conf, createChildren );
    SetDebugName( "DYPB", conf );
}

void DYPB::RegisterStats( )
{
	AddUnitStat(hit_rate, "%");
	AddStat(DRCLatency);
}

void DYPB::Retranslate( NVMainRequest *req )
{
    uint64_t col, row, bank, rank, chan, subarray;

    GetDecoder()->Translate( req->address.GetPhysicalAddress(), &row, &col, &bank, &rank, &chan, &subarray );
    req->address.SetTranslatedAddress( row, col, bank, rank, chan, subarray );
}

bool DYPB::IssueAtomic( NVMainRequest *req )
{
    uint64_t chan;

    Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );

    return drcChannels[chan]->IssueAtomic( req );
}

bool DYPB::IsIssuable( NVMainRequest * req, FailReason * /*fail*/ )
{
    uint64_t chan;

    Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );

    return drcChannels[chan]->IsIssuable( req );
}

bool DYPB::IssueCommand( NVMainRequest *req )
{
    uint64_t chan;
	Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    int coreid=req->coreid;
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );
	// dynamic select
	if(req->type==READ||req->type==WRITE)
	{
		if(IsPBSampel(req))
		{
			if(!IssueFunctional(req)&&req->type!=WRITE)
				PBMissCounter++;
			PBCounter++;
		}
		if(IsBABSampel(req))
		{
			if(!IssueFunctional(req)&&req->type!=WRITE)
				BABMissCounter++;
			BABCounter++;
		}
	}
	if(PBCounter>=16384||BABCounter>=16384)
	{
		if(((double)PBMissCounter/(double)PBCounter-(double)BABMissCounter/(double)BABCounter )<0.0625)
			modebit = 1;	//bypass fill
		else
		  modebit = 0;		//all fill
		cout<<"BABMissCounter"<<BABMissCounter<<endl;
		cout<<"BABCounter"<<BABCounter<<endl;
		cout<<"PBMissCounter"<<PBMissCounter<<endl;
		cout<<"PBCounter"<<PBCounter<<endl;
		cout<<"PB-BAB="<<(double)PBMissCounter/(double)PBCounter-(double)BABMissCounter/(double)BABCounter<<endl;
		cout<<"modebit="<<modebit<<endl;
		BABMissCounter/=2;
		BABCounter/=2;
		PBMissCounter/=2;
		PBCounter/=2;
	}

	if(((req->address.GetPhysicalAddress( ) >> 6/*offset*/)%7500)%30 == 0)
	{
		if(req->type==WRITE)
		{
		    NVMDataBlock dummy;
		    bool hit = wMonitor[coreid]->Present( req->address );
			if(hit)
			{
				int position = wMonitor[coreid]->MoRead( req->address,&dummy);
				if(position == 1)
				  wPos1Counter[coreid]++;
				else if(position ==2)
				  wPos2Counter[coreid]++;
				else
				  wPos3Counter[coreid]++;
			}
			else
			{
				if( wMonitor[coreid]->SetFull( req->address ) )
				{
					NVMAddress victim;
					(void)wMonitor[coreid]->ChooseVictim( req->address, &victim );
					(void)wMonitor[coreid]->Evict( victim, &dummy );
		
					monitor_evicts[coreid]++;
				}
				wMissCounter[coreid]++;
				(void)wMonitor[coreid]->Install( req->address, dummy );
			}
			wSumCounter[coreid]++;
		}
		else if(req->type == READ)
		{
		    NVMDataBlock dummy;
		    bool hit = rMonitor[coreid]->Present( req->address );
			if(hit)
			{
				int position = rMonitor[coreid]->MoRead( req->address,&dummy);
				if(position == 1)
				  rPos1Counter[coreid]++;
				else if(position == 2)
				  rPos2Counter[coreid]++;
				else
				  rPos3Counter[coreid]++;
			}
			else
			{
				if( rMonitor[coreid]->SetFull( req->address ) )
				{
					NVMAddress victim;
					(void)rMonitor[coreid]->ChooseVictim( req->address, &victim );
					(void)rMonitor[coreid]->Evict( victim, &dummy );
		
					monitor_evicts[coreid]++;
				}
				rMissCounter[coreid]++;
				(void)rMonitor[coreid]->Install( req->address, dummy );
			}
			rSumCounter[coreid]++;
		}
	}
	if(wSumCounter[coreid] == 2048)
	{
		//cout<<"sum0:"<<wSumCounter[0]<<" sum1:"<<wSumCounter[1]<<"sum2:"<<wSumCounter[2]<<" sum3:"<<wSumCounter[3];
		Pw[coreid] =((double)wPos1Counter[coreid]+(double)wPos2Counter[coreid]
					+(double)wPos3Counter[coreid])/wSumCounter[coreid];
		//Pw[coreid] = 1;
        wSumCounter[coreid]/=2;
		wPos1Counter[coreid]/=2;
		wPos2Counter[coreid]/=2;
		wPos3Counter[coreid]/=2;
		wMissCounter[coreid]/=2;
		//cout<<"wpos1"<<wPos1Counter[coreid]<<"wpos2"<<wPos2Counter[coreid]<<"wpos3"<<wPos3Counter[coreid]<<endl;
		cout<<"core:"<<coreid<<" Pw:"<<Pw[coreid]<<endl;
	}
	if(rSumCounter[coreid] == 2048)
	{
		//cout<<"sum0:"<<rSumCounter[0]<<" sum1:"<<rSumCounter[1]<<"sum2:"<<rSumCounter[2]<<" sum3:"<<rSumCounter[3];
		Pr[coreid] =((double)rPos1Counter[coreid]+(double)rPos2Counter[coreid]
					+(double)rPos3Counter[coreid])/rSumCounter[coreid];
		//Pr[coreid] = 1;
        rSumCounter[coreid]/=2;
		rPos1Counter[coreid]/=2;
		rPos2Counter[coreid]/=2;
		rPos3Counter[coreid]/=2;
		rMissCounter[coreid]/=2;
		//cout<<"rpos1"<<rPos1Counter[coreid]<<"rpos2"<<rPos2Counter[coreid]<<"rpos3"<<rPos3Counter[coreid]<<endl;
		cout<<"core:"<<coreid<<" Pr:"<<Pr[coreid]<<endl;
	}
	if(req->type ==WRITE)
	{
		if(IsPBSampel(req))
		{
			double coinToss = static_cast<double>(::rand_r(&seed)) / static_cast<double>(RAND_MAX);
			if(coinToss<Pw[coreid])
				req->bypass = 0;
			else
				req->bypass = 1;
		}
		else if(IsBABSampel(req))
		    req->bypass = 0;
		else
		{
			if(modebit)
			{
				double coinToss = static_cast<double>(::rand_r(&seed)) / static_cast<double>(RAND_MAX);
				if(coinToss<Pw[coreid])
					req->bypass = 0;
				else
					req->bypass = 1;
			}
			else
				req->bypass = 0;
		}
	}

	return drcChannels[chan]->IssueCommand( req );
}

bool DYPB::IssueFunctional( NVMainRequest *req )
{
    uint64_t chan;

    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );

    assert( chan < numChannels );

    return drcChannels[chan]->IssueFunctional( req );
}

bool DYPB::RequestComplete( NVMainRequest *req )
{
    bool rv = false;

    if( req->type == REFRESH )
        ProcessRefreshPulse( req );
    else if( req->owner == this )
    {
        delete req;
        rv = true;
    }
	else if(req->type == WRITE)
	{
        delete req;
        rv = true;		
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

        if( drcRequest )
        {
            uint64_t chan;

            /* Retranslate incase the request was rerouted. */
            Retranslate( req );
            req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
			
			if(IsPBSampel(req))
			{
				double coinToss = static_cast<double>(::rand_r(&seed)) / static_cast<double>(RAND_MAX);
				if(coinToss<Pr[req->coreid])
					req->bypass = 0;
				else
					req->bypass = 1;
			}
			else if(IsBABSampel(req))
			    req->bypass = 0;
			else
			{
				if(modebit)
				{
					double coinToss = static_cast<double>(::rand_r(&seed)) / static_cast<double>(RAND_MAX);
					if(coinToss<Pr[req->coreid])
						req->bypass = 0;
					else
						req->bypass = 1;
				}
				else
					req->bypass = 0;
			}
	        rv = drcChannels[chan]->RequestComplete( req );
	    }
	    else
	    {
	        rv = GetParent( )->RequestComplete( req );
	    }
    }

    return rv;
}

void DYPB::Cycle( ncycle_t steps )
{
    uint64_t i;

    for( i = 0; i < numChannels; i++ )
    {
        drcChannels[i]->Cycle( steps );
    }

    mainMemory->Cycle( steps );
}

void DYPB::CalculateStats( )
{
    uint64_t i;
	uint64_t dramcachehit = 0;
	uint64_t dramcachemiss = 0;

    for( i = 0; i < numChannels; i++ )
    {
        drcChannels[i]->CalculateStats( );
		dramcachehit += drcChannels[i]->hits;
		dramcachemiss += drcChannels[i]->misss;
		DRCLatency += drcChannels[i]->hits*(drcChannels[i]->HitLatency+drcChannels[i]->HitQueueLatency) +
			drcChannels[i]->misss*(drcChannels[i]->MissLatency+drcChannels[i]->MissQueueLatency);
    }
	hit_rate = static_cast<double>(dramcachehit)/static_cast<double>(dramcachehit+dramcachemiss);
	hit_rate *=100;
	DRCLatency /= (dramcachehit+dramcachemiss);
    mainMemory->CalculateStats( );

}

NVMain *DYPB::GetMainMemory( )
{
    return mainMemory;
}

bool DYPB::IsPBSampel(NVMainRequest *req)
{
	int setnum = (req->address.GetPhysicalAddress( ) >> 10)/*offset*/ % (2048*28);
	if(setnum%32 == 0)
	  return 1;
	else
	  return 0;
}

bool DYPB::IsBABSampel(NVMainRequest *req)
{
	int setnum = (req->address.GetPhysicalAddress( ) >> 10)/*offset*/ % (2048*28);
	if(setnum%32 == 2)
	  return 1;
	else
	  return 0;

}
