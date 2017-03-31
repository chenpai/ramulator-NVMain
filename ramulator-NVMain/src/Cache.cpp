#include "Cache.h"

#ifndef DEBUG_CACHE
#define debug(...)
#else
#define debug(...) do { \
          printf("\033[36m[DEBUG] %s ", __FUNCTION__); \
          printf(__VA_ARGS__); \
          printf("\033[0m\n"); \
      } while (0)
#endif

namespace NVM 
{

Cache::Cache(int size, int assoc, int block_size,
    int mshr_entry_num, Level level,
    std::shared_ptr<CacheSystem> cachesys):
    level(level), cachesys(cachesys), higher_cache(0),
    lower_cache(nullptr), size(size), assoc(assoc),
    block_size(block_size), mshr_entry_num(mshr_entry_num) {

  debug("level %d size %d assoc %d block_size %d\n",
      int(level), size, assoc, block_size);

  if (level == Level::L1) {
    level_string = "L1";
  } else if (level == Level::L2) {
    level_string = "L2";
  } else if (level == Level::L3) {
    level_string = "L3";
  }

  is_first_level = (level == cachesys->first_level);
  is_last_level = (level == cachesys->last_level);

  // Check size, block size and assoc are 2^N
  assert((size & (size - 1)) == 0);
  assert((block_size & (block_size - 1)) == 0);
  assert((assoc & (assoc - 1)) == 0);
  assert(size >= block_size);

  // Initialize cache configuration
  block_num = size / (block_size * assoc);
  index_mask = block_num - 1;
  index_offset = calc_log2(block_size);
  tag_offset = calc_log2(block_num) + index_offset;

  debug("index_offset %d", index_offset);
  debug("index_mask 0x%x", index_mask);
  debug("tag_offset %d", tag_offset);
  cache_read_miss=0;
  cache_write_miss=0;
  cache_total_miss=0;
  cache_eviction=0;
  cache_read_access=0;
  cache_write_access=0;
  cache_total_access=0;
  cache_mshr_hit=0;
  cache_mshr_unavailable=0;
  cache_set_unavailable=0;
  // regStats
}

bool Cache::send(NVMainRequest req,bool& MSHR) {
  debug("level %d req.addr %lx req.type %d, index %d, tag %ld",
      int(level), req.address.GetPhysicalAddress(), int(req.type), get_index(req.address.GetPhysicalAddress()),
      get_tag(req.address.GetPhysicalAddress()));
  cache_total_access++;
  if (req.type == WRITE) {
    cache_write_access++;
  } else {
    assert(req.type == READ);
    cache_read_access++;
  }
  // If there isn't a set, create it.
  auto& lines = get_lines(req.address.GetPhysicalAddress() );
  std::list<Line>::iterator line;

  if (is_hit(lines, req.address.GetPhysicalAddress(), &line,req.type)) {
    lines.push_back(Line(req.address.GetPhysicalAddress() , get_tag(req.address.GetPhysicalAddress() ), false,
        line->dirty || (req.type == WRITE),line->access,req.coreid ));
    lines.erase(line);
    cachesys->hit_list.push_back(
        make_pair(cachesys->clk + latency[int(level)],req));

    debug("hit, update timestamp %ld", cachesys->clk);
    debug("hit finish time %ld",
        cachesys->clk + latency[int(level)]);
    return 1;

  } else {
    debug("miss @level %d", int(level));
    cache_total_miss++;
    if (req.type == WRITE) {
      cache_write_miss++;
    } else {
      assert(req.type == READ);
      cache_read_miss++;
    }

    // The dirty bit will be set if this is a write request and @L1
    bool dirty = (req.type == WRITE);

    // Modify the type of the request to lower level
    if (req.type == WRITE) {
      req.type = READ;
    }

    // Look it up in MSHR entries
    assert(req.type == READ);
    auto mshr = hit_mshr(req.address.GetPhysicalAddress() );
    if (mshr != mshr_entries.end()) {
      debug("hit mshr");
      cache_mshr_hit++;
      mshr->second->dirty = dirty || mshr->second->dirty;
	  MSHR=1;
      return 1;
    }

    // All requests come to this stage will be READ, so they
    // should be recorded in MSHR entries.
    if (mshr_entries.size() == mshr_entry_num) {
      // When no MSHR entries available, the miss request
      // is stalling.
     // cache_mshr_unavailable++;
      debug("no mshr entry available");
      return 0;
    }

    // Check whether there is a line available
    if (all_sets_locked(lines)) {
      cache_set_unavailable++;
      return 0;
    }

    auto newline = allocate_line(lines, req.address.GetPhysicalAddress(),req.coreid);
    if (newline == lines.end()) {
      return 0;
    }

    newline->dirty = dirty;

    // Add to MSHR entries
    mshr_entries.push_back(make_pair(req.address.GetPhysicalAddress(), newline));

    // Send the request to next level;
    if (!is_last_level) {
      lower_cache->send(req,MSHR);
    } else {
      cachesys->wait_list.push_back(
          make_pair(cachesys->clk + latency[int(level)], req));
    }
    return 1;
  }
}

void Cache::evictline(long addr, bool dirty,int coreid) {

  auto it = cache_lines.find(get_index(addr));
  assert(it != cache_lines.end()); // check inclusive cache
  auto& lines = it->second;
  auto line = find_if(lines.begin(), lines.end(),
      [addr, this](Line l){return (l.tag == get_tag(addr));});

  assert(line != lines.end());
  // Update LRU queue. The dirty bit will be set if the dirty
  // bit inherited from higher level(s) is set.
  lines.push_back(Line(addr, get_tag(addr), false,
      dirty || line->dirty, coreid));
  lines.erase(line);
}

std::pair<long, bool> Cache::invalidate(long addr) {
  long delay = latency_each[int(level)];
  bool dirty = false;

  auto& lines = get_lines(addr);
  if (lines.size() == 0) {
    // The line of this address doesn't exist.
    return make_pair(0, false);
  }
  auto line = find_if(lines.begin(), lines.end(),
      [addr, this](Line l){return (l.tag == get_tag(addr));});

  // If the line is in this level cache, then erase it from
  // the buffer.
  if (line != lines.end()) {
    assert(!line->lock);
    debug("invalidate %lx @ level %d", addr, int(level));
    lines.erase(line);
  } else {
    // If it's not in current level, then no need to go up.
    return make_pair(delay, false);
  }

  if (higher_cache.size()) {
    long max_delay = delay;
    for (auto hc : higher_cache) {
      auto result = hc->invalidate(addr);
      if (result.second) {
        max_delay = max(max_delay, delay + result.first * 2);
      } else {
        max_delay = max(max_delay, delay + result.first);
      }
      dirty = dirty || line->dirty || result.second;
    }
    delay = max_delay;
  } else {
    dirty = line->dirty;
  }
  return make_pair(delay, dirty);
}


void Cache::evict(std::list<Line>* lines,
    std::list<Line>::iterator victim) {
  debug("level %d miss evict victim %lx", int(level), victim->addr);
  cache_eviction++;

  long addr = victim->addr;
  long invalidate_time = 0;
  bool dirty = victim->dirty;
  bool access = victim->access;
  int coreid = victim->coreid;
  // First invalidate the victim line in higher level.
  if (higher_cache.size()) {
    for (auto hc : higher_cache) {
      auto result = hc->invalidate(addr);
      invalidate_time = max(invalidate_time,
          result.first + (result.second ? latency_each[int(level)] : 0));
      dirty = dirty || result.second || victim->dirty;
    }
  }

  debug("invalidate delay: %ld, dirty: %s", invalidate_time,
      dirty ? "true" : "false");

  if (!is_last_level) {
    // not LLC eviction
    assert(lower_cache != nullptr);
    lower_cache->evictline(addr, dirty, coreid);
  } else {
    // LLC eviction
    if (dirty) {
      //NVMainRequest write_req(addr, WRITE);
      NVMainRequest write_req;
      write_req.type = WRITE;
	  write_req.coreid = coreid;
	  //cout<<"address:"<<addr<<endl;
      write_req.address.SetPhysicalAddress(addr);
      write_req.owner = (NVMObject *)(cachesys->getChild->GetTrampoline( )->GetParent( )->GetTrampoline( ));
	  if(access)
		write_req.L3fillDRC = 1;
	  else
		write_req.L3fillDRC = 0;
      cachesys->wait_list.push_back(make_pair(
          cachesys->clk + invalidate_time + latency[int(level)],
          write_req));

      debug("inject one write request to memory system "
          "addr %lx, invalidate time %ld, issue time %ld",
          write_req.addr, invalidate_time,
          cachesys->clk + invalidate_time + latency[int(level)]);
    }
	else
	{
      NVMainRequest req;
      req.type = FILLDRC;
	  req.coreid = coreid;
	  //cout<<"coreid:"<<coreid<<endl;
      req.address.SetPhysicalAddress(addr);
      req.owner = (NVMObject *)(cachesys->getChild->GetTrampoline( )->GetParent( )->GetTrampoline( ));
	  if(access)
	  {
		req.L3fillDRC = 1;
		cachesys->wait_list.push_back(make_pair(
          cachesys->clk + invalidate_time + latency[int(level)],
          req));
	  }
	  else
	  {
		req.L3fillDRC = 0;
		cachesys->wait_list.push_back(make_pair(
          cachesys->clk + invalidate_time + latency[int(level)],
          req));
	  }

	}
  }

  lines->erase(victim);
}

std::list<Cache::Line>::iterator Cache::allocate_line(
    std::list<Line>& lines, long addr,int coreid) {
  // See if an eviction is needed
  if (need_eviction(lines, addr)) {
    // Get victim.
    // The first one might still be locked due to reorder in MC
    auto victim = find_if(lines.begin(), lines.end(),
        [this](Line line) {
          bool check = !line.lock;
          if (!is_first_level) {
            for (auto hc : higher_cache) {
              if(!check) {
                return check;
              }
              check = check && hc->check_unlock(line.addr);
            }
          }
          return check;
        });
    if (victim == lines.end()) {
      return victim;  // doesn't exist a line that's already unlocked in each level
    }
    assert(victim != lines.end());
    evict(&lines, victim);
  }

  // Allocate newline, with lock bit on and dirty bit off
  lines.push_back(Line(addr, get_tag(addr),coreid));
  auto last_element = lines.end();
  --last_element;
  return last_element;
}

bool Cache::is_hit(std::list<Line>& lines, long addr,
    std::list<Line>::iterator* pos_ptr,OpType type) {
  auto pos = find_if(lines.begin(), lines.end(),
      [addr, this](Line l){return (l.tag == get_tag(addr));});
  *pos_ptr = pos;
  if (pos == lines.end()) {
    return false;
  }
  if(!pos->lock)
  {
	 if(type==READ||(type==WRITE&&pos->dirty==1))
	 //OpType t=type; type = t;
	 pos->access = 1;
  }
  return !pos->lock;
}

void Cache::concatlower(Cache* lower) {
  lower_cache = lower;
  assert(lower != nullptr);
  lower->higher_cache.push_back(this);
};

bool Cache::need_eviction(const std::list<Line>& lines, long addr) {
  if (find_if(lines.begin(), lines.end(),
      [addr, this](Line l){
        return (get_tag(addr) == l.tag);})
      != lines.end()) {
    // Due to MSHR, the program can't reach here. Just for checking
    assert(false);
	return false;
  } else {
    if (lines.size() < assoc) {
      return false;
    } else {
      return true;
    }
  }
}

void Cache::callback(NVMainRequest& req) {
  debug("level %d", int(level));

  auto it = find_if(mshr_entries.begin(), mshr_entries.end(),
      [&req, this](std::pair<long, std::list<Line>::iterator> mshr_entry) {
        return (align(mshr_entry.first) == align(req.address.GetPhysicalAddress()));
      });

  if (it != mshr_entries.end()) {
    it->second->lock = false;
    mshr_entries.erase(it);
  }

  if (higher_cache.size()) {
    for (auto hc : higher_cache) {
      hc->callback(req);
    }
  }
}

int CacheSystem::tick() {
  debug("clk %ld", clk);

  ++clk;
  int count=0;
  // Sends ready waiting request to memory
  auto it = wait_list.begin();

  while (it != wait_list.end() && clk >= it->first) {
//    if (!send_memory(it->second)) {
//      ++it;
//    } else {
//----------------------------------
    NVMainRequest *req = new NVMainRequest() ;
    *req = it->second;
	if (!getChild->IsIssuable( req))
	{
		++it;
        delete req;
	}
	else{
      getChild->IssueCommand(req );
//-----------------------------------
      debug("complete req: addr %lx", (it->second).address.GetPhysicalAddress());

      it = wait_list.erase(it);
	  count++;
    }

  }

  // hit request callback
   it = hit_list.begin();
  while (it != hit_list.end()) {
    if (clk >= it->first) {
      it->second.callback(it->second);
      debug("finish hit: addr %lx", (it->second).address.GetPhysicalAddress());

      it = hit_list.erase(it);
    } else {
      ++it;
    }
  }
  return count;

}

Stats *Cache::GetStats( )
{
    return stats;
}

void Cache::StatName( std::string name )
{
    statName = name;
}

std::string Cache::StatName( )
{
    return statName;
}

void Cache::RegisterStats( )
{
/*	AddStat(cache_read_miss );
	AddStat(cache_write_miss );
    AddStat(cache_total_miss );
    AddStat(cache_eviction );
    AddStat(cache_read_access );
    AddStat(cache_write_access );
    AddStat(cache_total_access );
    AddStat(cache_mshr_hit );
    AddStat(cache_mshr_unavailable );
    AddStat(cache_set_unavailable );*/

}

} // namespace NVM 
