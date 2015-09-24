#include <lart/aa/pass.h>
#include <lart/abstract/pass.h>
#include <lart/interference/pass.h>
#include <lart/weakmem/pass.h>
#include <lart/interrupt/pass.h>
#include <lart/support/composite.h>
#include <lart/paropt/pass.h>

#include <iostream>
#include <cassert>
#include <stdexcept>

#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>

#include <llvm/Bitcode/ReaderWriter.h>

#include <llvm/IR/Verifier.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>

#include <lart/support/meta.h>

using namespace llvm;
using namespace lart;

typedef std::vector< std::pair< std::string, std::string > > PassOpts;

PassOpts parse( char **pass )
{
    PassOpts r;
    while ( *pass ) {
        std::string p( *pass ), name( p, 0, p.find( ':' ) ), opt;

        if ( p.find( ':' ) != std::string::npos )
            opt = std::string( p, p.find( ':' ) + 1, std::string::npos );

        std::cerr << "setting up pass: " << name << ", options = " << opt << std::endl;
        r.push_back( std::make_pair( name, opt ) );
        ++ pass;
    }
    return r;
}

std::vector< PassMeta > passes() {
    return { weakmem::meta(), paropt::meta(), interrupt::meta() };
}

void addPass( ModulePassManager &mgr, std::string n, std::string opt )
{
    for ( auto pass : passes() ) {
        if ( pass.select( mgr, n, opt ) )
            return;
    }

    if ( n == "aa" ) {
        aa::Pass::Type t;

        if ( opt == "andersen" )
            t = aa::Pass::Andersen;
        else
            throw std::runtime_error( "unknown alias-analysis type: " + opt );

        mgr.addPass( aa::Pass( t ) );
    }

    if ( n == "abstract" ) {
        abstract::Pass::Type t;

        if ( opt == "interval" )
            t = abstract::Pass::Interval;
        else if ( opt == "trivial" )
            t = abstract::Pass::Trivial;
        else
            throw std::runtime_error( "unknown abstraction type: " + opt );

        mgr.addPass( abstract::Pass( t ) );
    }

    if ( n == "interference" )
        mgr.addPass( interference::Pass() );

}

void process( Module *m, PassOpts opt )
{
    ModulePassManager manager;

    for ( auto i : opt )
        addPass( manager, i.first, i.second );

    manager.run( *m );
}

int main( int argc, char **argv )
{
    if ( argc < 3 ) {
        std::cerr << "usage: lart in.bc out.bc [pass:options] ..." << std::endl
                  << std::endl
                  << "pass aa (alias analysis), options:" << std::endl
                  << "    andersen: andersen-style, flow- and context-insensitive" << std::endl
                  << std::endl
                  << "example: lart in.bc out.bc aa:andersen" << std::endl << std::endl;
        for ( auto pass : passes() ) {
            pass.description( std::cerr );
            std::cerr << std::endl;
        }
        return 1;
    }

    const char *from = argv[1], *to = argv[2];
    assert( from );
    assert( to );

    LLVMContext *ctx = new LLVMContext();

    std::unique_ptr< MemoryBuffer > input;
    input = std::move( MemoryBuffer::getFile( from ).get() );
    assert( input );

    Module *module;
    auto parsed = parseBitcodeFile( input->getMemBufferRef(), *ctx );
    if ( !parsed )
        throw std::runtime_error( "Error parsing input model; probably not a valid bitcode file." );
    module = parsed.get().get();

    std::error_code serr;
    ::llvm::raw_fd_ostream outs( to, serr, ::llvm::sys::fs::F_None );

    PassOpts passes = parse( argv + 3 );
    process( module, passes );
    llvm::verifyModule( *module );

    WriteBitcodeToFile( module, outs );
}
