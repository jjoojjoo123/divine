// -*- C++ -*- (c) 2016 Jan Mrázek <email@honzamrazek.cz>

#ifndef __DIOS_SYSCALL_H__
#define __DIOS_SYSCALL_H__

#include <cstdarg>
#include <new>
#include <dios.h>
#include <errno.h>
#include <dios/kernel.hpp>
#include <sys/syscall.h>

// Syscall argument types
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/monitor.h>
#include <signal.h>

namespace __dios {

#define SYSCALL(num,name) name = num,
enum _VM_SC {
    #include <dios/core/systable.def>
};
#undef SYSCALL

template < typename Context >
struct Syscall
{
    using ScHandler = SchedCommand (*)( Context& c, void *, va_list );

    static SchedCommand handle( Context& c, _DiOS_Syscall& s ) noexcept {
        if ( s._syscode != SYS_NONE ) {
            auto cmd = ( *( table[ s._syscode ] ) )( c, s._ret, s._args );
            s._syscode = SYS_NONE;
            if ( __dios_get_errno() && *__dios_get_errno() == EAGAIN2 )
                return SchedCommand::RESCHEDULE;
            return cmd;
        }
        return SchedCommand::RESCHEDULE;
    }

    static void kernelHandle( void *ctx, _DiOS_SC syscode, void *ret, va_list vl ) noexcept {
        Context& c = *static_cast< Context* >( ctx );
        ( *( table[ syscode ] ) )( c, ret, vl );
    }

    static ScHandler table[ SYS_MAXSYSCALL ];

    #include <dios/macro/syscall_common>
    #include <dios/macro/no_memory_tags>
    #define SYSCALL( name, schedule, ret, arg ) \
        static SchedCommand name ## Wrappper( Context& ctx, void *retVal, va_list vl) { \
            UNPACK arg \
            IF(IS_VOID(ret))( \
                ctx. name ( ARG_NAMES arg ); \
            ) \
            IF(NOT(IS_VOID(ret))) ( \
                auto *r = static_cast< ret * >( retVal ); \
                *r = ctx. name ( ARG_NAMES arg ); \
            ) \
            va_end( vl ); \
            return SchedCommand:: schedule; \
        }
    #define SYSCALLSEP( ... ) EVAL( SYSCALL( __VA_ARGS__ ) )

        #include <sys/syscall.def>

    #undef SYSCALLSEP
    #undef SYSCALL
    #include <dios/macro/no_memory_tags.cleanup>
    #include <dios/macro/syscall_common.cleanup>
};

template < typename Context >
typename Syscall< Context >::ScHandler Syscall< Context >::table[ SYS_MAXSYSCALL ] = {
    #define SYSCALL( name, ... ) [ SYS_ ## name ] = Syscall:: name ## Wrappper,
    #define SYSCALLSEP( ... ) EVAL( SYSCALL( __VA_ARGS__ ) )
        #include <sys/syscall.def>
    #undef SYSCALL
    #undef SYSCALLSEP
};

struct BaseContext
{
    using SyscallInvoker = void (*)( void *, _DiOS_SC syscode, void *, va_list );

    struct Process {};

    void linkSyscall( SyscallInvoker invoker ) {
        _kernelCall = invoker;
    };

    void setup( MemoryPool&, const _VM_Env *, SysOpts opts ) {
        if ( opts.empty() )
            return;
        for ( const auto& opt : opts )
            __dios_trace_f( "ERROR: Unused option %s:%s", opt.first.c_str(), opt.second.c_str() );
        __dios_fault( _DiOS_F_Config, "Unused options" );
    }

    void finalize() {}

    void kernelSyscall( _DiOS_SC syscode, void *ret, ... ) {
        va_list vl;
        va_start( vl, ret );
        _kernelCall( static_cast< void * >( this ), syscode, ret, vl );
        va_end( vl );
    }

    static void kernelSyscall( void *c, _DiOS_SC syscode, void *ret, ... ) {
        va_list vl;
        va_start( vl, ret );
        auto ctx = static_cast< BaseContext *>( c );
        ctx->_kernelCall( ctx, syscode, ret, vl );
        va_end( vl );
    }

    SyscallInvoker _kernelCall;

    #include <dios/macro/syscall_common>
    #include <dios/macro/no_memory_tags>
    #define SYSCALL( name, schedule, ret, arg ) \
        ret name ( NAMED_ARGS arg );
    #define SYSCALLSEP( ... ) EVAL( SYSCALL( __VA_ARGS__ ) )

        #include <sys/syscall.def>

    #undef SYSCALL
    #undef SYSCALLSEP
    #include <dios/macro/no_memory_tags.cleanup>
    #include <dios/macro/syscall_common.cleanup>
};

} // namespace __dios


#endif // __DIOS_SYSCALL_H__
