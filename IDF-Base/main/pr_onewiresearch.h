#pragma once
#include <cstdint>
#include <cstring>
#include "pr_onewire.h"

class OneWireSearch {
public:
    explicit OneWireSearch(OneWire &ow);

    void reset_search();
    bool next(uint8_t out_rom[8]);
    

private:
    OneWire &ow_;
    uint8_t rom_[8];
    uint8_t last_discrepancy_;
    bool last_device_;
};
