// Copyright (c) 2019 by Robert Bosch GmbH. All rights reserved.
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

#include "iceoryx_utils/posix_wrapper/timer.hpp"
#include "iceoryx_utils/cxx/generic_raii.hpp"
#include "iceoryx_utils/cxx/smart_c.hpp"
#include "iceoryx_utils/error_handling/error_handling.hpp"
#include "iceoryx_utils/platform/platform_correction.hpp"

namespace iox
{
namespace posix
{
Timer::OsTimerCallbackHandle Timer::OsTimer::s_callbackHandlePool[MAX_NUMBER_OF_CALLBACK_HANDLES];

sigval Timer::OsTimerCallbackHandle::indexAndDescriptorToSigval(uint8_t index, uint32_t descriptor)
{
    assert(descriptor < MAX_DESCRIPTOR_VALUE);
    uint32_t temp = (descriptor << 8) | static_cast<uint32_t>(index);
    sigval sigvalData;
    sigvalData.sival_int = static_cast<int>(temp);
    return sigvalData;
}

uint8_t Timer::OsTimerCallbackHandle::sigvalToIndex(sigval intVal)
{
    return static_cast<uint8_t>(0xFF & intVal.sival_int);
}

uint32_t Timer::OsTimerCallbackHandle::sigvalToDescriptor(sigval intVal)
{
    uint32_t temp = static_cast<uint32_t>(intVal.sival_int);
    return (temp >> 8u) & 0xFFFFFFu;
}

void Timer::OsTimerCallbackHandle::incrementDescriptor()
{
    auto callbackHandleDescriptor = m_descriptor.load(std::memory_order_relaxed);
    callbackHandleDescriptor++;
    if (callbackHandleDescriptor >= Timer::OsTimerCallbackHandle::MAX_DESCRIPTOR_VALUE)
    {
        callbackHandleDescriptor = 0u;
    }

    m_descriptor.store(callbackHandleDescriptor, std::memory_order_relaxed);
}

void Timer::OsTimer::callbackHelper(sigval data)
{
    auto index = Timer::OsTimerCallbackHandle::sigvalToIndex(data);
    auto descriptor = Timer::OsTimerCallbackHandle::sigvalToDescriptor(data);

    if (static_cast<uint32_t>(index) >= Timer::OsTimerCallbackHandle::MAX_DESCRIPTOR_VALUE)
    {
        ///@todo decide if to print a warning
        return;
    }

    auto& callbackHandle = OsTimer::s_callbackHandlePool[index];

    // small optimazition to not lock the mutex if the callback handle is not valid anymore
    if (descriptor != callbackHandle.m_descriptor.load(std::memory_order::memory_order_relaxed))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(callbackHandle.m_accessMutex);
    if (!callbackHandle.m_inUse.load(std::memory_order::memory_order_relaxed))
    {
        return;
    }

    if (descriptor != callbackHandle.m_descriptor.load(std::memory_order::memory_order_relaxed))
    {
        return;
    }

    if (!callbackHandle.m_isTimerActive.load(std::memory_order::memory_order_relaxed))
    {
        return;
    }

    /// @todo cxx::expect
    if (callbackHandle.m_timer != nullptr)
    {
        callbackHandle.m_timer->executeCallback();
    }
}

Timer::OsTimer::OsTimer(const units::Duration timeToWait, const std::function<void()>& callback) noexcept
    : m_timeToWait(timeToWait)
    , m_callback(callback)
{
    // Is the callback valid?
    if (!callback)
    {
        m_isInitialized = false;
        m_errorValue = TimerError::NO_VALID_CALLBACK;
        return;
    }

    // find OsTimerCallbackHandle not in use
    bool callbackHandleFound = false;
    uint32_t callbackHandleDescriptor = 0u;
    for (auto& callbackHandle : OsTimer::s_callbackHandlePool)
    {
        if (!callbackHandle.m_inUse.load(std::memory_order_relaxed))
        {
            std::lock_guard<std::mutex> lock(callbackHandle.m_accessMutex);
            // check in use again, just in case there we lost the race before we got the lock
            if (callbackHandle.m_inUse.load(std::memory_order_relaxed))
            {
                m_callbackHandleIndex++;
                continue;
            }

            callbackHandle.incrementDescriptor();
            callbackHandle.m_isTimerActive.store(true, std::memory_order_relaxed);
            callbackHandle.m_inUse.store(true, std::memory_order_relaxed);
            callbackHandle.m_timer = this;

            callbackHandleFound = true;
            callbackHandleDescriptor = callbackHandle.m_descriptor.load(std::memory_order_relaxed);
            break;
        }
        m_callbackHandleIndex++;
    }

    if (!callbackHandleFound)
    {
        errorHandler(Error::kPOSIX_TIMER__TIMERPOOL_OVERFLOW);
    }

    // Create the struct in order to configure the timer in the OS
    struct sigevent asyncCallNotification;
    // We want the timer to call a function
    asyncCallNotification.sigev_notify = SIGEV_THREAD;
    // Set the function pointer to our sigevent
    asyncCallNotification.sigev_notify_function = &callbackHelper;
    // Save the pointer to self in order to execute the callback
    asyncCallNotification.sigev_value.sival_ptr = nullptr; // initialize all bits of the sigval union for mem check
    asyncCallNotification.sigev_value.sival_int =
        Timer::OsTimerCallbackHandle::indexAndDescriptorToSigval(m_callbackHandleIndex, callbackHandleDescriptor)
            .sival_int;
    // Do not set any thread attributes
    asyncCallNotification.sigev_notify_attributes = nullptr;

    auto result = cxx::makeSmartC(timer_create,
                                  cxx::ReturnMode::PRE_DEFINED_ERROR_CODE,
                                  {-1},
                                  {},
                                  CLOCK_REALTIME,
                                  &asyncCallNotification,
                                  &m_timerId);

    if (result.hasErrors())
    {
        m_isInitialized = false;
        m_errorValue = createErrorFromErrno(result.getErrNum()).value;
        m_timerId = INVALID_TIMER_ID;
    }
    else
    {
        m_isInitialized = true;
    }
}


Timer::OsTimer::~OsTimer() noexcept
{
    if (m_timerId != INVALID_TIMER_ID)
    {
        stop();

        auto result = cxx::makeSmartC(timer_delete, cxx::ReturnMode::PRE_DEFINED_ERROR_CODE, {-1}, {}, m_timerId);

        if (result.hasErrors())
        {
            createErrorFromErrno(result.getErrNum());
            std::cerr << "Unable to cleanup posix::Timer \"" << m_timerId << "\" in the destructor" << std::endl;
        }

        m_timerId = INVALID_TIMER_ID;

        auto& callbackHandle = OsTimer::s_callbackHandlePool[m_callbackHandleIndex];
        std::lock_guard<std::mutex> lock(callbackHandle.m_accessMutex);
        callbackHandle.m_inUse.store(false, std::memory_order_relaxed);
    }
}

void Timer::OsTimer::executeCallback() noexcept
{
    if (m_isInitialized && m_callback)
    {
        m_callback();
    }
    else
    {
        // Thread couldn't reach callback or object is not correctly initalized, maybe the originial object was a
        // temporary?
        errorHandler(Error::kPOSIX_TIMER__FIRED_TIMER_BUT_STATE_IS_INVALID);
    }
}

cxx::expected<TimerError> Timer::OsTimer::start(const RunMode runMode, const TimerType timerType) noexcept
{
    // Convert units::Duration to itimerspec
    struct itimerspec interval;
    interval.it_value = m_timeToWait.timespec(units::TimeSpecReference::None);

    if (runMode == RunMode::PERIODIC)
    {
        interval.it_interval = m_timeToWait.timespec(units::TimeSpecReference::None);
    }
    else
    {
        interval.it_interval.tv_sec = 0;
        interval.it_interval.tv_nsec = 0;
    }

    // Set the timer
    auto result = cxx::makeSmartC(
        timer_settime, cxx::ReturnMode::PRE_DEFINED_ERROR_CODE, {-1}, {}, m_timerId, 0, &interval, nullptr);

    if (result.hasErrors())
    {
        return createErrorFromErrno(result.getErrNum());
    }

    OsTimer::s_callbackHandlePool[m_callbackHandleIndex].m_isTimerActive.store(true, std::memory_order_relaxed);

    return cxx::success<void>();
}

cxx::expected<TimerError> Timer::OsTimer::stop() noexcept
{
    // Signal callbackHelper() that no callbacks shall be executed anymore
    auto wasActive =
        OsTimer::s_callbackHandlePool[m_callbackHandleIndex].m_isTimerActive.exchange(false, std::memory_order_relaxed);

    if (!wasActive)
    {
        // Timer was not started yet
        return cxx::success<void>();
    }

    struct itimerspec interval;
    units::Duration zero = 0_s;
    interval.it_value = zero.timespec(units::TimeSpecReference::None);
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_nsec = 0;


    // Disarm the timer
    auto result = cxx::makeSmartC(
        timer_settime, cxx::ReturnMode::PRE_DEFINED_ERROR_CODE, {-1}, {}, m_timerId, 0, &interval, nullptr);

    if (result.hasErrors())
    {
        return createErrorFromErrno(result.getErrNum());
    }

    return cxx::success<void>();
}

cxx::expected<TimerError>
Timer::OsTimer::restart(const units::Duration timeToWait, const RunMode runMode, const TimerType timerType) noexcept
{
    // See if there is currently an active timer in the operating system and update m_isActive accordingly
    auto gettimeResult = timeUntilExpiration();

    if (gettimeResult.has_error())
    {
        return gettimeResult;
    }

    // Set new timeToWait value
    m_timeToWait = timeToWait;

    // Disarm running timer
    if (OsTimer::s_callbackHandlePool[m_callbackHandleIndex].m_isTimerActive.load(std::memory_order_relaxed))
    {
        auto stopResult = stop();

        if (stopResult.has_error())
        {
            return stopResult;
        }
    }

    // Activate the timer with the new timeToWait value
    auto startResult = start(runMode, timerType);

    if (startResult.has_error())
    {
        return startResult;
    }
    return cxx::success<void>();
}

cxx::expected<units::Duration, TimerError> Timer::OsTimer::timeUntilExpiration() noexcept
{
    struct itimerspec currentInterval;

    auto result =
        cxx::makeSmartC(timer_gettime, cxx::ReturnMode::PRE_DEFINED_ERROR_CODE, {-1}, {}, m_timerId, &currentInterval);

    if (result.hasErrors())
    {
        return createErrorFromErrno(result.getErrNum());
    }

    if (currentInterval.it_value.tv_sec == 0 && currentInterval.it_value.tv_nsec == 0)
    {
        // Timer is disarmed
        OsTimer::s_callbackHandlePool[m_callbackHandleIndex].m_isTimerActive.store(false, std::memory_order_relaxed);
    }
    return cxx::success<units::Duration>(currentInterval.it_value);
}

cxx::expected<uint64_t, TimerError> Timer::OsTimer::getOverruns() noexcept
{
    auto result = cxx::makeSmartC(timer_getoverrun, cxx::ReturnMode::PRE_DEFINED_ERROR_CODE, {-1}, {}, m_timerId);

    if (result.hasErrors())
    {
        return createErrorFromErrno(result.getErrNum());
    }
    return cxx::success<uint64_t>(result.getReturnValue());
}

bool Timer::OsTimer::hasError() const noexcept
{
    return !m_isInitialized;
}

TimerError Timer::OsTimer::getError() const noexcept
{
    return m_errorValue;
}

cxx::expected<units::Duration, TimerError> Timer::now() noexcept
{
    struct timespec value;
    auto result =
        cxx::makeSmartC(clock_gettime, cxx::ReturnMode::PRE_DEFINED_ERROR_CODE, {-1}, {}, CLOCK_REALTIME, &value);

    if (result.hasErrors())
    {
        return createErrorFromErrno(result.getErrNum());
    }

    return cxx::success<units::Duration>(value);
}

Timer::Timer(const units::Duration timeToWait) noexcept
    : m_timeToWait(timeToWait)
    , m_creationTime(now().get_value())
{
    if (m_timeToWait.nanoSeconds<uint64_t>() == 0u)
    {
        m_errorValue = TimerError::TIMEOUT_IS_ZERO;
    }
}

Timer::Timer(const units::Duration timeToWait, const std::function<void()>& callback) noexcept
    : m_timeToWait(timeToWait)
    , m_creationTime(now().get_value())
{
    if (m_timeToWait.nanoSeconds<uint64_t>() == 0u)
    {
        m_errorValue = TimerError::TIMEOUT_IS_ZERO;
        return;
    }

    m_osTimer.emplace(timeToWait, callback);
    if (m_osTimer->hasError())
    {
        m_errorValue = m_osTimer->getError();
        m_osTimer.reset();
    }
}

cxx::expected<TimerError> Timer::start(const RunMode runMode, const TimerType timerType) noexcept
{
    if (!m_osTimer.has_value())
    {
        return cxx::error<TimerError>(TimerError::TIMER_NOT_INITIALIZED);
    }

    return m_osTimer->start(runMode, timerType);
}

cxx::expected<TimerError> Timer::stop() noexcept
{
    if (!m_osTimer.has_value())
    {
        return cxx::error<TimerError>(TimerError::TIMER_NOT_INITIALIZED);
    }

    return m_osTimer->stop();
}

cxx::expected<TimerError>
Timer::restart(const units::Duration timeToWait, const RunMode runMode, const TimerType timerType) noexcept
{
    if (timeToWait.nanoSeconds<uint64_t>() == 0u)
    {
        return cxx::error<TimerError>(TimerError::TIMEOUT_IS_ZERO);
    }

    if (!m_osTimer.has_value())
    {
        return cxx::error<TimerError>(TimerError::TIMER_NOT_INITIALIZED);
    }

    return m_osTimer->restart(timeToWait, runMode, timerType);
}

cxx::expected<units::Duration, TimerError> Timer::timeUntilExpiration() noexcept
{
    if (!m_osTimer.has_value())
    {
        return cxx::error<TimerError>(TimerError::TIMER_NOT_INITIALIZED);
    }

    return m_osTimer->timeUntilExpiration();
}

cxx::expected<uint64_t, TimerError> Timer::getOverruns() noexcept
{
    if (!m_osTimer.has_value())
    {
        return cxx::error<TimerError>(TimerError::TIMER_NOT_INITIALIZED);
    }

    return m_osTimer->getOverruns();
}

void Timer::resetCreationTime() noexcept
{
    // Get the current time
    auto now = this->now();

    m_creationTime = now.get_value();
}

bool Timer::hasExpiredComparedToCreationTime() noexcept
{
    // Get the current time
    auto now = this->now();

    // Calc the elapsed time, since Timer object was created
    auto elapsedTime = now.get_value() - m_creationTime;

    if (elapsedTime >= m_timeToWait)
    {
        return true;
    }
    return false; // not enabled, returns false
}

bool Timer::hasError() const noexcept
{
    return m_errorValue != TimerError::NO_ERROR;
}

TimerError Timer::getError() const noexcept
{
    return m_errorValue;
}

cxx::error<TimerError> Timer::createErrorFromErrno(const int32_t errnum) noexcept
{
    TimerError timerError = TimerError::INTERNAL_LOGIC_ERROR;
    switch (errnum)
    {
    case EAGAIN:
    {
        std::cerr << "Kernel failed to allocate timer structures" << std::endl;
        timerError = TimerError::KERNEL_ALLOC_FAILED;
        break;
    }
    case EINVAL:
    {
        std::cerr << "Provided invalid arguments for posix::Timer" << std::endl;
        timerError = TimerError::INVALID_ARGUMENTS;
        break;
    }
    case ENOMEM:
    {
        std::cerr << "Could not allocate memory for posix::Timer" << std::endl;
        timerError = TimerError::ALLOC_MEM_FAILED;
        break;
    }
    case EPERM:
    {
        std::cerr << "No permissions to set the clock" << std::endl;
        timerError = TimerError::NO_PERMISSION;
        break;
    }
    case EFAULT:
    {
        std::cerr << "An invalid pointer was provided" << std::endl;
        timerError = TimerError::INVALID_POINTER;
        break;
    }
    default:
    {
        std::cerr << "Internal logic error in posix::Timer occurred" << std::endl;
        break;
    }
    }
    return cxx::error<TimerError>(timerError);
}

} // namespace posix
} // namespace iox
