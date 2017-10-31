// -*- C++ -*- (c) 2016 Jan Mrázek <email@honzamrazek.cz>

#ifndef __DIOS_SYNC_SCHEDULING_H__
#define __DIOS_SYNC_SCHEDULING_H__

#include <dios/core/scheduling.hpp>

namespace __dios {

template < typename Next >
struct SyncScheduler : public Scheduler< Next >
{
    using Task = __dios::Task< typename Scheduler< Next >::Process >;

    template < typename Setup >
    void setup( Setup s ) {
        traceAlias< SyncScheduler >( "{Scheduler}" );
        s.proc1->globals = __vm_control( _VM_CA_Get, _VM_CR_Globals );
        s.proc1->pid = 1;

        _yield = false;

        _setupTask.reset(
            new_object < Task >( s.pool->get(), s.pool->get(), _start_synchronous, 0, s.proc1 ) );
        assert( _setupTask );
        auto argv = construct_main_arg( "arg.", s.env, true );
        auto envp = construct_main_arg( "env.", s.env );
        this->setupMainTask( _setupTask.get(), argv.first, argv.second, envp.second );

        __vm_control( _VM_CA_Set, _VM_CR_Scheduler, runScheduler< typename Setup::Context > );

        this->setupDebug( s, argv, envp );
        environ = envp.second;
        Next::setup( s );
    }

    void exit_process( int code ) {
        Scheduler< Next >::killTask( __dios_get_task_handle() );
    }

    Task* getCurrentTask() {
        if ( _setupTask )
            return _setupTask.get();
        auto tid = __dios_get_task_handle();
        return this->tasks.find( tid );
    }

    void yield() {
        _yield = true;
    }

    _DiOS_TaskHandle start_task( _DiOS_TaskRoutine routine, void * arg, int tls_size ) {
        if ( !_setupTask )
            __dios_fault( _VM_F_Control, "Cannot start task outside setup" );
        auto t = this->newTask( routine, tls_size, _setupTask->_proc );
        this->setupTask( t, arg );
        __vm_obj_shared( t->getId() );
        __vm_obj_shared( arg );
        return t->getId();
    }

    template < typename Context >
    __attribute__(( __always_inline__ )) static bool runTask( Context& ctx, Task& t ) noexcept {
        using Sys = Syscall< Context >;
        while ( t._frame ) {
            __vm_control( _VM_CA_Set, _VM_CR_User2,
                reinterpret_cast< int64_t >( t.getId() ) );
            auto self = __vm_control( _VM_CA_Get, _VM_CR_Frame );
            __vm_control( _VM_CA_Set, _VM_CR_IntFrame, self );
            __vm_control( _VM_CA_Set, _VM_CR_Frame, t._frame,
                          _VM_CA_Set, _VM_CR_Globals, t._proc->globals,
                          _VM_CA_Set, _VM_CR_User1, ctx.debug,
                          _VM_CA_Bit, _VM_CR_Flags,
                          uintptr_t( _VM_CF_Interrupted | _VM_CF_Mask | _VM_CF_KernelMode ), 0ull );
            t._frame = static_cast< _VM_Frame * >( __vm_control( _VM_CA_Get, _VM_CR_IntFrame ) );

            auto syscall = static_cast< _DiOS_Syscall * >( __vm_control( _VM_CA_Get, _VM_CR_User1 ) );
            __vm_control( _VM_CA_Set, _VM_CR_User1, ctx.debug );
            if ( syscall == reinterpret_cast< _DiOS_Syscall * >( ctx.debug ) ||
                 Sys::handle( ctx, *syscall ) == SchedCommand::RESCHEDULE )
            {
                if ( !ctx._yield )
                    continue;
                __dios_assert_v( !ctx._setupTask, "Cannot yield in setup" );
                ctx._yield = false;
                return false;
            }
        }
        return true;
    }

    template < typename Context >
    static void runScheduler() noexcept {
        void *ctx = __vm_control( _VM_CA_Get, _VM_CR_State );
        auto& scheduler = *static_cast< Context * >( ctx );

        __vm_control( _VM_CA_Set, _VM_CR_User1, scheduler.debug );

        scheduler.traceTasks();
        if ( scheduler._setupTask ) {
            runTask( scheduler, *scheduler._setupTask );
            scheduler._setupTask.reset( nullptr );
            return;
        }

        for ( auto& t : scheduler.tasks ) {
            while ( true ) {
                bool done = runTask( scheduler, *t );
                if ( !done )
                    break;
                t->setupFrame();
            }
        }

        scheduler.runMonitors();
        if ( scheduler.tasks.empty() ) {
            scheduler.finalize();
            __vm_control( _VM_CA_Bit, _VM_CR_Flags, _VM_CF_Cancel, _VM_CF_Cancel );
        }
    }

    std::unique_ptr< Task > _setupTask;
    bool _yield;
};

} // namespace __dios

#endif