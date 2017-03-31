/******************************************************************************* 
* Author : chenpai 
*     2016.6.28   
********************************************************************************/

#include "MemControl/VictimTag/VictimTag.h"
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

VictimTag::VictimTag( )
{
    //translator->GetTranslationMethod( )->SetOrder( 5, 1, 4, 3, 2, 6 );

    std::cout << "Created a VictimTag!" << std::endl;

    drcChannels = NULL;
	vicTaghits = 0;
    numChannels = 0;
	hit_rate = 0;
	DRCLatency = 0;
}

VictimTag::~VictimTag( )
{
}

void VictimTag::SetConfig( Config *conf, bool createChildren )
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
        /* Add mainMemory as the last child */
        AddChild( mainMemory );
    }

    /* DRC Variant will call base SetConfig */
    //MemoryController::SetConfig( conf, createChildren );

    SetDebugName( "VictimTag", conf );
}

void VictimTag::RegisterStats( )
{
	AddStat(vicTaghits);
	AddUnitStat(hit_rate, "%");
	AddStat(DRCLatency);
}

void VictimTag::Retranslate( NVMainRequest *req )
{
    uint64_t col, row, bank, rank, chan, subarray;

    GetDecoder()->Translate( req->address.GetPhysicalAddress(), &row, &col, &bank, &rank, &chan, &subarray );
    req->address.SetTranslatedAddress( row, col, bank, rank, chan, subarray );
}

bool VictimTag::IssueAtomic( NVMainRequest *req )
{
    uint64_t chan;

    Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );

    return drcChannels[chan]->IssueAtomic( req );
}

bool VictimTag::IsIssuable( NVMainRequest * req, FailReason * /*fail*/ )
{
    uint64_t chan;

    Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );

    return drcChannels[chan]->IsIssuable( req );
}

bool VictimTag::IssueCommand( NVMainRequest *req )
{
    uint64_t rank, bank, chan;
	Retranslate( req );
    req->address.GetTranslatedAddress( NULL, NULL, &bank, &rank, &chan, NULL );
	int coreid = req->coreid;
    assert( chan < numChannels );
    assert( GetChild(req)->GetTrampoline() == drcChannels[chan] );
	if(req->type == FILLDRC && req->L3fillDRC == 1)
	{
		req->type = READ;
		//vector<uint64_t>::iterator it=
		//	find(vic[coreid].begin(),vic[coreid].end(),req->address.GetPhysicalAddress( ));
		//if(it!=vic[coreid].end())
		//{
			//vic[coreid].erase(it);
		//}
	}
	//else if(req->type == WRITE && req->L3fillDRC == 1)
	//{
	//	vector<uint64_t>::iterator it=
	//		find(vic[coreid].begin(),vic[coreid].end(),req->address.GetPhysicalAddress( ));
	//	if(it!=vic[coreid].end())
	//	{
			//vic[coreid].erase(it);
	//	}
	//}
	else if(req->type == FILLDRC && req->L3fillDRC == 0)
	{
		if(!drcChannels[chan]->IssueFunctional(req))
		{
			vector<uint64_t>::iterator it=
				find(vic[coreid].begin(),vic[coreid].end(),req->address.GetPhysicalAddress( ));
			if(it!=vic[coreid].end())
			{
				req->type = READ;
				req->L3fillDRC = 1;
				vic[coreid].erase(it);
				vic[coreid].push_back(req->address.GetPhysicalAddress( ));
				vicTaghits++;
			}
			else
			{
				if(vic[coreid].size()>=40000)
				{
					vic[coreid].erase(vic[coreid].begin());
				}
				vic[coreid].push_back(req->address.GetPhysicalAddress( ));
			}
		}
	}
	else if(req->type == WRITE && req->L3fillDRC == 0)
	{
		if(!drcChannels[chan]->IssueFunctional( req))
		{
			vector<uint64_t>::iterator it=
				find(vic[coreid].begin(),vic[coreid].end(),req->address.GetPhysicalAddress( ));
			if(it!=vic[coreid].end())
			{
				req->L3fillDRC = 1;
				vic[coreid].erase(it);
				vic[coreid].push_back(req->address.GetPhysicalAddress( ));
				vicTaghits++;
			}
			else
			{
				if(vic[coreid].size()>=40000)
				{
					vic[coreid].erase(vic[coreid].begin());
				}
				vic[coreid].push_back(req->address.GetPhysicalAddress( ));
			}
		}
	}  
    return drcChannels[chan]->IssueCommand( req );
}

bool VictimTag::IssueFunctional( NVMainRequest *req )
{
    uint64_t chan;

    req->address.GetTranslatedAddress( NULL, NULL, NULL, NULL, &chan, NULL );

    assert( chan < numChannels );

    return drcChannels[chan]->IssueFunctional( req );
}

bool VictimTag::RequestComplete( NVMainRequest *req )
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

            rv = drcChannels[chan]->RequestComplete( req );
        }
        else
        {
            rv = GetParent( )->RequestComplete( req );
        }
    }

    return rv;
}

void VictimTag::Cycle( ncycle_t steps )
{
    uint64_t i;

    for( i = 0; i < numChannels; i++ )
    {
        drcChannels[i]->Cycle( steps );
    }

    mainMemory->Cycle( steps );
}

void VictimTag::CalculateStats( )
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

NVMain *VictimTag::GetMainMemory( )
{
    return mainMemory;
}
