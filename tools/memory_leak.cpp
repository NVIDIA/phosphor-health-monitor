/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
const size_t maxAllocations = 500; // Limit the number of allocations
char** myptr;
unsigned long int i = 0;

void leak_memory()
{
    if (i < maxAllocations)
    {
        myptr[i] = (char*)malloc(1024 * 1024 * 7); // Allocate 7 MB
        if (myptr[i] == nullptr)
        {
            std::cerr << "Memory allocation failed!" << std::endl;
            exit(1);
        }
        // Fill the buffer with data (e.g., all zeros)
        memset(myptr[i], 0, 1024 * 1024 * 7);
        i++;
    }
    else
    {
        std::cerr << "Maximum allocations reached. Exiting." << std::endl;
        exit(0);
    }
}

int main()
{
    myptr = (char**)malloc(maxAllocations * sizeof(char*));
    if (myptr == nullptr)
    {
        std::cerr << "Memory allocation for myptr failed!" << std::endl;
        return 1;
    }

    while (1)
    {
        leak_memory();
        std::this_thread::sleep_for(std::chrono::milliseconds(4000));
    }

    return 0;
}
