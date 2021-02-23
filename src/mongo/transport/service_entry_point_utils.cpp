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

#include "mongo/transport/service_entry_point_utils.h"

#include "mongo/stdx/functional.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/log.h"

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

namespace mongo {

namespace {
void* runFunc(void* ctx) {
    std::unique_ptr<stdx::function<void()>> taskPtr(static_cast<stdx::function<void()>*>(ctx));
    (*taskPtr)();

    return nullptr;
}
}  // namespace

/*
 * 用户的连接通过这个函数创建对应的连接线程，这个函数的调用栈如下：
#0  mongo::launchServiceWorkerThread(std::function<void ()>) (task=...) at src/mongo/transport/service_entry_point_utils.cpp:62
#1  0x00007fca01e375b4 in mongo::transport::ServiceExecutorSynchronous::schedule(std::function<void ()>, mongo::transport::ServiceExecutor::ScheduleFlags, mongo::transport::ServiceExecutorTaskName) (this=0x7fc9f972f1a0, task=..., flags=mongo::transport::ServiceExecutor::kEmptyFlags, taskName=mongo::transport::kSSMStartSession)
    at src/mongo/transport/service_executor_synchronous.cpp:131
#2  0x00007fca019174f7 in mongo::ServiceStateMachine::_scheduleNextWithGuard (this=0x7fc9ee988f30, guard=..., flags=mongo::transport::ServiceExecutor::kEmptyFlags,
    taskName=mongo::transport::kSSMStartSession, ownershipModel=mongo::ServiceStateMachine::kStatic) at src/mongo/transport/service_state_machine.cpp:579
#3  0x00007fca01917270 in mongo::ServiceStateMachine::start (this=0x7fc9ee988f30, ownershipModel=mongo::ServiceStateMachine::kStatic)
    at src/mongo/transport/service_state_machine.cpp:558
#4  0x00007fca0190fab8 in mongo::ServiceEntryPointImpl::startSession (this=0x7fc9f972eca0, session=...) at src/mongo/transport/service_entry_point_impl.cpp:191
#5  0x00007fca01e3dc1a in mongo::transport::TransportLayerASIO::<lambda(const std::error_code&, mongo::transport::GenericSocket)>::operator()(const std::error_code &, mongo::transport::GenericSocket) (__closure=0x7fc9ef2c6680, ec=..., peerSocket=...) at src/mongo/transport/transport_layer_asio.cpp:905
#6  0x00007fca01e441b7 in asio::detail::move_binder2<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, std::error_code, asio::basic_stream_socket<asio::generic::stream_protocol> >::operator()(void) (this=0x7fc9ef2c6680)
    at src/third_party/asio-master/asio/include/asio/detail/bind_handler.hpp:665
#7  0x00007fca01e43c48 in asio::asio_handler_invoke<asio::detail::move_binder2<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, std::error_code, asio::basic_stream_socket<asio::generic::stream_protocol> > >(asio::detail::move_binder2<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, std::error_code, asio::basic_stream_socket<asio::generic::stream_protocol> > &, ...) (function=...)
    at src/third_party/asio-master/asio/include/asio/handler_invoke_hook.hpp:68
#8  0x00007fca01e436cd in asio_handler_invoke_helpers::invoke<asio::detail::move_binder2<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, std::error_code, asio::basic_stream_socket<asio::generic::stream_protocol> >, mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)> >(asio::detail::move_binder2<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, std::error_code, asio::basic_stream_socket<asio::generic::stream_protocol> > &, mongo::transport::TransportLayerASIO::<lambda(const std::error_code&, mongo::transport::GenericSocket)> &) (function=..., context=...) at src/third_party/asio-master/asio/include/asio/detail/handler_invoke_helpers.hpp:37
#9  0x00007fca01e42fbf in asio::detail::handler_work<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, asio::system_executor>::complete<asio::detail::move_binder2<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, std::error_code, asio::basic_stream_socket<asio::generic::stream_protocol> > >(asio::detail::move_binder2<mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)>, std::error_code, asio::basic_stream_socket<asio::generic::stream_protocol> > &, mongo::transport::TransportLayerASIO::<lambda(const std::error_code&, mongo::transport::GenericSocket)> &) (this=0x7fc9ef2c6656, function=..., handler=...)
    at src/third_party/asio-master/asio/include/asio/detail/handler_work.hpp:81
#10 0x00007fca01e4255f in asio::detail::reactive_socket_move_accept_op<asio::generic::stream_protocol, mongo::transport::TransportLayerASIO::_acceptConnection(mongo::transport::TransportLayerASIO::GenericAcceptor&)::<lambda(const std::error_code&, mongo::transport::GenericSocket)> >::do_complete(void *, asio::detail::operation *, const asio::error_code &, std::size_t) (owner=0x7fc9f972f060, base=0x7fc9f5637e40) at src/third_party/asio-master/asio/include/asio/detail/reactive_socket_accept_op.hpp:201
#11 0x00007fca01eacda4 in asio::detail::scheduler_operation::complete (this=0x7fc9f5637e40, owner=0x7fc9f972f060, ec=..., bytes_transferred=0)
    at src/third_party/asio-master/asio/include/asio/detail/scheduler_operation.hpp:39
#12 0x00007fca01e9ced7 in asio::detail::epoll_reactor::descriptor_state::do_complete (owner=0x7fc9f972f060, base=0x7fc9f973d720, ec=..., bytes_transferred=1)
    at src/third_party/asio-master/asio/include/asio/detail/impl/epoll_reactor.ipp:775
---Type <return> to continue, or q <return> to quit---
#13 0x00007fca01eacda4 in asio::detail::scheduler_operation::complete (this=0x7fc9f973d720, owner=0x7fc9f972f060, ec=..., bytes_transferred=1)
    at src/third_party/asio-master/asio/include/asio/detail/scheduler_operation.hpp:39
#14 0x00007fca01ea0740 in asio::detail::scheduler::do_run_one (this=0x7fc9f972f060, lock=..., this_thread=..., ec=...)
    at src/third_party/asio-master/asio/include/asio/detail/impl/scheduler.ipp:400
#15 0x00007fca01e9f8df in asio::detail::scheduler::run (this=0x7fc9f972f060, ec=...) at src/third_party/asio-master/asio/include/asio/detail/impl/scheduler.ipp:153
#16 0x00007fca01e988a8 in asio::io_context::run (this=0x7fc9f9ab55f8) at src/third_party/asio-master/asio/include/asio/impl/io_context.ipp:61
#17 0x00007fca01e4f18b in mongo::transport::TransportLayerASIO::ASIOReactor::run (this=0x7fc9f9ab55f0) at src/mongo/transport/transport_layer_asio.cpp:147
#18 0x00007fca01e3d32a in mongo::transport::TransportLayerASIO::_runListener (this=0x7fc9f9b53aa0) at src/mongo/transport/transport_layer_asio.cpp:809
#19 0x00007fca01e3d57b in mongo::transport::TransportLayerASIO::<lambda()>::operator()(void) const (__closure=0x7fc9f98ba260)
    at src/mongo/transport/transport_layer_asio.cpp:836
#20 0x00007fca01e4171f in std::__invoke_impl<void, mongo::transport::TransportLayerASIO::start()::<lambda()> >(std::__invoke_other, <unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12af83c5>) (__f=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12af83c5>)
    at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/invoke.h:60
#21 0x00007fca01e40a8b in std::__invoke<mongo::transport::TransportLayerASIO::start()::<lambda()> >(<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b08b1f>) (__fn=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b08b1f>) at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/invoke.h:95
#22 0x00007fca01e3f095 in std::__apply_impl<mongo::transport::TransportLayerASIO::start()::<lambda()>, std::tuple<> >(<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b329>, <unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b339>, std::index_sequence) (
    __f=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b329>,
    __t=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b339>) at /opt/rh/devtoolset-8/root/usr/include/c++/8/tuple:1678
#23 0x00007fca01e3f0cf in std::apply<mongo::transport::TransportLayerASIO::start()::<lambda()>, std::tuple<> >(<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b2d1>, <unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b2e1>) (
    __f=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b2d1>,
    __t=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b1b2e1>) at /opt/rh/devtoolset-8/root/usr/include/c++/8/tuple:1687
#24 0x00007fca01e3f135 in mongo::stdx::thread::<lambda()>::operator()(void) (this=0x7fc9f98ba258) at src/mongo/stdx/thread.h:172
#25 0x00007fca01e4184b in std::__invoke_impl<void, mongo::stdx::thread::thread(Function&&, Args&& ...) [with Function = mongo::transport::TransportLayerASIO::start()::<lambda()>; Args = {}; typename std::enable_if<(! std::is_same<mongo::stdx::thread, typename std::decay<_Tp>::type>::value), int>::type <anonymous> = 0]::<lambda()> >(std::__invoke_other, <unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12af8006>) (__f=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12af8006>)
    at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/invoke.h:60
#26 0x00007fca01e40abc in std::__invoke<mongo::stdx::thread::thread(Function&&, Args&& ...) [with Function = mongo::transport::TransportLayerASIO::start()::<lambda()>; Args = {}; typename std::enable_if<(! std::is_same<mongo::stdx::thread, typename std::decay<_Tp>::type>::value), int>::type <anonymous> = 0]::<lambda()> >(<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b08aa7>) (__fn=<unknown type in /root/mongo/drewryz/mongo/mongos, CU 0x12998760, DIE 0x12b08aa7>)
    at /opt/rh/devtoolset-8/root/usr/include/c++/8/bits/invoke.h:95
#27 0x00007fca01e4500c in std::thread::_Invoker<std::tuple<mongo::stdx::thread::thread(Function&&, Args&& ...) [with Function = mongo::transport::TransportLayerASIO::start()::<lambda()>; Args = {}; typename std::enable_if<(! std::is_same<mongo::stdx::thread, typename std::decay<_Tp>::type>::value), int>::type <anonymous> = 0]::<lambda()> > >::_M_invoke<0>(std::_Index_tuple<0>) (this=0x7fc9f98ba258) at /opt/rh/devtoolset-8/root/usr/include/c++/8/thread:244
#28 0x00007fca01e44f1c in std::thread::_Invoker<std::tuple<mongo::stdx::thread::thread(Function&&, Args&& ...) [with Function = mongo::transport::TransportLayerASIO::start()::<lambda()>; Args = {}; typename std::enable_if<(! std::is_same<mongo::stdx::thread, typename std::decay<_Tp>::type>::value), int>::type <anonymous> = 0]::<lambda()> > >::operator()(void) (this=0x7fc9f98ba258) at /opt/rh/devtoolset-8/root/usr/include/c++/8/thread:253
#29 0x00007fca01e44ec0 in std::thread::_State_impl<std::thread::_Invoker<std::tuple<mongo::stdx::thread::thread(Function&&, Args&& ...) [with Function = mongo::transport::Trans---Type <return> to continue, or q <return> to quit---
portLayerASIO::start()::<lambda()>; Args = {}; typename std::enable_if<(! std::is_same<mongo::stdx::thread, typename std::decay<_Tp>::type>::value), int>::type <anonymous> = 0]::<lambda()> > > >::_M_run(void) (this=0x7fc9f98ba250) at /opt/rh/devtoolset-8/root/usr/include/c++/8/thread:196
#30 0x00007fc9fedf1630 in execute_native_thread_routine () at ../../../../../libstdc++-v3/src/c++11/thread.cc:80
#31 0x00007fc9fe5fcdd5 in start_thread () from /lib64/libpthread.so.0
#32 0x00007fc9fe32602d in clone () from /lib64/libc.so.6
 */
/*
 * 该函数既会被ServiceExecutorSynchronou调用，又会被ServiceExecutorAdaptive调用
 * 整体说来这个函数创建一个新的线程，然后从task开始
 */
Status launchServiceWorkerThread(stdx::function<void()> task) {

    try {
#if defined(_WIN32)
        stdx::thread(std::move(task)).detach();
#else
        pthread_attr_t attrs;
        pthread_attr_init(&attrs);
        pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);

        static const rlim_t kStackSize =
            1024 * 1024;  // if we change this we need to update the warning

        struct rlimit limits;
        invariant(getrlimit(RLIMIT_STACK, &limits) == 0);
        if (limits.rlim_cur > kStackSize) {
            size_t stackSizeToSet = kStackSize;
#if !__has_feature(address_sanitizer)
            if (kDebugBuild)
                stackSizeToSet /= 2;
#endif
            int failed = pthread_attr_setstacksize(&attrs, stackSizeToSet);
            if (failed) {
                const auto ewd = errnoWithDescription(failed);
                warning() << "pthread_attr_setstacksize failed: " << ewd;
            }
        } else if (limits.rlim_cur < 1024 * 1024) {
            warning() << "Stack size set to " << (limits.rlim_cur / 1024) << "KB. We suggest 1MB";
        }

        // Wrap the user-specified `task` so it runs with an installed `sigaltstack`.
        task = [sigAltStackController = std::make_shared<stdx::support::SigAltStackController>(),
                f = std::move(task)] {
            auto sigAltStackGuard = sigAltStackController->makeInstallGuard();
            f();
        };

        pthread_t thread;
        auto ctx = stdx::make_unique<stdx::function<void()>>(std::move(task));
        /*
         * 默认情况下，使用thread per connection模型，这里就是创建连接线程的地方  
         */
        int failed = pthread_create(&thread, &attrs, runFunc, ctx.get());

        pthread_attr_destroy(&attrs);

        if (failed) {
            log() << "pthread_create failed: " << errnoWithDescription(failed);
            throw std::system_error(
                std::make_error_code(std::errc::resource_unavailable_try_again));
        }

        ctx.release();
#endif

    } catch (...) {
        return {ErrorCodes::InternalError, "failed to create service entry worker thread"};
    }

    return Status::OK();
}

}  // namespace mongo
