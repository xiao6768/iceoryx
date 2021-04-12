// Copyright (c) 2021 by Apex.AI Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "topic_data.hpp"

#include "iceoryx_posh/popo/subscriber.hpp"
#include "iceoryx_posh/runtime/posh_runtime.hpp"
#include "iceoryx_utils/cxx/string.hpp"
#include "iceoryx_utils/posix_wrapper/signal_handler.hpp"

bool killswitch = false;
constexpr char APP_NAME[] = "iox-cpp-subscriber-complexdata";

static void sigHandler(int f_sig [[gnu::unused]])
{
    // caught SIGINT or SIGTERM, now exit gracefully
    killswitch = true;
}

int main()
{
    // register sigHandler
    auto signalIntGuard = iox::posix::registerSignalHandler(iox::posix::Signal::INT, sigHandler);
    auto signalTermGuard = iox::posix::registerSignalHandler(iox::posix::Signal::TERM, sigHandler);

    // initialize runtime
    iox::runtime::PoshRuntime::initRuntime(APP_NAME);

    // initialized subscriber
    iox::popo::Subscriber<ComplexDataType> subscriber({"Radar", "FrontLeft", "Object"});
    //
    // run until interrupted by Ctrl-C
    while (!killswitch)
    {
        subscriber.take()
            .and_then([](auto& sample) {
                std::stringstream s;
                s << APP_NAME << " got values:";

                s << std::endl << "from stringForwardList: ";
                for (auto i = sample->stringForwardList.begin(); i != sample->stringForwardList.end(); ++i)
                {
                    s << *i << ", ";
                }

                s << std::endl << "from integerList: ";
                for (auto i = sample->integerList.begin(); i != sample->integerList.end(); ++i)
                {
                    s << *i << ", ";
                }

                s << std::endl << "from optionalList: ";
                for (auto i = sample->optionalList.begin(); i != sample->optionalList.end(); ++i)
                {
                    (i->has_value()) ? s << i->value() << ", " : s << "optional is empty, ";
                }

                s << std::endl << "from floatStack: ";
                auto stackCopy = sample->floatStack;
                for (uint64_t i = stackCopy.capacity(); i > 0U; i--)
                {
                    auto result = stackCopy.pop();
                    (result.has_value()) ? s << result.value() << ", " : s << "stack is empty";
                }

                s << std::endl << "from someString: ";
                s << sample->someString;

                s << std::endl << "from doubleVector: ";
                for (uint64_t i = 0U; i < sample->doubleVector.size(); ++i)
                {
                    s << sample->doubleVector[i] << ", ";
                }

                s << std::endl << "from variantVector: ";
                for (uint64_t i = 0U; i < sample->variantVector.size(); ++i)
                {
                    if (sample->variantVector[i].index() == 0)
                    {
                        s << *sample->variantVector[i].template get_at_index<0>() << ", ";
                    }
                    if (sample->variantVector[i].index() == 1)
                    {
                        s << *sample->variantVector[i].template get_at_index<1>() << ", ";
                    }
                }

                s << std::endl;
                std::cout << s.str();
            })
            .or_else([](auto& result) {
                // only has to be called if the alternative is of interest,
                // i.e. if nothing has to happen when no data is received and
                // a possible error alternative is not checked or_else is not needed
                if (result != iox::popo::ChunkReceiveResult::NO_CHUNK_AVAILABLE)
                {
                    std::cout << "Error receiving chunk." << std::endl;
                }
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return (EXIT_SUCCESS);
}
