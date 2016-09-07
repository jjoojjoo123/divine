// -*- C++ -*- (c) 2016 Vladimír Štill

#include <divine/cc/compile.hpp>
#include <divine/rt/runtime.hpp>

DIVINE_RELAX_WARNINGS
#include <llvm/Support/raw_os_ostream.h>
#include <brick-llvm>
DIVINE_UNRELAX_WARNINGS

#include <brick-fs>
#include <brick-string>

namespace divine {
namespace cc {

using brick::fs::joinPath;

static std::string getWrappedMDS( llvm::NamedMDNode *meta, int i = 0, int j = 0 ) {
    auto *op = llvm::cast< llvm::MDTuple >( meta->getOperand( i ) );
    auto *str = llvm::cast< llvm::MDString >( op->getOperand( j ).get() );
    return str->getString().str();
}

struct MergeFlags_ { // hide in clas so they can be mutually recursive
    void operator()( std::vector< std::string > & ) { }

    template< typename ... Xs >
    void operator()( std::vector< std::string > &out, std::string first, Xs &&... xs ) {
        out.emplace_back( std::move( first ) );
        (*this)( out, std::forward< Xs >( xs )... );
    }

    template< typename C, typename ... Xs >
    auto operator()( std::vector< std::string > &out, C &&c, Xs &&... xs )
        -> decltype( void( (c.begin(), c.end()) ) )
    {
        out.insert( out.end(), c.begin(), c.end() );
        (*this)( out, std::forward< Xs >( xs )... );
    }
};

template< typename ... Xs >
static std::vector< std::string > mergeFlags( Xs &&... xs ) {
    std::vector< std::string > out;
    MergeFlags_()( out, std::forward< Xs >( xs )... );
    return out;
}

Compile::Compile( Options opts ) :
    opts( opts ), compilers( 1 ), workers( 1 ), linker( new brick::llvm::Linker() )
{
    ASSERT_LEQ( 1ul, workers.size() );
    ASSERT_EQ( workers.size(), compilers.size() );

    commonFlags = { "-D__divine__"
                  , "-isystem", rt::includeDir
                  , "-isystem", joinPath( rt::includeDir, "pdclib" )
                  , "-isystem", joinPath( rt::includeDir, "libm" )
                  , "-isystem", joinPath( rt::includeDir, "libcxx/include" )
                  , "-isystem", joinPath( rt::includeDir, "libunwind/include" )
                  , "-D_POSIX_C_SOURCE=2008098L"
                  , "-D_LITTLE_ENDIAN=1234"
                  , "-D_BYTE_ORDER=1234"
                  , "-g"
                  };
}

Compile::~Compile() { }

void Compile::compileAndLink( std::string path, std::vector< std::string > flags )
{
    linker->link( compile( path, flags ) );
}

std::unique_ptr< llvm::Module > Compile::compile( std::string path,
                                    std::vector< std::string > flags )
{
    std::vector< std::string > allFlags;
    std::copy( commonFlags.begin(), commonFlags.end(), std::back_inserter( allFlags ) );
    std::copy( flags.begin(), flags.end(), std::back_inserter( allFlags ) );

    std::cerr << "compiling " << path << std::endl;
    if ( path[0] == '/' )
        mastercc().allowIncludePath( std::string( path, 0, path.rfind( '/' ) ) );
    auto mod = mastercc().compileModule( path, allFlags );

    return mod;
}

llvm::Module *Compile::getLinked() {
    return linker->get();
}

void Compile::writeToFile( std::string filename ) {
    writeToFile( filename, getLinked() );
}

void Compile::writeToFile( std::string filename, llvm::Module *mod )
{
    llvm::raw_os_ostream cerr( std::cerr );
    if ( llvm::verifyModule( *mod, &cerr ) ) {
        cerr.flush(); // otherwise nothing will be displayed
        UNREACHABLE( "invalid bitcode" );
    }
    std::error_code serr;
    ::llvm::raw_fd_ostream outs( filename, serr, ::llvm::sys::fs::F_None );
    WriteBitcodeToFile( mod, outs );
}

std::string Compile::serialize() {
    return mastercc().serializeModule( *getLinked() );
}

void Compile::addDirectory( std::string path ) {
    mastercc().allowIncludePath( path );
}

void Compile::addFlags( std::vector< std::string > flags ) {
    std::copy( flags.begin(), flags.end(), std::back_inserter( commonFlags ) );
}

void Compile::prune( std::vector< std::string > r ) {
    linker->prune( r, brick::llvm::Prune::UnusedModules );
}

std::shared_ptr< llvm::LLVMContext > Compile::context() { return mastercc().context(); }

Compiler &Compile::mastercc() { return compilers[0]; }

void Compile::setupLib( std::string name, const std::string &content )
{
    using brick::string::startsWith;

    rt::each(
        [&]( auto path, auto c )
        {
            if ( startsWith( path, joinPath( rt::srcDir, "filesystem" ) ) )
                return; /* ignore */
            mastercc().mapVirtualFile( path, c );
        } );
}

void Compile::setupLibs() {
    if ( opts.precompiled.size() ) {
        auto input = std::move( llvm::MemoryBuffer::getFile( opts.precompiled ).get() );
        ASSERT( !!input );

        auto inputData = input->getMemBufferRef();
        auto parsed = parseBitcodeFile( inputData, *context() );
        if ( !parsed )
            throw std::runtime_error( "Error parsing input model; probably not a valid bitcode file." );
        if ( getRuntimeVersionSha( *parsed.get() ) != DIVINE_RUNTIME_SHA )
            std::cerr << "WARNING: runtime version of the precompiled library does not match the current runtime version"
                      << std::endl;
        linker->load( std::move( parsed.get() ) );
    } else {
        auto libflags = []( auto... xs ) {
            return mergeFlags( "-Wall", "-Wextra", "-Wno-gcc-compat", "-Wno-unused-parameter", xs... );
        };
        std::initializer_list< std::string > cxxflags =
            { "-std=c++14"
              // , "-fstrict-aliasing"
              , "-I", joinPath( rt::includeDir, "libcxxabi/include" )
              , "-I", joinPath( rt::includeDir, "libcxxabi/src" )
              , "-I", joinPath( rt::includeDir, "libcxx/src" )
              , "-I", joinPath( rt::includeDir, "filesystem" )
              , "-Oz" };
        compileLibrary( joinPath( rt::srcDir, "pdclib" ), libflags( "-D_PDCLIB_BUILD" ) );
        compileLibrary( joinPath( rt::srcDir, "limb" ), libflags() );
        compileLibrary( joinPath( rt::srcDir, "libcxxabi" ),
                        libflags( cxxflags, "-DLIBCXXABI_USE_LLVM_UNWINDER" ) );
        compileLibrary( joinPath( rt::srcDir, "libcxx" ), libflags( cxxflags ) );
        compileLibrary( joinPath( rt::srcDir, "divine" ), libflags( cxxflags ) );
        compileLibrary( joinPath( rt::srcDir, "dios" ), libflags( cxxflags ) );
        compileLibrary( joinPath( rt::srcDir, "filesystem" ), libflags( cxxflags ) );
        compileLibrary( joinPath( rt::srcDir, "lart" ), libflags( cxxflags ) );
    }
}

void Compile::compileLibrary( std::string path, std::vector< std::string > flags )
{
    for ( const auto &f : mastercc().filesMappedUnder( path ) )
        compileAndLink( f, flags );
}
}
}
