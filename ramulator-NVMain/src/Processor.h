#ifndef __PROCESSOR_H
#define __PROCESSOR_H

#include "include/NVMainRequest.h"
#include "include/NVMAddress.h"
#include "include/NVMTypes.h"
#include "traceReader/TraceReaderFactory.h"
#include "traceReader/TraceLine.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <ctype.h>
#include <functional>
#include "src/NVMObject.h"
#include "src/Cache.h"
#include "src/Config.h"
//#include "Statistics.h"

namespace NVM 
{ 
class Window {
public:
    int ipc = 4;
    int depth = 128;

    Window() : ready_list(depth), addr_list(depth, -1) {}
    bool is_full();
    bool is_empty();
    void insert(bool ready, long addr);
    long retire();
    void set_ready(long addr , int mask);

private:
    int load = 0;
    int head = 0;
    int tail = 0;
    std::vector<bool> ready_list;
    std::vector<long> addr_list;
};

class Core {
public:
    long clk = 0;
    long retired = 0;
    int id = -1;
    function<bool(NVMainRequest,bool&)> send;

    Core(Config& configs, int coreid,
        const char* trace_fname,
        function<bool(NVMainRequest,bool&)> send_next, Cache* llc,
        std::shared_ptr<CacheSystem> cachesys, NVMObject_hook * get_Child);
    bool tick();
    void receive(NVMainRequest & req);
    double calc_ipc();
    bool finished();
    bool has_reached_limit();
    function<void(NVMainRequest &)> callback;

    bool no_core_caches = false;
    bool no_shared_cache = false;
    int l1_size = 1 << 15;
    int l1_assoc = 1 << 3;
    int l1_blocksz = 1 << 6;
    int l1_mshr_num = 16;

    int l2_size = 1 << 18;    //18
    int l2_assoc = 1 << 3;
    int l2_blocksz = 1 << 6;
    int l2_mshr_num = 16;
    std::vector<std::shared_ptr<Cache>> caches;
    Cache* llc;
	
    //ScalarStat record_cycs;
    //ScalarStat record_insts;
    long expected_limit_insts;
    // This is set true iff expected number of instructions has been executed or all instructions are executed.
    bool reached_limit = false;
	uint64_t cpu_inst=0;
	int * has_finish;
	bool more_reqs;
	GenericTraceReader * trace;
	int corenum;
private:
    Window window;
   
    long req_addr = -1;   
    long last = 0;
	
    //ScalarStat memory_access_cycles;
	
	//GenericTraceReader * trace;
	const char* trace_fname;
    NVMObject_hook * getChild;
    TraceLine * tl;
};

class Processor {
public:
    Processor(Config& configs, vector<const char*> trace_list,
    function<bool(NVMainRequest,bool&)> send_next,NVMObject_hook * get_Child);
	//Processor(GenericTraceReader * trace_fname, NVMObject_hook * get_Child);  
    int tick();
    void receive(NVMainRequest& req);
    bool finished();
    bool has_reached_limit();

    std::vector<std::unique_ptr<Core>> cores;
    std::vector<double> ipcs;
    double ipc = 0;

    // When early_exit is true, the simulation exits when the earliest trace finishes.
    bool early_exit;
	int * fini=NULL;
    bool no_core_caches = true;
    bool no_shared_cache = true;

    int l3_size = 1 << 22;  //23
    int l3_assoc = 1 << 3;
    int l3_blocksz = 1 << 6;
    int mshr_per_bank = 64;

    std::shared_ptr<CacheSystem> cachesys;
    Cache llc;
	
    //ScalarStat cpu_cycles;
	NVMObject_hook * get_Child;
/*
    static map<pair<int, long>, long> page_translation;
    static long free_physical_pages_remaining;
	static int physical_page_replacement;
    static vector<int> free_physical_pages;
    static long max_address ;
    static long page_allocator(long addr, int coreid) {
        long virtual_page_number = addr >> 12;

        //switch(int(translation)) {
          //  case int(Translation::None): {
            //  return addr;
            //}
            //case int(Translation::Random): {
                auto target = make_pair(coreid, virtual_page_number);
                if(page_translation.find(target) == page_translation.end()) {
                    // page doesn't exist, so assign a new page
                    // make sure there are physical pages left to be assigned

                    // if physical page doesn't remain, replace a previous assigned
                    // physical page.
                    if (!free_physical_pages_remaining) {
                      physical_page_replacement++;
                      long phys_page_to_read = lrand() % free_physical_pages.size();
                      assert(free_physical_pages[phys_page_to_read] != -1);
                      page_translation[target] = phys_page_to_read;
                    } else {
                        // assign a new page
                        long phys_page_to_read = lrand() % free_physical_pages.size();
                        // if the randomly-selected page was already assigned
                        if(free_physical_pages[phys_page_to_read] != -1) {
                            long starting_page_of_search = phys_page_to_read;

                            do {
                                // iterate through the list until we find a free page
                                // TODO: does this introduce serious non-randomness?
                                ++phys_page_to_read;
                                phys_page_to_read %= free_physical_pages.size();
                            }
                            while((phys_page_to_read != starting_page_of_search) && free_physical_pages[phys_page_to_read] != -1);
                        }

                        assert(free_physical_pages[phys_page_to_read] == -1);

                        page_translation[target] = phys_page_to_read;
                        free_physical_pages[phys_page_to_read] = coreid;
                        --free_physical_pages_remaining;
                    }
                }

                // SAUGATA TODO: page size should not always be fixed to 4KB
                return (page_translation[target] << 12) | (addr & ((1 << 12) - 1));
         //   }
           // default:
             //   assert(false);
        //}

    }
    static long lrand(void) {
        if(sizeof(int) < sizeof(long)) {
            return static_cast<long>(rand()) << (sizeof(int) * 8) | rand();
        }

        return rand();
    }	*/
};
//	int Processor::physical_page_replacement(0);
//	long Processor::max_address=1024*1024*1024;   //have problem

}
#endif /* __PROCESSOR_H */
