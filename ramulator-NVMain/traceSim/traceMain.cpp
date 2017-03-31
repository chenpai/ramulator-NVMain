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

#include <sstream>
#include <cmath>
#include <stdlib.h>
#include <fstream>

#include "src/Interconnect.h"
#include "Interconnect/InterconnectFactory.h"
#include "src/Config.h"
#include "src/TranslationMethod.h"
#include "traceReader/TraceReaderFactory.h"
#include "src/AddressTranslator.h"
#include "Decoders/DecoderFactory.h"
#include "src/MemoryController.h"
#include "MemControl/MemoryControllerFactory.h"
#include "Endurance/EnduranceDistributionFactory.h"
#include "SimInterface/NullInterface/NullInterface.h"
#include "include/NVMHelpers.h"
#include "Utils/HookFactory.h"
#include "src/EventQueue.h"
#include "NVM/nvmain.h"
#include "traceSim/traceMain.h"
#include "src/Processor.h"

using namespace NVM;

int main( int argc, char *argv[] )
{
    TraceMain *traceRunner = new TraceMain( );

    return traceRunner->RunTrace( argc, argv );
}

TraceMain::TraceMain( )
{
	cpu_cycles = 0;
}

TraceMain::~TraceMain( )
{

}

int TraceMain::RunTrace( int argc, char *argv[] )
{
    Stats *stats = new Stats( );
    Config *config = new Config( );
    //GenericTraceReader *trace = NULL;
    //TraceLine *tl = new TraceLine( );
    SimInterface *simInterface = new NullInterface( );
    NVMain *nvmain = new NVMain( );
    EventQueue *mainEventQueue = new EventQueue( );
    GlobalEventQueue *globalEventQueue = new GlobalEventQueue( );
    TagGenerator *tagGenerator = new TagGenerator( 1000 );
    //bool IgnoreData = false;
    bool EventDriven = false;
    //static uint64_t currentCycle;
    
    if( argc < 3 )
    {
        std::cout << "Usage: nvmain CONFIG_FILE TRACE_FILE1 TRACE_FILE2 ... " 
            << std::endl;
        return 1;
    }

    /* Print out the command line that was provided. */
    std::cout << "NVMain command line is:" << std::endl;
    for( int curArg = 0; curArg < argc; ++curArg )
    {
        std::cout << argv[curArg] << " ";
    }
    std::cout << std::endl << std::endl;

    config->Read( argv[1] );
    config->SetSimInterface( simInterface );
    SetEventQueue( mainEventQueue );
    SetGlobalEventQueue( globalEventQueue );
    SetStats( stats );
    SetTagGenerator( tagGenerator );
    std::ofstream statStream;

    if( config->KeyExists( "StatsFile" ) )
    {
        statStream.open( config->GetString( "StatsFile" ).c_str() , 
                         std::ofstream::out | std::ofstream::app );
    }

    config->GetBool( "EventDriven", EventDriven );

    /*  Add any specified hooks */
    std::vector<std::string>& hookList = config->GetHooks( );

    for( size_t i = 0; i < hookList.size( ); i++ )
    {
        std::cout << "Creating hook " << hookList[i] << std::endl;

        NVMObject *hook = HookFactory::CreateHook( hookList[i] );
        
        if( hook != NULL )
        {
            AddHook( hook );
            hook->SetParent( this );
            hook->Init( config );
        }
        else
        {
            std::cout << "Warning: Could not create a hook named `" 
                << hookList[i] << "'." << std::endl;
        }
    }

    AddChild( nvmain );
    nvmain->SetParent( this );

    globalEventQueue->SetFrequency( config->GetEnergy( "CPUFreq" ) * 1000000.0 );
    globalEventQueue->AddSystem( nvmain, config );

    simInterface->SetConfig( config, true );
    nvmain->SetConfig( config, "defaultMemory", true );

    std::cout << "traceMain (" << (void*)(this) << ")" << std::endl;
    nvmain->PrintHierarchy( );

    /*if( config->KeyExists( "TraceReader" ) )
        trace = TraceReaderFactory::CreateNewTraceReader( 
                config->GetString( "TraceReader" ) );
    else
        trace = TraceReaderFactory::CreateNewTraceReader( "NVMainTrace" );

    trace->SetTraceFile( argv[2] );*/


    int trace_start = 2;
    std::vector<const char*> files(&argv[trace_start], &argv[argc]);
    config->set_core_num(argc - trace_start);
    static uint64_t currentCycle = 0;
    int fanhui;
	//int cpu_tick = 4;
	//int mem_tick = 1;
	NVMObject_hook * getChild = GetChild( );
	function<bool(NVMainRequest,bool)> send_next=NULL;
	Processor proc(*config, files, send_next, getChild);
	Core* core;
	bool drain=false,nomoretrace=false;
    for (long i = 0; ; i++) {
		cpu_cycles++;
        fanhui= proc.tick( );
	    outstandingRequests=outstandingRequests+fanhui;
        //if (i % cpu_tick == (cpu_tick - 1))      
		  //   for (int j = 0; j < mem_tick; j++)
			// {
                 uint64_t no_more_trace=0;
				 if(!nomoretrace)
				 for (unsigned int k = 0 ; k < proc.cores.size() ; ++k) 
				 {
					 core = proc.cores[k].get();
					 if(core->reached_limit==true)
				        no_more_trace++;
				 }
				 if(no_more_trace==proc.cores.size()&&drain==false)
				 {
					 drain=Drain( );
					 nomoretrace=true;
				 }
                 memory_tick(currentCycle,EventDriven );
			//}
         //if (proc.finished() && outstandingRequests == 0)
		 if (proc.finished())
            break;
	}
	std::ostream& refStream = (statStream.is_open()) ? statStream : std::cout;
	for ( int k = 2 ; k < argc ; ++k)
		refStream << argv[k];

		 if(!proc.no_shared_cache)
		 {
			refStream << endl<<"L3_access=" << proc.llc.cache_total_access<<endl;
			refStream << "L3_miss=" << proc.llc.cache_total_miss<<endl;
			refStream << "L3_hit_rate=" <<(double)(proc.llc.cache_total_access-proc.llc.cache_total_miss)/(double)proc.llc.cache_total_access<<endl;
		 }

		if(!proc.no_core_caches)
		for (unsigned int k = 0 ; k < proc.cores.size() ; ++k) 
		{
			core = proc.cores[k].get();
			std::cout << "L2_access=" << core->caches[0]->cache_total_access<<endl;
			std::cout << "L2_miss=" << core->caches[0]->cache_total_miss<<endl;
			std::cout << "L2_hit_rate=" <<(double)(core->caches[0]->cache_total_access-core->caches[0]->cache_total_miss)/(double)core->caches[0]->cache_total_access<<endl;
			std::cout << "L1_access=" << core->caches[1]->cache_total_access<<endl;
			std::cout << "L1_miss=" << core->caches[1]->cache_total_miss<<endl;
			std::cout << "L1_hit_rate=" <<(double)(core->caches[1]->cache_total_access-core->caches[1]->cache_total_miss)/(double)core->caches[1]->cache_total_access<<endl;
			
		}
		//core = proc.cores[0].get();
		//cout<<"CPU TICK:"<<core->cpu_inst<<endl;
	for (unsigned int k = 0 ; k < proc.cores.size() ; ++k) 
	{
		cout<<proc.ipcs[k]<<","<<endl;
	}
	IPC = proc.ipc;
   	RegisterStats( );
    GetChild( )->CalculateStats( );
    //std::ostream& refStream = (statStream.is_open()) ? statStream : std::cout;
    stats->PrintAll( refStream );
	for (unsigned int k = 0 ; k < proc.cores.size() ; ++k) 
	{
		refStream <<proc.ipcs[k]<<endl;
	}
    delete config;
    delete stats;

    return 0;
}

void TraceMain::RegisterStats( )
{
	AddStat(IPC);
	AddStat(cpu_cycles);
}

void TraceMain::Cycle( ncycle_t /*steps*/ )
{

}

bool TraceMain::RequestComplete( NVMainRequest* request )
{
    /* This is the top-level module, so there are no more parents to fallback. */
    assert( request->owner == this);

    //outstandingRequests--;
	if(request->type != WRITE && request->type != WRITE_PRECHARGE) 
    request->callback(*request);
    delete request;

    return true;
}

void TraceMain::memory_tick(uint64_t & currentCycle ,bool EventDriven )
{
        /*
        if(0)
        {
            // Force all modules to drain requests. 
            bool draining = Drain( );

            std::cout << "Could not read next line from trace file!" 
                << std::endl;

            // Wait for requests to drain. 
            while( outstandingRequests > 0 )
            {
                if( EventDriven )
                    globalEventQueue->Cycle( 1 );
                else 
                    GetChild( )->Cycle( 1 );
              
                currentCycle++;

                // Retry drain each cycle if it failed. 
                if( !draining )
                    draining = Drain( );
            }

            break;
        }

        
      
        //If you want to ignore the cycles used in the trace file, just set
        // the cycle to 0. 
         
        if( config->KeyExists( "IgnoreTraceCycle" ) 
                && config->GetString( "IgnoreTraceCycle" ) == "true" )
            tl->SetLine(tl->GetBubble_cnt( ), tl->GetAddress( ), tl->GetOperation( ), 0, 
                         tl->GetData( ), tl->GetOldData( ), tl->GetThreadId( ) );
         
        
         // If the next operation occurs after the requested number of cycles,
         //we can quit. 
         
        if( tl->GetCycle( ) > simulateCycles && simulateCycles != 0 )
        {
            if( EventDriven )
            {
                globalEventQueue->Cycle( simulateCycles - currentCycle );
                currentCycle += simulateCycles - currentCycle;

                break;
            }

            //Just ride it out 'til the end. 
            while( currentCycle < simulateCycles )
            {
                GetChild( )->Cycle( 1 );
              
                currentCycle++;
            }

            break;
        }
        else
        {
             
             //  If the command is in the past, it can be issued. This would 
             // occur since the trace was probably generated with an inaccurate 
             //  memory *  simulator, so the cycles may not match up. Otherwise, 
             //  we need to wait.
             
            if( tl->GetCycle( ) > currentCycle )
            {
                if( EventDriven )
                {
                    globalEventQueue->Cycle( tl->GetCycle() - currentCycle );
                    currentCycle = globalEventQueue->GetCurrentCycle( );
                }
                else
                {
                    // Wait until currentCycle is the trace operation's cycle. 
                    while( currentCycle < tl->GetCycle( ) )
                    {
                        if( currentCycle >= simulateCycles && simulateCycles != 0 )
                            break;

                        GetChild( )->Cycle( 1 );

                        currentCycle++;
                    }
                }

                if( currentCycle >= simulateCycles && simulateCycles != 0 )
                    break;
            }
            */
            /* 
             *  Wait for the memory controller to accept the next command.. 
             *  the trace reader is "stalling" until then.
             */
			
            if( 1 )
            {
				
                //if( currentCycle >= simulateCycles && simulateCycles != 0 )
                  //  break;
                
                if( EventDriven )
                {
                    globalEventQueue->Cycle( 1 );
                    currentCycle = globalEventQueue->GetCurrentCycle( );
                }
                else 
                {
                    GetChild( )->Cycle( 1 );   //the key of memory process
                    currentCycle++;
                }
            }
}


