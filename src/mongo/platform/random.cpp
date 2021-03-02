/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/platform/random.h"

#include <string.h>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <errno.h>
#endif

#define _CRT_RAND_S
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

#include "mongo/stdx/memory.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"

namespace mongo {

// ---- PseudoRandom  -----

uint32_t PseudoRandom::nextUInt32() {
    uint32_t t = _x ^ (_x << 11);
    _x = _y;
    _y = _z;
    _z = _w;
    return _w = _w ^ (_w >> 19) ^ (t ^ (t >> 8));
}

namespace {
const uint32_t default_y = 362436069;
const uint32_t default_z = 521288629;
const uint32_t default_w = 88675123;
}  // namespace

PseudoRandom::PseudoRandom(uint32_t seed) {
    _x = seed;
    _y = default_y;
    _z = default_z;
    _w = default_w;
}

PseudoRandom::PseudoRandom(int32_t seed) : PseudoRandom(static_cast<uint32_t>(seed)) {}

PseudoRandom::PseudoRandom(int64_t seed)
    : PseudoRandom(static_cast<uint32_t>(seed >> 32) ^ static_cast<uint32_t>(seed)) {}

int32_t PseudoRandom::nextInt32() {
    return nextUInt32();
}

int64_t PseudoRandom::nextInt64() {
    uint64_t a = nextUInt32();
    uint64_t b = nextUInt32();
    return (a << 32) | b;
}

double PseudoRandom::nextCanonicalDouble() {
    double result;
    do {
        auto generated = static_cast<uint64_t>(nextInt64());
        result = static_cast<double>(generated) / std::numeric_limits<uint64_t>::max();
    } while (result == 1.0);
    return result;
}

// --- SecureRandom ----

SecureRandom::~SecureRandom() {}

#ifdef _WIN32
class WinSecureRandom : public SecureRandom {
public:
    WinSecureRandom() {
        auto ntstatus = ::BCryptOpenAlgorithmProvider(
            &_algHandle, BCRYPT_RNG_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);
        if (ntstatus != STATUS_SUCCESS) {
            error() << "Failed to open crypto algorithm provider while creating secure random "
                       "object; NTSTATUS: "
                    << ntstatus;
            fassertFailed(28815);
        }
    }

    virtual ~WinSecureRandom() {
        auto ntstatus = ::BCryptCloseAlgorithmProvider(_algHandle, 0);
        if (ntstatus != STATUS_SUCCESS) {
            warning() << "Failed to close crypto algorithm provider destroying secure random "
                         "object; NTSTATUS: "
                      << ntstatus;
        }
    }

    int64_t nextInt64() {
        int64_t value;
        auto ntstatus =
            ::BCryptGenRandom(_algHandle, reinterpret_cast<PUCHAR>(&value), sizeof(value), 0);
        if (ntstatus != STATUS_SUCCESS) {
            error() << "Failed to generate random number from secure random object; NTSTATUS: "
                    << ntstatus;
            fassertFailed(28814);
        }
        return value;
    }

private:
    BCRYPT_ALG_HANDLE _algHandle;
};

std::unique_ptr<SecureRandom> SecureRandom::create() {
    return stdx::make_unique<WinSecureRandom>();
}

#elif defined(__linux__) || defined(__sun) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__EMSCRIPTEN__)

class InputStreamSecureRandom : public SecureRandom {
public:
    InputStreamSecureRandom(const char* fn) {
        _in = stdx::make_unique<std::ifstream>(fn, std::ios::binary | std::ios::in);
        if (!_in->is_open()) {
            error() << "cannot open " << fn << " " << strerror(errno);
            fassertFailed(28839);
        }
    }

    /*
     * 用户认证时会调用这个函数，下面是客户端执行db.auth()命令的调用栈
#0  mongo::InputStreamSecureRandom::nextInt64 (this=0x7fab14a21760) at src/mongo/platform/random.cpp:165
#1  0x00007fab24fa8c04 in mongo::SaslSCRAMServerMechanism<mongo::SCRAMSHA1Policy>::_firstStep (this=0x7fab14835220, opCtx=0x7fab14415420, inputData=...)
    at src/mongo/db/auth/sasl_scram_server_conversation.cpp:240
#2  0x00007fab24fa7868 in mongo::SaslSCRAMServerMechanism<mongo::SCRAMSHA1Policy>::stepImpl (this=0x7fab14835220, opCtx=0x7fab14415420, inputData=...)
    at src/mongo/db/auth/sasl_scram_server_conversation.cpp:68
#3  0x00007fab24f96e34 in mongo::ServerMechanismBase::step (this=0x7fab14835220, opCtx=0x7fab14415420, input=...) at src/mongo/db/auth/sasl_mechanism_registry.h:162
#4  0x00007fab24f953f9 in mongo::(anonymous namespace)::doSaslStep (opCtx=0x7fab14415420, session=0x7fab14a21660, cmdObj=..., result=0x7fab1feab700)
    at src/mongo/db/auth/sasl_commands.cpp:184
#5  0x00007fab24f95c33 in mongo::(anonymous namespace)::doSaslStart (opCtx=0x7fab14415420, db=..., cmdObj=..., result=0x7fab1feab700, principalName=0x7fab1feab640)
    at src/mongo/db/auth/sasl_commands.cpp:244
#6  0x00007fab24f96232 in mongo::(anonymous namespace)::CmdSaslStart::run (this=0x7fab27ad7ca0 <mongo::(anonymous namespace)::cmdSaslStart>, opCtx=0x7fab14415420, db=...,
    cmdObj=..., result=...) at src/mongo/db/auth/sasl_commands.cpp:293
#7  0x00007fab251e0a20 in mongo::BasicCommand::Invocation::run (this=0x7fab0c527800, opCtx=0x7fab14415420, result=0x7fab1393f860) at src/mongo/db/commands.cpp:639
#8  0x00007fab23681252 in mongo::(anonymous namespace)::runCommandImpl (opCtx=0x7fab14415420, invocation=0x7fab0c527800, request=..., replyBuilder=0x7fab1393f860,
    startOperationTime=..., behaviors=..., extraFieldsBuilder=0x7fab1feabb20, sessionOptions=...) at src/mongo/db/service_entry_point_common.cpp:610
#9  0x00007fab2368391c in mongo::(anonymous namespace)::execCommandDatabase (opCtx=0x7fab14415420, command=0x7fab27ad7ca0 <mongo::(anonymous namespace)::cmdSaslStart>,
    request=..., replyBuilder=0x7fab1393f860, behaviors=...) at src/mongo/db/service_entry_point_common.cpp:912
#10 0x00007fab23684a3f in mongo::(anonymous namespace)::<lambda()>::operator()(void) const (__closure=0x7fab1feabfc0) at src/mongo/db/service_entry_point_common.cpp:1053
#11 0x00007fab2368533d in mongo::(anonymous namespace)::receivedCommands (opCtx=0x7fab14415420, message=..., behaviors=...) at src/mongo/db/service_entry_point_common.cpp:1068
#12 0x00007fab23687817 in mongo::ServiceEntryPointCommon::handleRequest (opCtx=0x7fab14415420, m=..., behaviors=...) at src/mongo/db/service_entry_point_common.cpp:1353
#13 0x00007fab2366d890 in mongo::ServiceEntryPointMongod::handleRequest (this=0x7fab198a3ce0, opCtx=0x7fab14415420, m=...) at src/mongo/db/service_entry_point_mongod.cpp:262
#14 0x00007fab2367a6d2 in mongo::ServiceStateMachine::_processMessage (this=0x7fab14a17030, guard=...) at src/mongo/transport/service_state_machine.cpp:479
#15 0x00007fab23675f8d in mongo::ServiceStateMachine::_runNextInGuard (this=0x7fab14a17030, guard=...) at src/mongo/transport/service_state_machine.cpp:566
#16 0x00007fab23676310 in mongo::ServiceStateMachine::<lambda()>::operator()(void) const (__closure=0x7fab1483a2e0) at src/mongo/transport/service_state_machine.cpp:623
#17 0x00007fab236779fa in std::_Function_handler<void(), mongo::ServiceStateMachine::_scheduleNextWithGuard(mongo::ServiceStateMachine::ThreadGuard, mongo::transport::ServiceExecutor::ScheduleFlags, mongo::transport::ServiceExecutorTaskName, mongo::ServiceStateMachine::Ownership)::<lambda()> >::_M_invoke(const std::_Any_data &) (__functor=...)
    at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:297
#18 0x00007fab230a68c0 in std::function<void ()>::operator()() const (this=0x7fab1feac510) at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:687
#19 0x00007fab24dd0ed5 in mongo::transport::ServiceExecutorSynchronous::schedule(std::function<void ()>, mongo::transport::ServiceExecutor::ScheduleFlags, mongo::transport::ServiceExecutorTaskName) (this=0x7fab198fa2a0, task=..., flags=mongo::transport::ServiceExecutor::kMayRecurse, taskName=mongo::transport::kSSMProcessMessage)
    at src/mongo/transport/service_executor_synchronous.cpp:116
#20 0x00007fab236764a9 in mongo::ServiceStateMachine::_scheduleNextWithGuard (this=0x7fab14a17030, guard=..., flags=mongo::transport::ServiceExecutor::kMayRecurse,
    taskName=mongo::transport::kSSMProcessMessage, ownershipModel=mongo::ServiceStateMachine::kOwned) at src/mongo/transport/service_state_machine.cpp:635
#21 0x00007fab236756a3 in mongo::ServiceStateMachine::_sourceCallback (this=0x7fab14a17030, status=...) at src/mongo/transport/service_state_machine.cpp:398
#22 0x00007fab236750cf in mongo::ServiceStateMachine::<lambda(mongo::StatusWith<mongo::Message>)>::operator()(mongo::StatusWith<mongo::Message>) const (
    __closure=0x7fab1feac7f8, msg=...) at src/mongo/transport/service_state_machine.cpp:349
#23 0x00007fab23676b16 in mongo::future_details::call<mongo::ServiceStateMachine::_sourceMessage(mongo::ServiceStateMachine::ThreadGuard)::<lambda(mongo::StatusWith<mongo::Message>)>&, mongo::StatusWith<mongo::Message> >(mongo::ServiceStateMachine::<lambda(mongo::StatusWith<mongo::Message>)> &, <unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb809029>) (func=..., arg=<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb809029>) at src/mongo/util/future_impl.h:237
---Type <return> to continue, or q <return> to quit---
#24 0x00007fab23676bc1 in mongo::future_details::FutureImpl<mongo::Message>::<lambda(mongo::Message&&)>::operator()(<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808bc8>) const (this=0x7fab1feac770, val=<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808bc8>) at src/mongo/util/future_impl.h:857
#25 0x00007fab2367709e in mongo::future_details::FutureImpl<mongo::Message>::generalImpl<mongo::future_details::FutureImpl<T>::getAsync(Func&&) && [with Func = mongo::ServiceStateMachine::_sourceMessage(mongo::ServiceStateMachine::ThreadGuard)::<lambda(mongo::StatusWith<mongo::Message>)>; T = mongo::Message]::<lambda(mongo::Message&&)>, mongo::future_details::FutureImpl<T>::getAsync(Func&&) && [with Func = mongo::ServiceStateMachine::_sourceMessage(mongo::ServiceStateMachine::ThreadGuard)::<lambda(mongo::StatusWith<mongo::Message>)>; T = mongo::Message]::<lambda(mongo::Status&&)>, mongo::future_details::FutureImpl<T>::getAsync(Func&&) && [with Func = mongo::ServiceStateMachine::_sourceMessage(mongo::ServiceStateMachine::ThreadGuard)::<lambda(mongo::StatusWith<mongo::Message>)>; T = mongo::Message]::<lambda()> >(<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808e37>, <unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808e48>, <unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808e59>) (this=0x7fab1feac800, success=<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808e37>,
    fail=<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808e48>,
    notReady=<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808e59>) at src/mongo/util/future_impl.h:1172
#26 0x00007fab23677235 in mongo::future_details::FutureImpl<mongo::Message>::getAsync<mongo::ServiceStateMachine::_sourceMessage(mongo::ServiceStateMachine::ThreadGuard)::<lambda(mongo::StatusWith<mongo::Message>)> >(<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808b4d>) (this=0x7fab1feac800,
    func=<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb808b4d>) at src/mongo/util/future_impl.h:871
#27 0x00007fab236769e1 in mongo::Future<mongo::Message>::getAsync<mongo::ServiceStateMachine::_sourceMessage(mongo::ServiceStateMachine::ThreadGuard)::<lambda(mongo::StatusWith<mongo::Message>)> >(<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb80c470>) (this=0x7fab1feac800,
    func=<unknown type in /root/mongo/drewryz/mongo/mongod, CU 0xb7642e3, DIE 0xb80c470>) at src/mongo/util/future.h:349
#28 0x00007fab23675208 in mongo::ServiceStateMachine::_sourceMessage (this=0x7fab14a17030, guard=...) at src/mongo/transport/service_state_machine.cpp:344
#29 0x00007fab23675f4b in mongo::ServiceStateMachine::_runNextInGuard (this=0x7fab14a17030, guard=...) at src/mongo/transport/service_state_machine.cpp:563
#30 0x00007fab23676310 in mongo::ServiceStateMachine::<lambda()>::operator()(void) const (__closure=0x7fab1362cac0) at src/mongo/transport/service_state_machine.cpp:623
#31 0x00007fab236779fa in std::_Function_handler<void(), mongo::ServiceStateMachine::_scheduleNextWithGuard(mongo::ServiceStateMachine::ThreadGuard, mongo::transport::ServiceExecutor::ScheduleFlags, mongo::transport::ServiceExecutorTaskName, mongo::ServiceStateMachine::Ownership)::<lambda()> >::_M_invoke(const std::_Any_data &) (__functor=...)
    at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:297
#32 0x00007fab230a68c0 in std::function<void ()>::operator()() const (this=0x7fab1392a820) at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:687
#33 0x00007fab24dd0d0d in mongo::transport::ServiceExecutorSynchronous::<lambda()>::operator()(void) const (__closure=0x7fab14225020)
    at src/mongo/transport/service_executor_synchronous.cpp:134
#34 0x00007fab24dd135c in std::_Function_handler<void(), mongo::transport::ServiceExecutorSynchronous::schedule(mongo::transport::ServiceExecutor::Task, mongo::transport::ServiceExecutor::ScheduleFlags, mongo::transport::ServiceExecutorTaskName)::<lambda()> >::_M_invoke(const std::_Any_data &) (__functor=...)
    at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:297
#35 0x00007fab230a68c0 in std::function<void ()>::operator()() const (this=0x7fab19848ab0) at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:687
#36 0x00007fab25703a10 in mongo::<lambda()>::operator()(void) const (__closure=0x7fab19848aa0) at src/mongo/transport/service_entry_point_utils.cpp:156
#37 0x00007fab25704120 in std::_Function_handler<void(), mongo::launchServiceWorkerThread(std::function<void()>)::<lambda()> >::_M_invoke(const std::_Any_data &) (
    __functor=...) at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:297
#38 0x00007fab230a68c0 in std::function<void ()>::operator()() const (this=0x7fab13e05970) at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/std_function.h:687
#39 0x00007fab2570397b in mongo::(anonymous namespace)::runFunc (ctx=0x7fab13e05970) at src/mongo/transport/service_entry_point_utils.cpp:56
#40 0x00007fab1e826dd5 in start_thread () from /lib64/libpthread.so.0
#41 0x00007fab1e55002d in clone () from /lib64/libc.so.6 
     */
    int64_t nextInt64() {
        int64_t r;
        _in->read(reinterpret_cast<char*>(&r), sizeof(r));
        if (_in->fail()) {
            error() << "InputStreamSecureRandom failed to generate random bytes";
            fassertFailed(28840);
        }
        return r;
    }

private:
    std::unique_ptr<std::ifstream> _in;
};

std::unique_ptr<SecureRandom> SecureRandom::create() {
    return stdx::make_unique<InputStreamSecureRandom>("/dev/urandom");
}

#elif defined(__OpenBSD__)

class Arc4SecureRandom : public SecureRandom {
public:
    int64_t nextInt64() {
        int64_t value;
        arc4random_buf(&value, sizeof(value));
        return value;
    }
};

std::unique_ptr<SecureRandom> SecureRandom::create() {
    return stdx::make_unique<Arc4SecureRandom>();
}

#else

#error Must implement SecureRandom for platform

#endif
}  // namespace mongo
