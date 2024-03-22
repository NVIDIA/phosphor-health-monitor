
#pragma once
#include "paramConfig.hpp"
#include <string>
#include <vector>
#include <cstdint>
namespace phosphor
{
namespace ipc
{
struct IPCConfig
{
    std::string name;
    uint16_t freq;
    uint16_t windowSize;
    uint16_t logRateLimit;
    std::vector<struct ParamConfig> paramConfig;
};
} // namespace ipc
} // namespace phosphor
