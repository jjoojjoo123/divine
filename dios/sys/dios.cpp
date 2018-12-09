// -*- C++ -*- (c) 2016 Jan Mrázek <email@honzamrazek.cz>
//                 2016 Vladimir Still <xstill@fi.muni.cz>
//                 2016 Petr Rockai <me@mornfall.net>

#include <algorithm>
#include <unistd.h>

#include <sys/utsname.h>
#include <sys/bitcode.h>
#include <sys/metadata.h>

#include <dios.h>
#include <dios/sys/main.hpp>
#include <dios/sys/trace.hpp>
#include <dios/sys/config.hpp>

#include <dios/vfs/manager.h>

namespace __dios
{

std::string_view extractDiosConfiguration( SysOpts& o )
{
    auto r = std::find_if( o.begin(), o.end(),
        []( const auto& opt ) {
            return opt.first == "config";
        } );

    std::string_view cfg( "default" );
    if ( r != o.end() )
    {
        cfg = r->second;
        o.erase( r );
    }
    return cfg;
}

void traceHelpOption( int i, std::string_view opt, std::string_view desc,
                      const Array< std::string_view >& args )
{
    __dios_trace_i( i, "- %.*s: %.*s", int( opt.size() ), opt.begin(), int( desc.size() ), desc.begin() );
    __dios_trace_i( i, "  arguments:" );
    for ( const auto& arg : args )
        __dios_trace_i( i, "   - %.*s", int( arg.size() ), arg.begin() );
}

void traceHelp( int i, const ArrayMap< std::string_view, HelpOption >& help )
{
    for ( const auto& option : help )
        traceHelpOption( i, option.first, option.second.description,
                         option.second.options );
}

void traceEnv( int ind, const _VM_Env *env ) {
    __dios_trace_i( ind, "raw env options:" );
    for ( ; env->key; env++ ) {
        __dios_trace_i( ind + 1, "%s: \"%.*s\"", env->key, env->size, env->value );
    }
}

template < typename Configuration >
void boot( SetupBase sb )
{
    auto *context = new Configuration();
    __vm_trace( _VM_T_StateType, context );
    traceAlias< Configuration >( "{Context}" );
    __vm_ctl_set( _VM_CR_State, context );

    if ( extractOpt( "debug", "help", sb.opts ) )
    {
        ArrayMap< std::string_view, HelpOption > help;

        help.emplace( "config", HelpOption
                      { "run DiOS in a given configuration",
                           { "default: async threads, processes, vfs",
                             "passthrough: pass syscalls to the host OS",
                             "replay: re-use a trace recorded in passthrough mode",
                             "synchronous: for use with synchronous systems" } } );

        help.emplace( "debug", HelpOption
                      { "print debug information during boot",
                         { "help - help of selected configuration and exit",
                           // ToDo: trace binary blobs
                           /*"rawenvironment - user DiOS boot parameters",*/
                           "machineparams - specified by user, e.g. number of cpus",
                           "mainargs - argv and envp",
                           "faultcfg - fault and simfail configuration" } } );

        context->getHelp( help );
        traceHelp( 0, help );
        __vm_cancel();
        return;
    }

    if ( extractOpt( "debug", "rawenvironment", sb.opts ) )
    {
        traceEnv( 1, sb.env );
    }

    Setup< Configuration > s = sb;
    s.proc1 = new typename Configuration::Process();
    context->setup( s );
}

void temporaryScheduler()
{
    __vm_cancel();
}

void temporaryFaultHandler( _VM_Fault, _VM_Frame *, void (*)() )
{
    __vm_ctl_flag( 0, _VM_CF_Error );
    __vm_ctl_set( _VM_CR_Scheduler, nullptr );
    __dios_this_frame()->parent = nullptr;
}

void init( const _VM_Env *env )
{
    MemoryPool deterministicPool( 2 );

    __vm_ctl_set( _VM_CR_User1, nullptr );
    __vm_ctl_set( _VM_CR_FaultHandler, reinterpret_cast< void * >( temporaryFaultHandler ) );
    __vm_ctl_set( _VM_CR_Scheduler, reinterpret_cast< void * >( temporaryScheduler ) );

    SysOpts sysOpts;
    if ( !getSysOpts( env, sysOpts ) )
    {
        __vm_ctl_flag( 0, _VM_CF_Error );
        return;
    }

    auto cfg = extractDiosConfiguration( sysOpts );
    SetupBase setup{ .pool = &deterministicPool, .env = env, .opts = sysOpts };

    if ( cfg == "default" )
        return boot< config::Default >( setup );
//    if ( cfg == "passthrough" )
//        return boot< config::Passthrough >( setup );
//    if ( cfg == "replay" )
//        return boot< config::Replay >( setup );
    if ( cfg == "synchronous" )
        return boot< config::Sync >( setup );
    if ( cfg == "fair" )
        return boot< config::Fair >( setup );

    __dios_trace_f( "Unknown configuration: %.*s", int( cfg.size() ), cfg.begin() );
    __vm_ctl_flag( 0, _VM_CF_Error );
}

}

/*
 * DiOS entry point. Defined weak to allow user to redefine it.
 */
extern "C" void  __attribute__((weak)) __boot( const _VM_Env *env ) {
    __dios::init( env );
    __vm_suspend();
}
