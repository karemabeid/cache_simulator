/* 046267 Computer Architecture - Winter 20/21 - HW #2 */

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include "classesForCache.cpp"


using std::FILE;
using std::string;
using std::cout;
using std::endl;
using std::cerr;
using std::ifstream;
using std::stringstream;

int main(int argc, char **argv) {
    if (argc < 19) {
        cerr << "Not enough arguments" << endl;
        return 0;
    }

    // Get input arguments

    // File
    // Assuming it is the first argument
    char* fileString = argv[1];
    ifstream file(fileString); //input file stream
    string line;
    if (!file || !file.good()) {
        // File doesn't exist or some other error
        cerr << "File not found" << endl;
        return 0;
    }
    ///example 1 :
    //unsigned MemCyc = 100, BSize = 3, L1Size = 4, L2Size = 6, L1Assoc = 1,L2Assoc = 0, L1Cyc = 1, L2Cyc = 5, WrAlloc = 1;


    ///example 2 :
    // unsigned MemCyc = 50, BSize = 4, L1Size = 6, L2Size = 8, L1Assoc = 1,L2Assoc = 2, L1Cyc = 2, L2Cyc = 4, WrAlloc = 1;


    ///example 3 :
    //   unsigned MemCyc = 10, BSize = 2, L1Size = 4, L2Size = 4, L1Assoc = 1,  L2Assoc = 2, L1Cyc = 1, L2Cyc = 5, WrAlloc = 1;


    unsigned MemCyc = 0, BSize = 0, L1Size = 0, L2Size = 0, L1Assoc = 0,
            L2Assoc = 0, L1Cyc = 0, L2Cyc = 0, WrAlloc = 0;

    for (int i = 2; i < 19; i += 2) {
        string s(argv[i]);
        if (s == "--mem-cyc") {
            MemCyc = atoi(argv[i + 1]);
        } else if (s == "--bsize") {
            BSize = atoi(argv[i + 1]);
        } else if (s == "--l1-size") {
            L1Size = atoi(argv[i + 1]);
        } else if (s == "--l2-size") {
            L2Size = atoi(argv[i + 1]);
        } else if (s == "--l1-cyc") {
            L1Cyc = atoi(argv[i + 1]);
        } else if (s == "--l2-cyc") {
            L2Cyc = atoi(argv[i + 1]);
        } else if (s == "--l1-assoc") {
            L1Assoc = atoi(argv[i + 1]);
        } else if (s == "--l2-assoc") {
            L2Assoc = atoi(argv[i + 1]);
        } else if (s == "--wr-alloc") {
            WrAlloc = atoi(argv[i + 1]);
        } else {
            cerr << "Error in arguments" << endl;
            return 0;
        }
    }


    /// here we have to initialize our cache/memory and then work with it
    CacheManager *main_controller = new CacheManager(
            BSize, WrAlloc, MemCyc,
            L1Size, L1Assoc, L1Cyc,
            L2Size, L2Assoc, L2Cyc
    );

    while (getline(file, line)) {

        stringstream ss(line);
        string address;
        char operation = 0; // read (R) or write (W)
        if (!(ss >> operation >> address)) {
            // Operation appears in an Invalid format
            cout << "Command Format error" << endl;
            return 0;
        }

        // DEBUG - remove this line
        //cout << "operation: " << operation;

        string cutAddress = address.substr(2); // Removing the "0x" part of the address

        // DEBUG - remove this line
        //cout << ", address (hex)" << cutAddress;

        unsigned long int num = 0;
        num = strtoul(cutAddress.c_str(), NULL, 16);

        // DEBUG - remove this line
        //cout << " (dec) " << num << endl;

        main_controller->access(static_cast<uint32_t>(num), operation);
    }

    double L1MissRate;
    double L2MissRate;
    double avgAccTime;
    main_controller->finalizeStats(L1MissRate, L2MissRate, avgAccTime);
    printf("L1miss=%.03f ", L1MissRate);
    printf("L2miss=%.03f ", L2MissRate);
    printf("AccTimeAvg=%.03f\n", avgAccTime);

    return 0;
}
