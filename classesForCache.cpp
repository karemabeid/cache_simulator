//
// Created by karim on 5/25/2025.
//
#include <iostream>
#include <vector>
#include <math.h>
using std::vector;

class Block{
public:
    bool isValid;
    bool isDirty;
    long tag;
    unsigned long address;
    int counter;

    Block(long tag, unsigned long pc, bool state) : isValid(true), isDirty(state), tag(tag),
    address(pc), counter(0){}
    Block() : isValid(false), isDirty(false){}
};

class Cache{};

class CacheSimulator{};
