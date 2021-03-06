/**
 *  Copyright 2014-2016 CyberVision, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


#ifndef PERIODICLOGUPLOADSTRATEGY_HPP_
#define PERIODICLOGUPLOADSTRATEGY_HPP_

#include <cstdlib>
#include <chrono>

#include "kaa/logging/Log.hpp"
#include "kaa/log/ILogStorageStatus.hpp"
#include "kaa/log/DefaultLogUploadStrategy.hpp"

namespace kaa {

class PeriodicLogUploadStrategy : public DefaultLogUploadStrategy {
public:
    PeriodicLogUploadStrategy(std::size_t logUploadCheckReriod, IKaaClientContext &context)
        : DefaultLogUploadStrategy(context), lastUploadTime_(Clock::now())
    {
        setLogUploadCheckPeriod(logUploadCheckReriod);
    }

    virtual LogUploadStrategyDecision isUploadNeeded(ILogStorageStatus& status) override
    {
        auto now = Clock::now();

        if (now >= (lastUploadTime_ + std::chrono::seconds(logUploadCheckReriod_))) {
            KAA_LOG_INFO(boost::format("Need to upload logs - current count: %llu, lastUploadedTime: %llu, timeLimit: %llu sec")
                                                    % status.getRecordsCount() % lastUploadTime_.time_since_epoch().count()
                                                    % logUploadCheckReriod_);
            lastUploadTime_ = now;
            return LogUploadStrategyDecision::UPLOAD;
        }

        return LogUploadStrategyDecision::NOOP;
    }

private:
    typedef std::chrono::system_clock Clock;
    std::chrono::time_point<Clock> lastUploadTime_;
};

}

#endif /* PERIODICLOGUPLOADSTRATEGY_HPP_ */
