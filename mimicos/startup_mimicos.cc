#include "mimicos.h"

#include "globals.h"
#include "INIReader.h"
#include <unistd.h>
#include "sim_api.h"
#include "argparser.h"
#include <iostream>
#include <string>
#include <memory>
#include <vector>

int main(int argc, char *argv[]) {

    ArgumentParser args;
    if (!args.parse(argc, argv)) {
        return EXIT_FAILURE;
    }

    std::cout << "[MimicOS]: Configuration file path: " << args.configFile << std::endl;
    std::cout << "[MimicOS]: SIFT file path: " << args.siftFile << std::endl;
    std::cout << "[MimicOS]: Output file path: " << args.outputFile << std::endl;

    try {
        MimicOS* mimicos = MimicOS::getMimicOS(args.configFile, args.outputFile, args.siftFile);

        mimicos->initHandlers();
        mimicos->boot();
        
    } catch (const std::exception& e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "An unknown error occurred." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}