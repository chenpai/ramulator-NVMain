#include "Processor.h"
#include <cassert>

using namespace std;
using namespace NVM;

Processor::Processor(Config& configs,
    vector<const char*> trace_list,function<bool(NVMainRequest,bool&)> send_next,
    NVMObject_hook * get_Child)
    : ipcs(trace_list.size(), -1),
    early_exit(configs.is_early_exit()),
    no_core_caches(!configs.has_core_caches()),
    no_shared_cache(!configs.has_l3_cache()),
    cachesys(new CacheSystem(configs, send_next, get_Child)),
    llc(l3_size, l3_assoc, l3_blocksz,
         mshr_per_bank * trace_list.size(),
         Cache::Level::L3, cachesys) ,get_Child(get_Child){

  assert(cachesys != nullptr);
  int tracenum = trace_list.size();
  assert(tracenum > 0);
  printf("tracenum: %d\n", tracenum);
  for (int i = 0 ; i < tracenum ; ++i) {
    printf("trace_list[%d]: %s\n", i, trace_list[i]);
  }
  if (no_shared_cache) {                                        //若没有共享cache，也就是LLC，那么request直接发送给memory
    for (int i = 0 ; i < tracenum ; ++i) {
      cores.emplace_back(new Core(
          configs, i, trace_list[i], send_next, nullptr,
          cachesys, get_Child));
    }
  } else {														//若有共享cache，也就是LLC，那么request发送给LLC
    for (int i = 0 ; i < tracenum ; ++i) {
      cores.emplace_back(new Core(configs, i, trace_list[i],
          std::bind(&Cache::send, &llc, std::placeholders::_1,std::placeholders::_2),
          &llc, cachesys, get_Child));
    }
  }
  for (int i = 0 ; i < tracenum ; ++i) {
    cores[i]->callback = std::bind(&Processor::receive, this,
        placeholders::_1);
  }
  fini = new int[tracenum];
  for (int i(0); i<tracenum; i++) 
  {	
	  fini[i] = 2;
	  cores[i]->has_finish = fini;
	  cores[i]->corenum = tracenum;
  }
   /* regStats
  cpu_cycles.name("cpu_cycles")
            .desc("cpu cycle number")
            .precision(0)
            ;
  cpu_cycles = 0;*/
    //free_physical_pages_remaining = max_address >> 12;

    //free_physical_pages.resize(free_physical_pages_remaining, -1);
}

int Processor::tick() {
  int count=0;
  //cpu_cycles++;
  if (!(no_core_caches && no_shared_cache)) {
    count+=cachesys->tick();
  }
  for (unsigned int i = 0 ; i < cores.size() ; ++i) {
    Core* core = cores[i].get();
    count+=core->tick();
  }
  return count;
}

void Processor::receive(NVMainRequest& req) {
  if (!no_shared_cache) {
    llc.callback(req);
  } else if (!cores[0]->no_core_caches) {
    // Assume all cores have caches or don't have caches
    // at the same time.
    for (unsigned int i = 0 ; i < cores.size() ; ++i) {
      Core* core = cores[i].get();
      core->caches[0]->callback(req);
    }
  }
  for (unsigned int i = 0 ; i < cores.size() ; ++i) {
    Core* core = cores[i].get();
    core->receive(req);
  }
}

bool Processor::finished() {

  if (early_exit) {
    for (unsigned int i = 0 ; i < cores.size(); ++i) {
      if (cores[i]->finished()) {
        for (unsigned int j = 0 ; j < cores.size() ; ++j) {
		  ipcs[j] = cores[j]->calc_ipc();
          ipc += ipcs[j];
        }
        return true;
      }
    }
    return false;
  } else {
    for (unsigned int i = 0 ; i < cores.size(); ++i) {
      if (!cores[i]->finished()) {
        return false;
      }
      if (ipcs[i] < 0) {
        ipcs[i] = cores[i]->calc_ipc();
        ipc += ipcs[i];
      }
    }
    return true;
  }
}

bool Processor::has_reached_limit() {
  for (unsigned int i = 0 ; i < cores.size() ; ++i) {
    if (!cores[i]->has_reached_limit()) {
      return false;
    }
  }
  return true;
}

Core::Core( Config& configs, int coreid,
    const char* trace_fname, function<bool(NVMainRequest,bool&)> send_next,
    Cache* llc, std::shared_ptr<CacheSystem> cachesys, NVMObject_hook * get_Child)
    : id(coreid), no_core_caches(!configs.has_core_caches()),
    no_shared_cache(!configs.has_l3_cache()),
    llc(llc), trace_fname(trace_fname), getChild(get_Child)
{
  // Build cache hierarchy
  if (no_core_caches) {
    send = send_next;
  } else {
    // L2 caches[0]
    caches.emplace_back(new Cache(
        l2_size, l2_assoc, l2_blocksz, l2_mshr_num,
        Cache::Level::L2, cachesys));
    // L1 caches[1]
    caches.emplace_back(new Cache(
        l1_size, l1_assoc, l1_blocksz, l1_mshr_num,
        Cache::Level::L1, cachesys));
    send = bind(&Cache::send, caches[1].get(), std::placeholders::_1,std::placeholders::_2);
    if (llc != nullptr) {
      caches[0]->concatlower(llc);
    }
    caches[1]->concatlower(caches[0].get());

  }
  tl=new TraceLine();
//--------------------------------------------
  // GenericTraceReader *trace = NULL;
  Config configg(configs);
  if( configg.KeyExists("TraceReader"))
       this->trace = TraceReaderFactory::CreateNewTraceReader( 
                configg.GetString( "TraceReader" ) );
    else
        this->trace = TraceReaderFactory::CreateNewTraceReader( "NVMainTrace" );

    this->trace->SetTraceFile( trace_fname );
//--------------------------------------------
  
  if (no_core_caches) {
    more_reqs = trace->GetNextAccess( tl,coreid,has_finish,corenum );
    //tl->GetAddress( ).SetPhysicalAddress(Processor::page_allocator(tl->GetAddress( ).GetPhysicalAddress( ), coreid));          
  } else {
    more_reqs = trace->GetNextAccess( tl,coreid,has_finish,corenum );
    //tl->GetAddress( ).SetPhysicalAddress(Processor::page_allocator(tl->GetAddress( ).GetPhysicalAddress( ), coreid));   
  }

  // set expected limit instruction for calculating weighted speedup
  expected_limit_insts = configs.get_expected_limit_insts();

  /* regStats
  record_cycs.name("record_cycs_core_" + to_string(id))
             .desc("Record cycle number for calculating weighted speedup. (Only valid when expected limit instruction number is non zero in config file.)")
             .precision(0)
             ;

  record_insts.name("record_insts_core_" + to_string(id))
              .desc("Retired instruction number when record cycle number. (Only valid when expected limit instruction number is non zero in config file.)")
              .precision(0)
              ;

  memory_access_cycles.name("memory_access_cycles_core_" + to_string(id))
                      .desc("memory access cycles in memory time domain")
                      .precision(0)
                      ;
  memory_access_cycles = 0;
  cpu_inst.name("cpu_instructions_core_" + to_string(id))
          .desc("cpu instruction number")
          .precision(0)
          ;
  cpu_inst = 0;*/
}


double Core::calc_ipc()
{
    printf("[%d]retired: %ld, clk, %ld\n", id, retired, clk);
    return (double) retired / clk;
}

bool Core::tick()
{
    clk++;

    retired += window.retire();

    if (expected_limit_insts == 0 && !more_reqs) return 0;

    // bubbles (non-memory operations)
    int inserted = 0;
    while (tl->GetBubble_cnt( ) > 0) {
        if (inserted == window.ipc) return 0;
        if (window.is_full()) return 0;

        window.insert(true, -1);
        inserted++;
        tl->bubble_cnt--;
        cpu_inst++;
        /*if (long(cpu_inst.value()) == expected_limit_insts && !reached_limit) {
          record_cycs = clk;
          record_insts = long(cpu_inst.value());
          memory.record_core(id);                    //忽略
          reached_limit = true;
        }*/
    }

//	NVMainRequest *req = new NVMainRequest( );      
//    req->address = tl->GetAddress( );
//    req->type = tl->GetOperation( );
//    req->bulkCmd = CMD_NOP;
//    req->threadId = tl->GetThreadId( );
//    req->data = tl->GetData( );
//    req->oldData = tl->GetOldData( );
//    req->status = MEM_REQUEST_INCOMPLETE;
//    req->owner = (NVMObject *)(getChild->GetTrampoline( )->GetParent( )->GetTrampoline( ));
//	req->callback = callback;
//	req->coreid = id;
    bool MSHR=false; 
	
    if (tl->GetOperation( ) == READ) {
        if (inserted == window.ipc) { return 0;}
        if (window.is_full())  { return 0;}

		NVMainRequest *req = new NVMainRequest( );      
	    req->address = tl->GetAddress( );
		req->type = tl->GetOperation( );
	    req->bulkCmd = CMD_NOP;
	    req->status = MEM_REQUEST_INCOMPLETE;
	    req->owner = (NVMObject *)(getChild->GetTrampoline( )->GetParent( )->GetTrampoline( ));
		req->callback = callback;
		req->coreid = id;

	    if (no_core_caches && no_shared_cache )	
		{
            if (!getChild->IsIssuable( req ))  { delete req;return 0;}                   
            getChild->IssueCommand( req );
		    window.insert(false, req->address.GetPhysicalAddress());
		}
		else
		{
	    	if (!send(*req,MSHR))  { delete req;return 0;}
			window.insert(false, req->address.GetPhysicalAddress());
		    delete req;
		}
    }
    else {
        assert(tl->GetOperation( ) == WRITE);
		
		NVMainRequest *req = new NVMainRequest( );      
	    req->address = tl->GetAddress( );
		req->type = tl->GetOperation( );
	    req->bulkCmd = CMD_NOP;
	    req->status = MEM_REQUEST_INCOMPLETE;
	    req->owner = (NVMObject *)(getChild->GetTrampoline( )->GetParent( )->GetTrampoline( ));
		req->callback = callback;
		req->coreid = id;

		if (no_core_caches&&no_shared_cache)
		{
			if (!getChild->IsIssuable( req ))  { delete req;return 0;}       //传入的send是memory.h的send()
			getChild->IssueCommand( req );
		}
		else
		{
			if (!send(*req,MSHR))  { delete req;return 0;}
			delete req;
		}
    }
    /*if (long(cpu_inst.value()) == expected_limit_insts && !reached_limit) {
      record_cycs = clk;
      record_insts = long(cpu_inst.value());
      memory.record_core(id);           					//忽略
      reached_limit = true;
    }*/

    if (no_core_caches) {
      more_reqs = trace->GetNextAccess( tl,id,has_finish,corenum );
      //if (more_reqs) {
      //      tl->GetAddress( ).SetPhysicalAddress(Processor::page_allocator(tl->GetAddress( ).GetPhysicalAddress( ), id));   
      //}
    } else {
      more_reqs = trace->GetNextAccess( tl,id ,has_finish,corenum);
      //if (more_reqs) {
      //      tl->GetAddress( ).SetPhysicalAddress(Processor::page_allocator(tl->GetAddress( ).GetPhysicalAddress( ), id));   
      //}
    }
    if (!more_reqs) {
      if (!reached_limit) { // if the length of this trace is shorter than expected length, then record it when the whole trace finishes, and set reached_limit to true.
        //record_cycs = clk;
        //record_insts = long(cpu_inst.value());
        //memory.record_core(id);								//忽略
        reached_limit = true;
      }
    }
	if(MSHR)
	  return 0;
	if (no_core_caches && no_shared_cache)
	  return 1;
	return 0;
}

bool Core::finished()
{
    return !more_reqs && window.is_empty();
}

bool Core::has_reached_limit() {
  return reached_limit;
}

void Core::receive(NVMainRequest& req)
{
    window.set_ready(req.address.GetPhysicalAddress(), ~(l1_blocksz - 1l));
    /*if (req.arrive != -1 && req.depart > last) {
      memory_access_cycles += (req.depart - max(last, req.arrive));
      last = req.depart;
    }*/
}

bool Window::is_full()
{
    return load == depth;
}

bool Window::is_empty()
{
    return load == 0;
}


void Window::insert(bool ready, long addr)
{
    assert(load <= depth);

    ready_list.at(head) = ready;
    addr_list.at(head) = addr;

    head = (head + 1) % depth;
    load++;
}


long Window::retire()
{
    assert(load <= depth);

    if (load == 0) return 0;

    int retired = 0;
    while (load > 0 && retired < ipc) {
        if (!ready_list.at(tail))
            break;

        tail = (tail + 1) % depth;
        load--;
        retired++;
    }

    return retired;
}


void Window::set_ready(long addr, int mask)
{
    if (load == 0) return;

    for (int i = 0; i < load; i++) {
        int index = (tail + i) % depth;
        if ((addr_list.at(index) & mask) != (addr & mask))
            continue;
        ready_list.at(index) = true;
    }
}



