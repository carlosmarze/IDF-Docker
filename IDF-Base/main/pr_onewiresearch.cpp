#include "pr_onewiresearch.h"

OneWireSearch::OneWireSearch(OneWire &ow) : ow_(ow) {
    reset_search();
}

void OneWireSearch::reset_search() {
    last_discrepancy_ = 0;
    last_device_ = false;
    memset(rom_, 0, sizeof(rom_));
}

bool OneWireSearch::next(uint8_t out_rom[8]) {
    if (last_device_) return false;

    if (!ow_.reset()) return false;

    ow_.write_byte(0xF0); // SEARCH ROM

    uint8_t new_discrepancy = 0;

    for (int bit = 0; bit < 64; bit++) {
        int b = ow_.read_bit();
        int c = ow_.read_bit();

        int chosen;

        if (b == 1 && c == 1)
            return false;

        if (b == 0 && c == 0) {
            if (bit == last_discrepancy_)
                chosen = 1;
            else if (bit > last_discrepancy_)
                chosen = 0;
            else
                chosen = (rom_[bit / 8] >> (bit % 8)) & 1;

            if (chosen == 0)
                new_discrepancy = bit;
        } else {
            chosen = b;
        }

        if (chosen)
            rom_[bit / 8] |= (1 << (bit % 8));
        else
            rom_[bit / 8] &= ~(1 << (bit % 8));

        ow_.write_bit(chosen);
    }

    last_discrepancy_ = new_discrepancy;
    if (last_discrepancy_ == 0)
        last_device_ = true;

    memcpy(out_rom, rom_, 8);
    return true;
}
