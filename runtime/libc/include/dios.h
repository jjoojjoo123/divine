// -*- C++ -*- (c) 2016 Jan Mrázek <email@honzamrazek.cz>

#ifndef __DIOS_H__
#define __DIOS_H__

#include <sys/divm.h>
#include <sys/types.h>
#include <sys/trace.h>
#include <sys/fault.h>
#include <sys/interrupt.h>
#include <sys/monitor.h>

#ifdef __cplusplus
#define EXTERN_C extern "C" {
#define CPP_END }
#if __cplusplus >= 201103L
#define NOTHROW noexcept
#else
#define NOTHROW throw()
#endif
#else
#define EXTERN_C
#define CPP_END
#define NOTHROW __attribute__((__nothrow__))
#endif

EXTERN_C

#include <stddef.h>

struct _DiOS_TLS {
    int _errno;
    char data[ 0 ];
};

typedef struct _DiOS_TLS * _DiOS_ThreadHandle;

static inline int __dios_pointer_get_type( void *ptr ) NOTHROW
{
    unsigned long p = (unsigned long) ptr;
    return ( p & _VM_PM_Type ) >> _VM_PB_Off;
}

static inline void *__dios_pointer_set_type( void *ptr, int type ) NOTHROW
{
    unsigned long p = (unsigned long) ptr;
    p &= ~_VM_PM_Type;
    unsigned long newt = ( type << _VM_PB_Off ) & _VM_PM_Type;
    return (void *)( p | newt );
}

/*
 * Start a new thread and obtain its identifier. Thread starts executing routine
 * with arg.
 * - tls_size is the total size of TLS, _DiOS_TLS_Reserved must be included in this,
 *   if tls_size is less then _DiOS_TLS_Reserved at least _DiOS_TLS_Reserved is allocated
 * - the resulting _DiOS_ThreadHandle points to the beginning of TLS. Userspace is
 *   allowed to use it from offset _DiOS_TLS_Reserved
 */
_DiOS_ThreadHandle __dios_start_thread( void ( *routine )( void * ), void *arg, int tls_size ) NOTHROW;

/*
 * Get caller thread id
 *
 * - the resulting _DiOS_ThreadHandle points to the beginning of TLS. Userspace is
 *   allowed to use it from offset _DiOS_TLS_Reserved
 */
_DiOS_ThreadHandle __dios_get_thread_handle() NOTHROW;

/*
 * get pointer to errno, which is in dios-managed thread-local data (accessible
 * to userspace, but independent of pthreading library)
 */
int *__dios_get_errno() NOTHROW;

/*
 * Kill thread with given id.
 */
void __dios_kill_thread( _DiOS_ThreadHandle id ) NOTHROW;

/*
 * Kill process with given id. If NULL is passed, all processes are killed.
 */
void __dios_kill_process( pid_t id ) NOTHROW;


_DiOS_ThreadHandle *__dios_get_process_threads() NOTHROW;

/*
 * Return number of claimed hardware concurrency units, specified in DiOS boot
 * parameters.
 */
int __dios_hardware_concurrency() NOTHROW;

/*
 * Issue DiOS syscall with given args. Return value is stored in ret.
 */
void __dios_syscall(int syscode, void* ret, ...) NOTHROW;


// unwind and free frames on stack 'stack' from 'from' to 'to' so that 'to'
// the frame which originally returned to 'from' now returns to 'to'
// * 'stack' can be nullptr if unwinding on local stack
// * 'from' can be nullptr if everything from the caller of __dios_unwind should be unwound
// * 'to' can be nullptr if all frames from 'from' below should be destroyed
//
// i.e. __dios_unwind( nullptr, nullptr, nullptr ) destroys complete stack
// except for the caller of __dios_unwind, which will have 'parent' set to
// nullptr
void __dios_unwind( struct _VM_Frame *stack, struct _VM_Frame *from, struct _VM_Frame *to ) NOTHROW __attribute__((__noinline__));

// transfer control to given frame and program counter, if restoreMaskTo is -1
// it does not change mask
void __dios_jump( struct _VM_Frame *to, void (*pc)( void ), int restoreMaskTo ) NOTHROW __attribute__((__noinline__));

#define __dios_assert_v( x, msg ) do { \
        if ( !(x) ) { \
            __dios_trace( 0, "DiOS assert failed at %s:%d: %s", \
                __FILE__, __LINE__, msg ); \
            __dios_fault( (_VM_Fault) _DiOS_F_Assert, "DiOS assert failed" ); \
        } \
    } while (0)

#define __dios_assert( x ) do { \
        if ( !(x) ) { \
            __dios_trace( 0, "DiOS assert failed at %s:%d", \
                __FILE__, __LINE__ ); \
            __dios_fault( (_VM_Fault) _DiOS_F_Assert, "DiOS assert failed" ); \
        } \
    } while (0)

CPP_END


#ifdef __cplusplus
#if defined( __divine__ ) || defined( DIVINE_NATIVE_RUNTIME )

#include <cstdint>
#include <dios/core/stdlibwrap.hpp>

namespace __dios {

namespace fs {
    struct VFS;
}

template < class T, class... Args >
T *new_object( Args... args ) {
    T* obj = static_cast< T * >( __vm_obj_make( sizeof( T ) ?: 1 ) );
    new (obj) T( args... );
    return obj;
}

template < class T >
void delete_object( T *obj ) {
    obj->~T();
    __vm_obj_free( obj );
}

using SysOpts = Vector< std::pair< String, String > >;

struct Scheduler;
struct Syscall;
struct Fault;
using VFS = fs::VFS;

struct MachineParams {
    int hardwareConcurrency;

    void initialize( const SysOpts& opts );
    void traceParams( int indent );
};

typedef void ( *sighandler_t )( int );

struct Context {
    Scheduler *scheduler;
    Fault *fault;
    VFS *vfs;
    void *globals;
    Monitor *monitors;
    MachineParams machineParams;
    sighandler_t *sighandlers;

    Context();
    void finalize();
};


} // namespace __dios

#endif // __divine__ || DIVINE_NATIVE_RUNTIME

#endif // __cplusplus

#endif // __DIOS_H__

#undef EXTERN_C
#undef CPP_END
#undef NOTHROW
