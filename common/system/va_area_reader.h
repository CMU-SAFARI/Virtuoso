

#include <iostream>
#include <cstdio>
#include <sstream>

#include <vector>
#include <utility>
#include <unistd.h>

namespace ParametricDramDirectoryMSI
{

    class vaAreaReader
    {

    private:
        std::vector<std::pair<unsigned long, unsigned long>> memory_areas;
        int pid;

    public:
        std::string ReadProcessMemoryMap()
        {
            std::ostringstream oss;
            std::string maps_file = "/proc/" + std::to_string(pid) + "/maps";
            std::FILE *file = std::fopen(maps_file.c_str(), "r");

            memory_areas.clear(); // Clear any previous memory areas

            if (file != nullptr)
            {
                char buffer[256];
                while (std::fgets(buffer, sizeof(buffer), file))
                {
                    oss << buffer;
                    unsigned long base, end;
                    if (std::sscanf(buffer, "%lx-%lx", &base, &end) == 2)
                    {
                        memory_areas.push_back(std::make_pair(base, end));
                    }
                }
                std::fclose(file);
            }

            return oss.str();
        }

        const std::vector<std::pair<unsigned long, unsigned long>> &GetMemoryAreas() const
        {
            return memory_areas;
        }
    };

}