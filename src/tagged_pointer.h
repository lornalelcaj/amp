#pragma once
#include <assert.h>

constexpr int N_LOWER_TAG_BITS = 16;
constexpr int N_UPPER_TAG_BITS = 16;
constexpr uintptr_t LOWER_TAG_MASK = (uintptr_t)~(~(uintptr_t)NULL << N_LOWER_TAG_BITS);
constexpr uintptr_t UPPER_TAG_MASK = (uintptr_t)~(~(uintptr_t)NULL >> N_UPPER_TAG_BITS);

constexpr int N_ADDRESS_BITS = sizeof(uintptr_t) * 8 - N_LOWER_TAG_BITS - N_UPPER_TAG_BITS;
constexpr uintptr_t ADDRESS_MASK = ~(LOWER_TAG_MASK | UPPER_TAG_MASK);

constexpr int OBJ_ALIGNMENT = 1LL << N_LOWER_TAG_BITS;

// A pointer to an object of type @param obj_t and a tag which are stored in the same word.
// the tag is split between and occupies the topmost and bottom most bits in the word.
// The top most bits can be used since they are often not mapped to actual addresses
// The bottom moast bits can be used if all stored addresses are to properly alligned objects.
template <typename obj_t> struct TaggedPointer {
private:
    uintptr_t tagged_pointer = 0;

public:
    static_assert(N_LOWER_TAG_BITS + N_UPPER_TAG_BITS < sizeof(obj_t*) * 8);

    // Get the address from a tagged pointer
    static obj_t* extract_address(TaggedPointer<obj_t> ptr) {
        return (obj_t*)(ptr.tagged_pointer & ADDRESS_MASK);
    }

    // Get the tag from a tagged pointer
    static size_t extract_tag(TaggedPointer<obj_t> ptr) {
        uintptr_t tagLower = ptr.tagged_pointer & LOWER_TAG_MASK;
        uintptr_t tagUpper = ptr.tagged_pointer & UPPER_TAG_MASK;
        uintptr_t tag = (tagUpper >> N_ADDRESS_BITS) | tagLower;

        assert(tag != 0 || (tagUpper == 0 && tagLower == 0)); // check if ther result is empty so are both tag sections
        assert(tag < (1LL << (N_LOWER_TAG_BITS + N_UPPER_TAG_BITS))); // check that there are no excess bits set
        return (size_t)tag;
    }
    

    // Create a tagged pointer from an address and a tag
    static TaggedPointer<obj_t> pack_pointer(obj_t* address, size_t tag) {
        uintptr_t cleanAddress = ((uintptr_t)address & ADDRESS_MASK);
        
        // decompose tag into upper and lower section
        uintptr_t tagLower = ((uintptr_t)tag & LOWER_TAG_MASK);
        uintptr_t tagUpper = ((uintptr_t)tag << N_ADDRESS_BITS) & UPPER_TAG_MASK;

        assert((uintptr_t)tag < (1LL << (N_LOWER_TAG_BITS + N_UPPER_TAG_BITS)) || ((tagUpper | tagLower) == 0)); // check if tag is larger than what can be stored it overflows to 0

        // assemble the new tagged pointer
        TaggedPointer<obj_t> new_tp;
        new_tp.tagged_pointer = (cleanAddress | tagLower | tagUpper);
        assert(address == 0 || new_tp.tagged_pointer != 0);
        return new_tp;
    }


    bool operator== (const TaggedPointer<obj_t> other) {
        return this->tagged_pointer == other.tagged_pointer;
    }
    bool operator!= (const TaggedPointer<obj_t> other) {
        return !(*this == other);
    }
};

static_assert(sizeof(TaggedPointer<void>) == sizeof(void*));
