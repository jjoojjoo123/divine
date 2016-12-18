// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2016 Petr Ročkai <code@fixp.eu>
 * (c) 2016 Viktória Vozárová <>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <divine/ui/cli.hpp>
#include <divine/vm/bitcode.hpp>
#include <divine/vm/run.hpp>
#include <divine/cc/compile.hpp>
#include <divine/rt/runtime.hpp>
#include <brick-string>

#include <runtime/divine/stat.h>

DIVINE_RELAX_WARNINGS
#include <brick-llvm>
DIVINE_UNRELAX_WARNINGS

namespace divine {
namespace ui {

namespace {

using namespace brick::string;
using bstr = std::vector< uint8_t >;
using seenType = std::set< std::string >;

bstr readContent( const std::string& path, const struct stat& s ) {
    if ( S_ISLNK( s.st_mode ) ) {
        bstr content( 1024 );
        ssize_t ret;
        while ( ( ret = readlink( path.c_str(),
            reinterpret_cast< char * >( content.data() ), content.size() ) ) < 1 )
        {
            if ( errno != ENAMETOOLONG )
                die( "Cannot read content of symlink " + path );
            content.resize( 2 * content.size() );
        }
        content.resize( ret );
        return content;
    }
    else if ( S_ISREG( s.st_mode ) ) {
        std::ifstream f( path, std::ios::binary );
        bstr content( (std::istreambuf_iterator< char >( f )),
                      std::istreambuf_iterator< char >() );
        return content;
    }
    else if ( S_ISDIR( s.st_mode ) ) {
        return {};
    }
    die( "Capturing of sockets, devices and FIFOs is not supported" );
    __builtin_unreachable();
}

bstr packStat( const struct stat& s ) {
    bstr ret( sizeof( _DivineStat ) );
    auto *ss = reinterpret_cast< _DivineStat * >( ret.data() );
    ss->st_dev = s.st_dev;
    ss->st_ino = s.st_ino;
    ss->st_mode = s.st_mode;
    ss->st_nlink = s.st_nlink;
    ss->st_uid = s.st_uid;
    ss->st_gid = s.st_gid;
    ss->st_rdev = s.st_rdev;
    ss->st_size = s.st_size;
    ss->st_blksize = s.st_blksize;
    ss->st_blocks = s.st_blocks;

    ss->st_atim_tv_sec = s.st_atim.tv_sec;
    ss->st_atim_tv_nsec = s.st_atim.tv_nsec;

    ss->st_mtim_tv_sec = s.st_mtim.tv_sec;
    ss->st_mtim_tv_nsec = s.st_mtim.tv_nsec;

    ss->st_ctim_tv_sec = s.st_ctim.tv_sec;
    ss->st_ctim_tv_nsec = s.st_ctim.tv_nsec;

    return ret;
}

bool visited( const seenType& s, const std::string& path ) {
    return std::find( s.begin(), s.end(), path ) != s.end();
}

std::string changePathPrefix( const std::string& path, const std::string& oldPref,
    const std::string& newPref )
{
    auto p = brick::fs::splitPath( path );
    auto o = brick::fs::splitPath( oldPref );
    if ( !o.empty() && o.back().empty() )
        o.pop_back();
    std::vector< std::string > suf( p.begin() + o.size(), p.end() );
    return brick::fs::joinPath( newPref, brick::fs::joinPath(suf) );
}

std::string noPrefixChange( const std::string& s ) {
    return s;
}

template < typename MountPath, typename See, typename Seen, typename Count, typename Limit >
bool explore( bool follow, MountPath mountPath, See see, Seen seen, Count count,
    Limit limit, vm::BitCode::Env& env, const std::string& oPath )
{
    auto stat = brick::fs::lstat( oPath );
    auto cont = readContent( oPath, *stat );
    auto pStat = packStat( *stat );
    auto path = mountPath( oPath );
    auto iCount = count();

    if ( seen( path ) )
        return false;
    see( path );

    env.emplace_back( "vfs." + fmt( iCount ) + ".name", bstr( path.begin(), path.end() ) );
    env.emplace_back( "vfs." + fmt( iCount ) + ".stat", pStat );
    env.emplace_back( "vfs." + fmt( iCount ) + ".content", cont );
    limit( cont.size() );

    if ( S_ISLNK( stat->st_mode ) ) {
        std::string symPath ( cont.begin(), cont.end() );
        bool absolute = brick::fs::isAbsolute( symPath);
        if ( !absolute ) {
            auto split = brick::fs::splitPath( oPath );
            split.pop_back();
            symPath = brick::fs::joinPath( brick::fs::joinPath( split ), symPath );
        }
        if ( follow && !seen( symPath ) ) {
            auto ex = [&]( const std::string& item ) {
                if ( absolute )
                    return explore( follow, noPrefixChange, see, seen, count,
                        limit, env, item );
                else
                    return explore( follow, mountPath, see, seen, count,
                            limit, env, item );
            };
            brick::fs::traverseDirectoryTree( symPath, ex, []( std::string ){}, ex );
        }
        return false;
    }
    return S_ISDIR( stat->st_mode );
}

} // namespace

void pruneBC( cc::Compile &driver )
{
    driver.prune( { "__sys_init", "main", "memmove", "memset",
                "memcpy", "llvm.global_ctors", "__lart_weakmem_buffer_size",
                "__md_get_function_meta", "__sys_env" } );
}

std::string outputName( std::string path )
{
    return brick::fs::replaceExtension( brick::fs::basename( path ), "bc" );
}

void WithBC::setup()
{
    using namespace brick::string;
    vm::BitCode::Env env;
    using bstr = std::vector< uint8_t >;
    int i = 0;
    for ( auto s : _env )
        env.emplace_back( "env." + fmt( i++ ), bstr( s.begin(), s.end() ) );
    i = 0;
    for ( auto o : _useropts )
        env.emplace_back( "arg." + fmt( i++ ), bstr( o.begin(), o.end() ) );
    i = 0;
    for ( auto o : _systemopts )
        env.emplace_back( "sys." + fmt( i++ ), bstr( o.begin(), o.end() ) );
    i = 0;
    std::set< std::string > vfsCaptured;
    size_t limit = _vfsSizeLimit;
    for ( auto vfs : _vfs ) {
        auto ex = [&](const std::string& item) {
            return explore( vfs.followSymlink,
                [&]( const std::string& s) { return changePathPrefix( s, vfs.capture, vfs.mount ); },
                [&]( const std::string& s ) { vfsCaptured.insert( s ); },
                [&]( const std::string& s ) { return visited( vfsCaptured, s); },
                [&]( ) { return i++; },
                [&]( size_t s ) { if ( limit < s ) die( "VFS capture limit reached"); limit -= s;  },
                env,
                item );
        };

        if ( vfs.capture == vfs.mount ) {
            vfsCaptured.insert( vfs.capture );
        }
        brick::fs::traverseDirectoryTree( vfs.capture, ex, []( std::string ){ }, ex );
    }

    if ( !_stdin.empty() ) {
        std::ifstream f( _stdin, std::ios::binary );
        bstr content( (std::istreambuf_iterator< char >( f )),
                      std::istreambuf_iterator< char >() );
        env.emplace_back( "vfs.stdin", content );
    }

    env.emplace_back( "divine.bcname", bstr( _file.begin(), _file.end() ) );


    try {
        _bc = std::make_shared< vm::BitCode >( _file );
    } catch ( vm::BCParseError &err ) /* probably not a bitcode file */
    {
        cc::Options ccopt;
        cc::Compile driver( ccopt );
        std::vector< std::string > ccopts;
        if ( !_std.empty() )
            ccopts.push_back( { "-std=" + _std } );
        for ( auto &o : _ccOpts )
            std::copy( o.begin(), o.end(), std::back_inserter( ccopts ) );
        ccopts.push_back( _file );

        driver.setupFS( rt::each );
        driver.runCC( ccopts );
        pruneBC( driver );
        _bc = std::make_shared< vm::BitCode >(
            std::unique_ptr< llvm::Module >( driver.getLinked() ),
            driver.context() );
    }
    _bc->environment( env );
    _bc->autotrace( _autotrace );
    _bc->reduce( !_disableStaticReduction );
    _bc->lart( _lartPasses );
}

void Cc::run()
{
    cc::Compile driver( _drv );
    driver.setupFS( rt::each );
    for ( auto &x : _passThroughFlags )
        std::copy( x.begin(), x.end(), std::back_inserter( _flags ) );

    std::string firstFile;
    driver.runCC( _flags, [&]( std::unique_ptr< llvm::Module > &&m, std::string name )
            -> std::unique_ptr< llvm::Module >
        {
            bool first;
            if ( (first = firstFile.empty()) )
                firstFile = name;

            if ( _drv.dont_link ) {
                if ( !_output.empty() && !first )
                    die( "CC: Cannot specify --dont-link/-c with -o with multiple input files." );
                driver.writeToFile( _output.empty() ? outputName( name ) : _output, m.get() );
                return nullptr;
            }
            return std::move( m );
        } );

    if ( firstFile.empty() )
        die( "CC: You must specify at least one source file." );

    pruneBC( driver );
    driver.writeToFile( _output.empty() ? outputName( firstFile ) : _output );
}

}
}
