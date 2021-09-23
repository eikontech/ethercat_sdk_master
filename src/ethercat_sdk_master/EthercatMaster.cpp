/*
 ** Copyright 2020 Robotic Systems Lab - ETH Zurich:
 ** Lennart Nachtigall, Jonas Junger
 ** Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 **
 ** 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 **
 ** 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 **
 ** 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 **
 ** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ethercat_sdk_master/EthercatMaster.hpp"

#include <thread>
#include <pthread.h>
#include <cmath>

#define SLEEP_EARLY_STOP_NS (50000) // less than 1e9 - 1!!
#define BILLION (1000000000)

namespace ecat_master{

void EthercatMaster::loadEthercatMasterConfiguration(const EthercatMasterConfiguration &configuration){
  using namespace std::chrono_literals;
  devices_.clear();
  configuration_ = configuration;

  timestepNs_ = floor(configuration.timeStep * 1e9);
  createEthercatBus();
}

EthercatMasterConfiguration EthercatMaster::getConfiguration()
{
    return configuration_;
}

void EthercatMaster::createEthercatBus(){
    bus_.reset(new soem_interface::EthercatBusBase(configuration_.networkInterface));
}

bool EthercatMaster::attachDevice(EthercatDevice::SharedPtr device){
  if (deviceExists(device->getName())){
    MELO_ERROR_STREAM(bus_->get_logger(), "Cannot attach device with name '" << device->getName()
                      << "' because it already exists.");
    return false;
  }
  bus_->addSlave(device);
  device->setEthercatBusBasePointer(bus_.get());
  device->setTimeStep(configuration_.timeStep);
  devices_.push_back(device);
  MELO_DEBUG_STREAM(bus_->get_logger(), "Attached device '"
                    << device->getName()
                    << "' to address "
                    << device->getAddress());
  return true;
}

bool EthercatMaster::startup(){
  bool success = true;

  success &= bus_->startup(false);

  for(const auto & device : devices_)
  {
    if(!bus_->waitForState(EC_STATE_SAFE_OP, device->getAddress(), 50, 0.05))
      MELO_ERROR(bus_->get_logger(), "not in safe op after satrtup!");
    bus_->setState(EC_STATE_OPERATIONAL, device->getAddress());
    success &= bus_->waitForState(EC_STATE_OPERATIONAL, device->getAddress(), 50, 0.05);
  }

  if(!success)
    MELO_ERROR(bus_->get_logger(), "[ethercat_sdk_master:EthercatMaster::startup] Startup not successful.");
  return success;
}

void EthercatMaster::update(UpdateMode updateMode){
  if(firstUpdate_){
    clock_gettime(CLOCK_MONOTONIC, &lastWakeup_);
    sleepEnd_ = lastWakeup_;
    firstUpdate_ = false;
  }
  bus_->updateWrite();
  bus_->updateRead();

  // create update heartbeat if in standalone mode
  switch(updateMode){
    case UpdateMode::StandaloneEnforceRate:
      createUpdateHeartbeat(true);
      break;
    case UpdateMode::StandaloneEnforceStep:
      createUpdateHeartbeat(false);
      break;
    case UpdateMode::NonStandalone:
      break;
  }
}

void EthercatMaster::shutdown(){
  bus_->setState(EC_STATE_INIT);
  bus_->shutdown();
}

void EthercatMaster::preShutdown()
{
   for(auto & device : devices_)
      device->preShutdown();
}

bool EthercatMaster::deviceExists(const std::string& name){
  for (const auto& device : devices_) {
    if (device->getName() == name) {
      return true;
    }
  }
  return false;
}

bool EthercatMaster::setRealtimePriority(int priority){
  sched_param param;
  param.sched_priority = priority;
  int errorFlag = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
  if(errorFlag != 0){
    MELO_ERROR_STREAM(bus_->get_logger(), "[ethercat_sdk_master:EthercatMaster::setRealtimePriority]"
                      << " Could not set thread priority. Check limits.conf or"
                      << " execute as root");
    return false;
  }
  return true;
}


////////////////////////////
// Timing functionalities //
////////////////////////////

// true if ts1 < ts2
inline bool timespecSmallerThan(timespec* ts1, timespec* ts2){
  return (ts1->tv_sec < ts2->tv_sec || (ts1->tv_sec == ts2->tv_sec && ts1->tv_nsec < ts2->tv_nsec));
}

inline void highPrecisionSleep(timespec ts){
  if(ts.tv_nsec >= SLEEP_EARLY_STOP_NS){
    ts.tv_nsec -= SLEEP_EARLY_STOP_NS;
  } else {
    ts.tv_nsec += BILLION - SLEEP_EARLY_STOP_NS;
    ts.tv_sec -= 1;
  }
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);

  // busy waiting for the remaining time
  timespec now;
  do {
    clock_gettime(CLOCK_MONOTONIC, &now);
  } while(timespecSmallerThan(&now, &ts));
}


inline void addNsecsToTimespec(timespec *ts, long int ns){
  if(ts->tv_nsec < BILLION - (ns % BILLION)){
    ts->tv_nsec += ns % BILLION;
    ts->tv_sec += ns / BILLION;
  } else {
    ts->tv_nsec += (ns % BILLION) - BILLION;
    ts->tv_sec += ns / BILLION + 1;
  }
}

inline long int getTimeDiffNs(timespec t_end, timespec t_start){
  return BILLION*(t_end.tv_sec - t_start.tv_sec) + t_end.tv_nsec - t_start.tv_nsec;
}

void EthercatMaster::createUpdateHeartbeat(bool enforceRate){
  if(enforceRate){
    // since we do sleep in absolute times keeping the rate constant is trivial:
    // we simply increment the target sleep wakeup time by the timestep in every
    // iteration.
    addNsecsToTimespec(&sleepEnd_, timestepNs_);
  } else {
    // sleep until timeStepNs_ nanoseconds from last wakeup time
    sleepEnd_ = lastWakeup_;
    addNsecsToTimespec(&sleepEnd_, timestepNs_);
  }

  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  // we are late.
  if(timespecSmallerThan(&sleepEnd_, &now)){
      rateTooLowCounter_++;

    // prevent the creation of a too low update step
    addNsecsToTimespec(&lastWakeup_, static_cast<long int>(configuration_.rateCompensationCoefficient*timestepNs_));
    // we need to sleep a bit
    if(timespecSmallerThan(&now, &lastWakeup_)){
      if(rateTooLowCounter_ >= configuration_.updateRateTooLowWarnThreshold){
        MELO_WARN(bus_->get_logger(), "[ethercat_sdk_master:EthercatMaster::createUpdateHeartbeat]: update rate too low.");
      }
      highPrecisionSleep(lastWakeup_);
      clock_gettime(CLOCK_MONOTONIC, &lastWakeup_);

    // We do not violate the minimum time step
    } else {
      if(rateTooLowCounter_ >= configuration_.updateRateTooLowWarnThreshold){
        MELO_WARN(bus_->get_logger(), "[ethercat_sdk_master:EthercatMaster::createUpdateHeartbeat]: update rate too low.");
      }
      clock_gettime(CLOCK_MONOTONIC, &lastWakeup_);
    }
    return;

  // we are on time
  } else {
    rateTooLowCounter_ = 0;
    highPrecisionSleep(sleepEnd_);
    clock_gettime(CLOCK_MONOTONIC, &lastWakeup_);
  }
}

} // namespace ecat_master
