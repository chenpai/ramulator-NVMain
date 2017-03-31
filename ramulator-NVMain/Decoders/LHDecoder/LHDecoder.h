/*******************************************************************************
* 2016.4.14 
* Author list: 
* chen pai
*******************************************************************************/

#ifndef __MEMCONTROL_LHDECODER_H__
#define __MEMCONTROL_LHDECODER_H__

#include "src/AddressTranslator.h"

namespace NVM {

class LHDecoder : public AddressTranslator
{
  public:
    LHDecoder( );
    ~LHDecoder( ) { }

    void SetIgnoreBits( uint64_t numIgnore );
    void SetCachelineSize( uint64_t lineSize );

    void Translate( uint64_t address, uint64_t *row, uint64_t *col, 
                    uint64_t *bank, uint64_t *rank, uint64_t *channel, uint64_t *subarray );
    uint64_t ReverseTranslate( const uint64_t& row, const uint64_t& col, 
                               const uint64_t& bank, const uint64_t& rank, 
                               const uint64_t& channel, const uint64_t& subarray );
    using AddressTranslator::Translate;
    using AddressTranslator::SetDefaultField;

    void RegisterStats( ) { }
    void CalculateStats( ) { }

  private:
    uint64_t ignoreBits;
    uint64_t cachelineSize;
};

};

#endif
