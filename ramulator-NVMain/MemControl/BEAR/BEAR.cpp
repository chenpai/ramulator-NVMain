/******************************************************************************* 
* Author : chenpai 
*   6.24
*******************************************************************************/

#include "MemControl/BEAR/BEAR.h"
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

BEAR::BEAR( )
{

    std::cout << "Created a BEAR!" << std::endl;

    drcChannels = NULL;

    numChannels = 0;
	hit_rate = 0;
	DRCLatency = 0;
	predicttrue = 0;
	predictfalse = 0;
	BABMissCounter = 0;
	BABCounter = 0;
	PBMissCounter = 0;
	PBCounter = 0;
	runtimechange = 0;
	seed = 1;
}

BEAR::~BEAR( )
{
}

void BEAR::SetConfig( Config *conf, bool createChildren )
{
    /* Initialize DRAM Cache channels */
    numChannels = static_cast<ncounter_t>( conf->GetValue( "DRC_CHANNELS" ) );

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
			else if(conf->GetString( "DRCVariant" )=="CP_Cache")
			{
				LHDecoder *drcDecoder = new LHDecoder( );
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

            drcChannels[i] = dynamic_cast<LO_Cache *>(MemoryControllerFactory::CreateNewController(conf->GetString( "DRCVariant" ))) ;
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
        /* Add mainMemory as the last child */
        AddChild( mainMemory );
    }

    /* DRC Variant will call base SetConfig */
    //MemoryController::SetConfig( conf, createChildren );

    SetDebugName( "BEAR", conf );
}

void BEAR::RegisterStats( )
{
	AddUnitStat(hit_rate, "%");
	AddStat(DRCLatency);
}

void BEAR::Retranslate( NVMainRequest *req )
{
    uint64_t col, row, bank, rank, chan, subarray;

    GetDecoder()->Translate( req->address.GetPhysicalAddress(), &row, &col, &bank, &rank, &chan, &subarray );
    req->address.SetTranslatedAddress( row, col, bank, rank, chan, subarray );
}

bool BEAR::IssueAtomic( NVMainRequest *req )
{
    uint64_t chan;

    Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );

    return drcChannels[chan]->IssueAtomic( req );
}

bool BEAR::IsIssuable( NVMainRequest * req, FailReason * /*fail*/ )
{
    uint64_t chan;

    Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );

    return drcChannels[chan]->IsIssuable( req );
}

bool BEAR::IssueCommand( NVMainRequest *req )
{
    uint64_t rank, bank, chan;
	Retranslate( req );

    req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, &chan, NULL );

    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );
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
		runtimechange ++;
	}
	//predictor -- HMP_region
    /*if(HMP.find(req->address.GetPhysicalAddress()>>12)==HMP.end())
	  HMP.insert(pair<uint64_t,short>(req->address.GetPhysicalAddress()>>12, 1));
	if(HMP.find(req->address.GetPhysicalAddress()>>12)->second>1)
	    req->predict_hit = 1;
	else
		req->predict_hit = 0;*/
    return drcChannels[chan]->IssueCommand( req );
}

bool BEAR::IssueFunctional( NVMainRequest *req )
{
    uint64_t chan;

    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );

    assert( chan < numChannels );

    return drcChannels[chan]->IssueFunctional( req );
}

bool BEAR::RequestComplete( NVMainRequest *req )
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
				if(coinToss<0.9)
					req->bypass = 1;
				else
					req->bypass = 0;
			}
			else if(IsBABSampel(req))
			    req->bypass = 0;
			else
			{
				if(modebit)
				{
					double coinToss = static_cast<double>(::rand_r(&seed)) / static_cast<double>(RAND_MAX);
					if(coinToss<0.9)
						req->bypass = 1;
					else
						req->bypass = 0;
				}
				else
					req->bypass = 0;
			}
            rv = drcChannels[chan]->RequestComplete( req );
        }
        else
        {
			//predictor -- HMP_region
			/*
			if((req->dramcachehit==0&&req->predict_hit==0)||(req->dramcachehit==1&&req->predict_hit==1))
			  predicttrue++;
			if((req->dramcachehit==0&&req->predict_hit==1)||(req->dramcachehit==1&&req->predict_hit==0))
			  predictfalse++;
			if(req->dramcachehit==0&&HMP.find(req->address.GetPhysicalAddress()>>12)->second!=0)
			  HMP.find(req->address.GetPhysicalAddress()>>12)->second--;
			else if(HMP.find(req->address.GetPhysicalAddress()>>12)->second!=3)
			  HMP.find(req->address.GetPhysicalAddress()>>12)->second++;*/

            rv = GetParent( )->RequestComplete( req );
        }
    }

    return rv;
}

void BEAR::Cycle( ncycle_t steps )
{
    uint64_t i;

    for( i = 0; i < numChannels; i++ )
    {
        drcChannels[i]->Cycle( steps );
    }

    mainMemory->Cycle( steps );
}

void BEAR::CalculateStats( )
{
    uint64_t i;
	uint64_t dramcachehit = 0;
	uint64_t dramcachemiss = 0;

    for( i = 0; i < numChannels; i++ )
    {
        drcChannels[i]->CalculateStats( );
		dramcachehit += drcChannels[i]->hits;
		dramcachemiss += drcChannels[i]->misss;
		DRCLatency += drcChannels[i]->hits*(drcChannels[i]->HitLatency+
					drcChannels[i]->HitQueueLatency) + drcChannels[i]->misss*
			(drcChannels[i]->MissLatency+drcChannels[i]->MissQueueLatency);
    }
	hit_rate = static_cast<double>(dramcachehit)/static_cast<double>(dramcachehit+dramcachemiss);
	hit_rate *=100;
	DRCLatency /= (dramcachehit+dramcachemiss);
    mainMemory->CalculateStats( );
	//cout<<"predicttrue"<<predicttrue<<endl;
	//cout<<"predictfalse"<<predictfalse<<endl;
	cout<<"runtimechange:"<<runtimechange<<endl;
}

NVMain *BEAR::GetMainMemory( )
{
    return mainMemory;
}

bool BEAR::IsPBSampel(NVMainRequest *req)
{
	int setnum = (req->address.GetPhysicalAddress( ) >> 10)/*offset*/ % (2048*28);
	if(setnum%32 == 0)
	  return 1;
	else
	  return 0;
}

bool BEAR::IsBABSampel(NVMainRequest *req)
{
	int setnum = (req->address.GetPhysicalAddress( ) >> 10)/*offset*/ % (2048*28);
	if(setnum%32 == 2)
	  return 1;
	else
	  return 0;

}
