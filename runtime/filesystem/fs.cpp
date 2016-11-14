// -*- C++ -*- (c) 2015 Jiří Weiser

#include <iostream>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>  

#include <bits/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dios.h>

#include "fs-manager.h"

#ifdef __divine__

# define FS_MALLOC( x ) __vm_obj_make( x )
# define FS_PROBLEM( msg ) __dios_fault( _VM_Fault::_VM_F_Assert, msg )

#else
# define FS_MALLOC( x ) std::malloc( x )
# define FS_PROBLEM( msg )
#endif

using divine::fs::Error;
// divine::fs::VFS vfs{};

namespace conversion {

    using namespace divine::fs::flags;

    divine::fs::Flags <Open> open( int fls ) {
        divine::fs::Flags <Open> f = Open::NoFlags;
        // special behaviour - check for access rights but do not grant them
        if (( fls & 3 ) == 3 )
            f |= Open::NoAccess;
        if ( fls & O_RDWR ) {
            f |= Open::Read;
            f |= Open::Write;
        }
        else if ( fls & O_WRONLY )
            f |= Open::Write;
        else
            f |= Open::Read;

        if ( fls & O_CREAT ) f |= Open::Create;
        if ( fls & O_EXCL ) f |= Open::Excl;
        if ( fls & O_TRUNC ) f |= Open::Truncate;
        if ( fls & O_APPEND ) f |= Open::Append;
        if ( fls & O_NOFOLLOW ) f |= Open::SymNofollow;
        if ( fls & O_NONBLOCK ) f |= Open::NonBlock;
        return f;
    }

    int open( divine::fs::Flags <Open> fls ) {
        int f;
        if ( fls.has( Open::NoAccess ))
            f = 3;
        else if ( fls.has( Open::Read ) && fls.has( Open::Write ))
            f = O_RDWR;
        else if ( fls.has( Open::Write ))
            f = O_WRONLY;
        else
            f = O_RDONLY;

        if ( fls.has( Open::Create )) f |= O_CREAT;
        if ( fls.has( Open::Excl )) f |= O_EXCL;
        if ( fls.has( Open::Truncate )) f |= O_TRUNC;
        if ( fls.has( Open::Append )) f |= O_APPEND;
        if ( fls.has( Open::SymNofollow )) f |= O_NOFOLLOW;
        if ( fls.has( Open::NonBlock )) f |= O_NONBLOCK;
        return f;
    }

    divine::fs::Flags <Message> message( int fls ) {
        divine::fs::Flags <Message> f = Message::NoFlags;

        if ( fls & MSG_DONTWAIT ) f |= Message::DontWait;
        if ( fls & MSG_PEEK ) f |= Message::Peek;
        if ( fls & MSG_WAITALL ) f |= Message::WaitAll;
        return f;
    }

    static_assert( AT_FDCWD == divine::fs::CURRENT_DIRECTORY,
                   "mismatch value of AT_FDCWD and divine::fs::CURRENT_DIRECTORY" );
}

namespace __sc {

    //With _ begins function that are many-times user and recalled.
    static void _initStat( struct stat *buf ) {
        buf->st_dev = 0;
        buf->st_rdev = 0;
        buf->st_atime = 0;
        buf->st_mtime = 0;
        buf->st_ctime = 0;
    }

    static int _fillStat( const divine::fs::Node item, struct stat *buf ) {
        if ( !item )
            return -1;
        _initStat( buf );
        buf->st_ino = item->ino( );
        buf->st_mode = item->mode( );
        buf->st_nlink = item.use_count( );
        buf->st_size = item->size( );
        buf->st_uid = item->uid( );
        buf->st_gid = item->gid( );
        buf->st_blksize = 512;
        buf->st_blocks = ( buf->st_size + 1 ) / buf->st_blksize;
        return 0;
    }

    int _mknodat( int dirfd, const char *path, mode_t mode, dev_t dev, divine::fs::VFS *vfs ) 
    {
            if ( dev != 0 )
                throw Error( EINVAL );
            if ( !S_ISCHR( mode ) && !S_ISBLK( mode ) && !S_ISREG( mode ) && !S_ISFIFO( mode ) && !S_ISSOCK( mode ))
                throw Error( EINVAL );
            vfs->instance( ).createNodeAt( dirfd, path, mode );
            return  0;
    }

    void creat( __dios::Context& ctx, int* err, void* retval, va_list vl  ) 
    {
        auto path = va_arg( vl, const char * );
        auto mode = va_arg( vl, mode_t );
        auto ret = static_cast< int * >( retval );
        auto vfs = ctx.vfs;
        va_end( vl );
        
        try {
            *ret =  _mknodat( AT_FDCWD, path, mode | S_IFREG, 0, vfs );
        }catch( Error &e ) {
            *ret = -1;
            *err = e.code();
        }
    }

    void open( __dios::Context& ctx, int* err, void* retval, va_list vl  ) 
    {
        auto path = va_arg( vl, const char * );
        auto flags = va_arg(  vl, int );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;
        int mode = 0;
        using namespace divine::fs::flags;
        divine::fs::Flags <Open> f = Open::NoFlags;
        if (( flags & 3 ) == 3 )
            f |= Open::NoAccess;
        if ( flags & O_RDWR ) {
            f |= Open::Read;
            f |= Open::Write;
        }
        else if ( flags & O_WRONLY )
            f |= Open::Write;
        else
            f |= Open::Read;

        if ( flags & O_CREAT ) {
            f |= divine::fs::flags::Open::Create;
            if ( !vl )
                    FS_PROBLEM( "flag O_CREAT has been specified but mode was not set" );
            mode = va_arg(  vl, int );
        }
        va_end( vl );

        if ( flags & O_EXCL )
            f |= divine::fs::flags::Open::Excl;
        if ( flags & O_TRUNC )
            f |= divine::fs::flags::Open::Truncate;
        if ( flags & O_APPEND )
            f |= divine::fs::flags::Open::Append;
        if ( flags & O_NOFOLLOW )
            f |= divine::fs::flags::Open::SymNofollow;
        if ( flags & O_NONBLOCK )
            f |= divine::fs::flags::Open::NonBlock;

        try {
            *ret = vfs->instance( ).openFileAt( AT_FDCWD, path, f, mode );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    
    }

    void openat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        using namespace divine::fs::flags;
        divine::fs::Flags <Open> f = Open::NoFlags;
        mode_t mode = 0;
        auto dirfd = va_arg(  vl, int );
        auto path = va_arg( vl, const char * );
        auto flags = va_arg(  vl, int );
        auto ret = static_cast< int *>( retval );
        auto vfs = ctx.vfs;
        // special behaviour - check for access rights but do not grant them
        if (( flags & 3 ) == 3 )
            f |= Open::NoAccess;
        if ( flags & O_RDWR ) {
            f |= Open::Read;
            f |= Open::Write;
        }
        else if ( flags & O_WRONLY )
            f |= Open::Write;
        else
            f |= Open::Read;

        if ( flags & O_CREAT ) {
            f |= divine::fs::flags::Open::Create;
            if ( !vl )
                    FS_PROBLEM( "flag O_CREAT has been specified but mode was not set" );
            mode = va_arg(  vl, mode_t );
            va_end(  vl );
        }

        if ( flags & O_EXCL )
            f |= divine::fs::flags::Open::Excl;
        if ( flags & O_TRUNC )
            f |= divine::fs::flags::Open::Truncate;
        if ( flags & O_APPEND )
            f |= divine::fs::flags::Open::Append;
        if ( flags & O_NOFOLLOW )
            f |= divine::fs::flags::Open::SymNofollow;
        if ( flags & O_NONBLOCK )
            f |= divine::fs::flags::Open::NonBlock;

        try {
            *ret = vfs->instance( ).openFileAt( dirfd, path, f, mode );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fcntl( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto cmd = va_arg( vl, int );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            auto f = vfs->instance( ).getFile( fd );

            switch ( cmd ) {
                case F_SETFD: {
                    if ( !vl )
                            FS_PROBLEM( "command F_SETFD requires additional argument" );
                    int lowEdge = va_arg(  vl, int );
                }
                case F_GETFD:
                    *ret = 0;
                    return;
                case F_DUPFD_CLOEXEC: // for now let assume we cannot handle O_CLOEXEC
                case F_DUPFD: {
                    if ( !vl )
                            FS_PROBLEM( "command F_DUPFD requires additional argument" );
                    int lowEdge = va_arg(  vl, int );
                    va_end( vl );
                    *ret = vfs->instance( ).duplicate( fd, lowEdge );
                    return;
                }
                case F_GETFL:
                    va_end( vl );
                    *ret = conversion::open( f->flags( ));
                    return;
                case F_SETFL: {
                    if ( !vl )
                            FS_PROBLEM( "command F_SETFL requires additional argument" );
                    int mode = va_arg(  vl, int );

                    if ( mode & O_APPEND )
                        f->flags( ) |= divine::fs::flags::Open::Append;
                    else if ( f->flags( ).has( divine::fs::flags::Open::Append ))
                        throw Error( EPERM );

                    if ( mode & O_NONBLOCK )
                        f->flags( ) |= divine::fs::flags::Open::NonBlock;
                    else
                        f->flags( ).clear( divine::fs::flags::Open::NonBlock );

                    va_end( vl );
                    *ret = 0;
                    return;
                }
                default:
                    FS_PROBLEM( "the requested command is not implemented" );
                    va_end( vl );
                    *ret = 0;
                    return;
            }

        } catch ( Error & e ) {
            va_end( vl );
            *err = e.code();
            *ret = -1;
        }
    }

    void close( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).closeFile( fd );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void write( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto buf = va_arg( vl, const void * );
        auto count = va_arg( vl, size_t );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;

        try {
            auto f = vfs->instance( ).getFile( fd );
            *ret = f->write( buf, count );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void pwrite( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto buf = va_arg( vl, const void * );
        auto count = va_arg( vl, size_t );
        auto offset = va_arg( vl, off_t );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;

        try {
            auto f = vfs->instance( ).getFile( fd );
            size_t savedOffset = f->offset( );
            f->offset( offset );
            auto d = divine::fs::utils::make_defer( [ & ] { f->offset( savedOffset ); } );
            *ret = f->write( buf, count );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void read( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto buf = va_arg( vl, void * );
        auto count = va_arg( vl, size_t );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;

        try {
            auto f = vfs->instance( ).getFile( fd );
            *ret = f->read( buf, count );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void pread( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto buf = va_arg( vl, void * );
        auto count = va_arg( vl, size_t );
        auto offset = va_arg( vl, off_t );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;

        try {
            auto f = vfs->instance( ).getFile( fd );
            size_t savedOffset = f->offset( );
            f->offset( offset );
            auto d = divine::fs::utils::make_defer( [ & ] { f->offset( savedOffset ); } );
            *ret = f->read( buf, count );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void pipe( __dios::Context& ctx, int* err, void* retval, va_list vl )
    {
        auto ret = static_cast< int* >( retval );
        auto pipefd = va_arg( vl, int* );
        auto vfs = ctx.vfs;

        try {
            std::tie( pipefd[ 0 ], pipefd[ 1 ] ) = vfs->instance( ).pipe( );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void lseek( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< off_t* >( retval );
        auto fd = va_arg( vl, int );
        auto offset = va_arg( vl, off_t );
        auto whence = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            divine::fs::Seek w = divine::fs::Seek::Undefined;
            switch ( whence ) {
                case SEEK_SET:
                    w = divine::fs::Seek::Set;
                    break;
                case SEEK_CUR:
                    w = divine::fs::Seek::Current;
                    break;
                case SEEK_END:
                    w = divine::fs::Seek::End;
                    break;
            }
            *ret = vfs->instance( ).lseek( fd, offset, w );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void dup( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            *ret = vfs->instance( ).duplicate( fd );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void dup2( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto oldfd = va_arg( vl, int );
        auto newfd = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            *ret = vfs->instance( ).duplicate2( oldfd, newfd );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void ftruncate( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto length = va_arg( vl, off_t );
        auto vfs = ctx.vfs;

        try {
            auto item = vfs->instance( ).getFile( fd );
            if ( !item->flags( ).has( divine::fs::flags::Open::Write ))
                throw Error( EINVAL );
            vfs->instance( ).truncate( item->inode( ), length );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void truncate( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto length = va_arg( vl, off_t );
        auto vfs = ctx.vfs;

        try {
            auto item = vfs->instance( ).findDirectoryItem( path );
            vfs->instance( ).truncate( item, length );
            *ret =  0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void unlink( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).removeFile( path );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void rmdir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).removeDirectory( path );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void unlinkat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirfd = va_arg( vl, int );
        auto path = va_arg( vl, const char* );
        auto flags = va_arg( vl, int );
        auto vfs = ctx.vfs;

        divine::fs::flags::At f;
        switch ( flags ) {
            case 0:
                f = divine::fs::flags::At::NoFlags;
                break;
            case AT_REMOVEDIR:
                f = divine::fs::flags::At::RemoveDir;
                break;
            default:
                f = divine::fs::flags::At::Invalid;
                break;
        }
        try {
            vfs->instance( ).removeAt( dirfd, path, f );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    int _linkat(int olddirfd, const char *target, int newdirfd, const char *linkpath, int flags,divine::fs::VFS* vfs)
    {

        divine::fs::Flags <divine::fs::flags::At> fl = divine::fs::flags::At::NoFlags;
        if ( flags & AT_SYMLINK_FOLLOW ) fl |= divine::fs::flags::At::SymFollow;
        if (( flags | AT_SYMLINK_FOLLOW ) != AT_SYMLINK_FOLLOW )
            fl |= divine::fs::flags::At::Invalid;
            vfs->instance( ).createHardLinkAt( newdirfd, linkpath, olddirfd, target, fl );
            return 0;
    }

    void linkat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto olddirfd = va_arg( vl, int );
        auto target = va_arg( vl, const char* );
        auto newdirfd = va_arg( vl, int );
        auto linkpath = va_arg( vl, const char* );
        auto flags = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            *ret = _linkat( olddirfd, target, newdirfd, linkpath, flags, vfs);
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
        
        
    }

    void link( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto target = va_arg( vl, const char* );
        auto linkpath = va_arg( vl, const char* );
        auto vfs = ctx.vfs;
         try {
            *ret = _linkat(AT_FDCWD, target, AT_FDCWD, linkpath, 0 , vfs);
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void symlinkat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto target = va_arg( vl, const char* );
        auto dirfd = va_arg( vl, int );
        auto linkpath = va_arg( vl, const char* );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).createSymLinkAt( dirfd, linkpath, target );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void symlink( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto target = va_arg( vl, const char* );
        auto linkpath = va_arg( vl, const char* );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).createSymLinkAt( AT_FDCWD, linkpath, target );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void readlinkat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< ssize_t* >( retval );
        auto dirfd = va_arg( vl, int );
        auto path = va_arg( vl, const char* );
        auto buf = va_arg( vl, char* );
        auto count = va_arg( vl, size_t );
        auto vfs = ctx.vfs;

        try {
            *ret = vfs->instance( ).readLinkAt( dirfd, path, buf, count );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void readlink( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< ssize_t* >( retval );
        auto path = va_arg( vl, const char* );
        auto buf = va_arg( vl, char* );
        auto count = va_arg( vl, size_t );
        auto vfs = ctx.vfs;

        try {
            *ret = vfs->instance( ).readLinkAt( AT_FDCWD, path, buf, count );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void faccessat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirfd = va_arg( vl, int );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, int );
        auto flags = va_arg( vl, int );
        auto vfs = ctx.vfs;

        divine::fs::Flags <divine::fs::flags::Access> m = divine::fs::flags::Access::OK;
        if ( mode & R_OK ) m |= divine::fs::flags::Access::Read;
        if ( mode & W_OK ) m |= divine::fs::flags::Access::Write;
        if ( mode & X_OK ) m |= divine::fs::flags::Access::Execute;
        if (( mode | R_OK | W_OK | X_OK ) != ( R_OK | W_OK | X_OK ))
            m |= divine::fs::flags::Access::Invalid;

        divine::fs::Flags <divine::fs::flags::At> fl = divine::fs::flags::At::NoFlags;
        if ( flags & AT_EACCESS ) fl |= divine::fs::flags::At::EffectiveID;
        if ( flags & AT_SYMLINK_NOFOLLOW ) fl |= divine::fs::flags::At::SymNofollow;
        if (( flags | AT_EACCESS | AT_SYMLINK_NOFOLLOW ) != ( AT_EACCESS | AT_SYMLINK_NOFOLLOW ))
            fl |= divine::fs::flags::At::Invalid;

        try {
            vfs->instance( ).accessAt( dirfd, path, m, fl );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void access( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, int );
        auto vfs = ctx.vfs;

        divine::fs::Flags <divine::fs::flags::Access> m = divine::fs::flags::Access::OK;
        if ( mode & R_OK ) m |= divine::fs::flags::Access::Read;
        if ( mode & W_OK ) m |= divine::fs::flags::Access::Write;
        if ( mode & X_OK ) m |= divine::fs::flags::Access::Execute;
        if (( mode | R_OK | W_OK | X_OK ) != ( R_OK | W_OK | X_OK ))
            m |= divine::fs::flags::Access::Invalid;

        divine::fs::Flags <divine::fs::flags::At> fl = divine::fs::flags::At::NoFlags;
       
        try {
            vfs->instance( ).accessAt( AT_FDCWD, path, m, fl );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void chdir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).changeDirectory( path );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fchdir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirfd = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).changeDirectory( dirfd );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fdatasync( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).getFile( fd );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fsync( __dios::Context& ctx, int* err, void* retval, va_list vl ) {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).getFile( fd );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void syncfs( __dios::Context& ctx, int* err, void* retval, va_list vl ) {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto vfs = ctx.vfs;
        va_end( vl );

        try {
            vfs->instance( ).getFile( fd );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void sync( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {}

    void stat(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto buf = va_arg( vl, struct stat* );
        auto vfs = ctx.vfs;

        try {
            auto item = vfs->instance( ).findDirectoryItem( path );
            if ( !item )
                throw Error( ENOENT );
            *ret = _fillStat( item, buf );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void lstat(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto buf = va_arg( vl, struct stat* );
        auto vfs = ctx.vfs;

        try {
            auto item = vfs->instance( ).findDirectoryItem( path, false );
            if ( !item )
                throw Error( ENOENT );
            *ret = _fillStat( item, buf );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fstat(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto buf = va_arg( vl, struct stat* );
        auto vfs = ctx.vfs;

        try {
            auto item = vfs->instance( ).getFile( fd );
            *ret = _fillStat( item->inode( ), buf );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fstatfs(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto buf = va_arg( vl, struct statfs* );
        auto vfs = ctx.vfs;
        *ret = -1;
        FS_PROBLEM("Fstatfs not implemented");
    }

    void statfs(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto buf = va_arg( vl, struct statfs* );
        auto vfs = ctx.vfs;
        *ret = -1;
        FS_PROBLEM("statfs not implemented");
    }

    void fchmodat(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirfd = va_arg( vl, int );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, mode_t );
        auto flags = va_arg( vl, int );
        auto vfs = ctx.vfs;
        
        divine::fs::Flags <divine::fs::flags::At> fl = divine::fs::flags::At::NoFlags;
        if ( flags & AT_SYMLINK_NOFOLLOW ) fl |= divine::fs::flags::At::SymNofollow;
        if (( flags | AT_SYMLINK_NOFOLLOW ) != AT_SYMLINK_NOFOLLOW )
            fl |= divine::fs::flags::At::Invalid;

        try {
            vfs->instance( ).chmodAt( dirfd, path, mode, fl );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void chmod(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, mode_t );
        auto vfs = ctx.vfs;

        divine::fs::Flags <divine::fs::flags::At> fl = divine::fs::flags::At::NoFlags;

        try {
            vfs->instance( ).chmodAt( AT_FDCWD, path, mode, fl );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fchmod(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto fd = va_arg( vl, int );
        auto mode = va_arg( vl, mode_t );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).chmod( fd, mode );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void mkdirat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirfd = va_arg( vl, int );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, mode_t );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).createNodeAt( dirfd, path, ( ACCESSPERMS & mode ) | S_IFDIR );
            *ret =  0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret =  -1;
        }
    }

    void mkdir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, mode_t );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).createNodeAt( AT_FDCWD, path, ( ACCESSPERMS & mode ) | S_IFDIR );
            *ret =  0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret =  -1;
        }
    }

    void mknodat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirfd = va_arg( vl, int );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, mode_t );
        auto dev = va_arg( vl, dev_t );
        auto vfs = ctx.vfs;

        try {
            *ret = _mknodat( dirfd, path, mode, dev, vfs);
        } catch ( Error & e ) {
            *err = e.code();
            *ret =  -1;
        }
    }
 
    void mknod( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto mode = va_arg( vl, mode_t );
        auto dev = va_arg( vl, dev_t );
        auto vfs = ctx.vfs;

         try {
            if ( dev != 0 )
                throw Error( EINVAL );
            if ( !S_ISCHR( mode ) && !S_ISBLK( mode ) && !S_ISREG( mode ) && !S_ISFIFO( mode ) && !S_ISSOCK( mode ))
                throw Error( EINVAL );
            vfs->instance( ).createNodeAt( AT_FDCWD, path, mode );
            *ret =  0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret =  -1;
        }
    }

    void umask( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< mode_t* >( retval );
        auto mask = va_arg( vl, mode_t );
        auto vfs = ctx.vfs;


        mode_t result = vfs->instance( ).umask( );
        vfs->instance( ).umask( mask & 0777 );
        *ret = result;
    }


    void closedir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirp = va_arg( vl, DIR* );
        auto vfs = ctx.vfs;

    
        try {
            vfs->instance().closeDirectory( dirp );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void dirfd( __dios::Context& ctx, int* err, void* retval, va_list vl  ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirp = va_arg( vl, DIR* );
        auto vfs = ctx.vfs;

        try {
            *ret = vfs->instance().getDirectory( dirp )->fd();
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void fdopendir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< DIR** >( retval );
        auto fd = va_arg( vl, int );
        auto vfs = ctx.vfs;

        try {
            *ret = vfs->instance().openDirectory( fd );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = nullptr;
        }
    }


    void opendir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< DIR** >( retval );
        auto path = va_arg( vl, const char* );
        auto vfs = ctx.vfs;

        using namespace divine::fs::flags;
        divine::fs::Flags< Open > f = Open::Read;
        try {
            int fd = vfs->instance().openFileAt( divine::fs::CURRENT_DIRECTORY, path, f, 0 );
            *ret = vfs->instance().openDirectory( fd );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = nullptr;
        }
    }

    void readdir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast<struct dirent **>( retval );
        auto dirp = va_arg( vl, DIR* );
        static struct dirent entry;
        auto vfs = ctx.vfs;

        try {
            auto dir = vfs->instance().getDirectory( dirp );
            auto ent = dir->get();
            
            if ( !ent ){
                *ret = nullptr;
                return;
            }
                
            entry.d_ino = ent->ino();
            char *x = std::copy( ent->name().begin(), ent->name().end(), entry.d_name );
            *x = '\0';
            dir->next();
            *ret = &entry;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = nullptr;
        }
    }

    void readdir_r( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< int* >( retval );
        auto dirp = va_arg( vl, DIR* );
        auto entry = va_arg( vl, struct dirent* );
        auto result = va_arg( vl, struct dirent** );
        auto vfs = ctx.vfs;

        try {
            auto dir = vfs->instance().getDirectory( dirp );
            auto ent = dir->get();
            if ( ent ) {
                entry->d_ino = ent->ino();
                char *x = std::copy( ent->name().begin(), ent->name().end(), entry->d_name );
                *x = '\0';
                *result = entry;
                dir->next();
            }
            else
                *result = nullptr;
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = e.code();
        }
    }

    void rewinddir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto dirp = va_arg( vl, DIR* );
        int e = *err;
        auto vfs = ctx.vfs;

        try {
            vfs->instance().getDirectory( dirp )->rewind();
        } catch ( Error & er ) {
            *err = er.code();
        }
    }

    void scandir( __dios::Context& ctx, int* err, void* retval, va_list vl )
    {
        typedef int (*filterFc)( const struct dirent * );
        typedef int (*compareFc)( const struct dirent **, const struct dirent ** );

        auto ret = static_cast< int* >( retval );
        auto path = va_arg( vl, const char* );
        auto namelist = va_arg( vl, struct dirent *** );
        auto filter = va_arg( vl,  filterFc );
        auto compare = va_arg( vl,  compareFc );
        auto vfs = ctx.vfs;

        using namespace divine::fs::flags;
        divine::fs::Flags< Open > f = Open::Read;
        DIR *dirp = nullptr;
        try {
            int length = 0;
            int fd = vfs->instance().openFileAt( divine::fs::CURRENT_DIRECTORY, path, f, 0 );
            dirp = vfs->instance().openDirectory( fd );

            struct dirent **entries = nullptr;
            struct dirent *workingEntry = (struct dirent *)FS_MALLOC( sizeof( struct dirent ) );

            while ( true ) {
                auto dir = vfs->instance().getDirectory( dirp );
                auto ent = dir->get();
                if ( !ent )
                    break;

                workingEntry->d_ino = ent->ino();
                char *x = std::copy( ent->name().begin(), ent->name().end(), workingEntry->d_name );
                *x = '\0';
                dir->next();

                if ( filter && !filter( workingEntry ) )
                    continue;

                struct dirent **newEntries = (struct dirent **)FS_MALLOC( ( length + 1 ) * sizeof( struct dirent * ) );
                if ( length )
                    std::memcpy( newEntries, entries, length * sizeof( struct dirent * ) );
                std::swap( entries, newEntries );
                std::free( newEntries );
                entries[ length ] = workingEntry;
                workingEntry = (struct dirent *)FS_MALLOC( sizeof( struct dirent ) );
                ++length;
            }
            std::free( workingEntry );
            vfs->instance().closeDirectory( dirp );

            typedef int( *cmp )( const void *, const void * );
            std::qsort( entries, length, sizeof( struct dirent * ), reinterpret_cast< cmp >( compare ) );

            *namelist = entries;
            *ret = length;
        } catch ( Error & e ) {
            *err = e.code();
            if ( dirp )
                vfs->instance().closeDirectory( dirp );
            *ret = -1;
        }
    }

    void telldir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto ret = static_cast< long* >( retval );
        auto dirp = va_arg( vl, DIR* );
        int e = *err;
        auto vfs = ctx.vfs;

        try {
            *ret = vfs->instance().getDirectory( dirp )->tell();
        } catch ( Error & er ) {
            *err = e;
            *ret = -1;
        }
    }

    void seekdir( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto dirp = va_arg( vl, DIR* );
        auto offset = va_arg( vl, long );
        int e = *err;
        auto vfs = ctx.vfs;

        try {
            vfs->instance().getDirectory( dirp )->seek( offset );
        } catch ( Error & er ) {
            *err = e;
        }
    }


    void socket( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        using SocketType = divine::fs::SocketType;
        using namespace divine::fs::flags;
        auto domain = va_arg( vl, int );
        auto t = va_arg( vl, int );
        auto protocol = va_arg( vl, int );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            if ( domain != AF_UNIX )
                throw Error( EAFNOSUPPORT );

            SocketType type;
            switch ( t & __SOCK_TYPE_MASK ) {
                case SOCK_STREAM:
                    type = SocketType::Stream;
                    break;
                case SOCK_DGRAM:
                    type = SocketType::Datagram;
                    break;
                default:
                    throw Error( EPROTONOSUPPORT );
            }
            if ( protocol )
                throw Error( EPROTONOSUPPORT );

            *ret = vfs->instance( ).socket( type, t & SOCK_NONBLOCK ? Open::NonBlock : Open::NoFlags );

        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void socketpair( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        using SocketType = divine::fs::SocketType;
        using Open = divine::fs::flags::Open;
        auto domain = va_arg( vl, int );
        auto t = va_arg( vl, int );
        auto protocol = va_arg( vl, int );
        auto fds = va_arg( vl, int* );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {

            if ( domain != AF_UNIX )
                throw Error( EAFNOSUPPORT );

            SocketType type;
            switch ( t & __SOCK_TYPE_MASK ) {
                case SOCK_STREAM:
                    type = SocketType::Stream;
                    break;
                case SOCK_DGRAM:
                    type = SocketType::Datagram;
                    break;
                default:
                    throw Error( EPROTONOSUPPORT );
            }
            if ( protocol )
                throw Error( EPROTONOSUPPORT );

            std::tie( fds[ 0 ], fds[ 1 ] ) = vfs->instance( ).socketpair( type, t & SOCK_NONBLOCK ? Open::NonBlock
                                                                                                 : Open::NoFlags );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void getsockname( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto addr = va_arg( vl,  struct sockaddr* );
        auto len = va_arg( vl, socklen_t* );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            if ( !len )
                throw Error( EFAULT );

            auto s = vfs->instance( ).getSocket( sockfd );
            struct sockaddr_un *target = reinterpret_cast< struct sockaddr_un * >( addr );

            auto &address = s->address( );

            if ( address.size( ) >= *len )
                throw Error( ENOBUFS );

            if ( target ) {
                target->sun_family = AF_UNIX;
                char *end = std::copy( address.value( ).begin( ), address.value( ).end( ), target->sun_path );
                *end = '\0';
            }
            *len = address.size( ) + 1 + sizeof( target->sun_family );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void bind( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto addr = va_arg( vl,  const struct sockaddr* );
        auto len = va_arg( vl, socklen_t );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;
        using Address = divine::fs::Socket::Address;

        try {

            if ( !addr )
                throw Error( EFAULT );
            if ( addr->sa_family != AF_UNIX )
                throw Error( EINVAL );

            const struct sockaddr_un *target = reinterpret_cast< const struct sockaddr_un * >( addr );
            Address address( target->sun_path );

            vfs->instance( ).bind( sockfd, std::move( address ));
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void connect( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto addr = va_arg( vl,  const struct sockaddr* );
        auto len = va_arg( vl, socklen_t );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;
        using Address = divine::fs::Socket::Address;

        try {

            if ( !addr )
                throw Error( EFAULT );
            if ( addr->sa_family != AF_UNIX )
                throw Error( EAFNOSUPPORT );

            const struct sockaddr_un *target = reinterpret_cast< const struct sockaddr_un * >( addr );
            Address address( target->sun_path );

            vfs->instance( ).connect( sockfd, address );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void getpeername( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto addr = va_arg( vl, struct sockaddr* );
        auto len = va_arg( vl, socklen_t* );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            if ( !len )
                throw Error( EFAULT );

            auto s = vfs->instance( ).getSocket( sockfd );
            struct sockaddr_un *target = reinterpret_cast< struct sockaddr_un * >( addr );

            auto &address = s->peer( ).address( );

            if ( address.size( ) >= *len )
                throw Error( ENOBUFS );

            if ( target ) {
                target->sun_family = AF_UNIX;
                char *end = std::copy( address.value( ).begin( ), address.value( ).end( ), target->sun_path );
                *end = '\0';
            }
            *len = address.size( ) + 1 + sizeof( target->sun_family );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void  send( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto buf = va_arg( vl,  const void* );
        auto n = va_arg( vl, size_t );
        auto flags = va_arg( vl, int );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;
        
        try {
            auto s = vfs->instance( ).getSocket( sockfd );
            *ret = s->send( static_cast< const char * >( buf ), n, conversion::message( flags ));
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void sendto( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto buf = va_arg( vl,  const void* );
        auto n = va_arg( vl, size_t );
        auto flags = va_arg( vl, int );
        auto addr = va_arg( vl,  const struct sockaddr* );
        auto len = va_arg( vl, socklen_t );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;
        using Address = divine::fs::Socket::Address;

        if ( !addr ) {
            try {
                auto s = vfs->instance( ).getSocket( sockfd );
                *ret = s->send( static_cast< const char * >( buf ), n, conversion::message( flags ));
            } catch ( Error & e ) {
                *err = e.code();
                *ret = -1;
            }
            return;
        }

        try {
            if ( addr->sa_family != AF_UNIX )
                throw Error( EAFNOSUPPORT );

            auto s = vfs->instance( ).getSocket( sockfd );
            const struct sockaddr_un *target = reinterpret_cast< const struct sockaddr_un * >( addr );
            Address address( target ? target->sun_path : divine::fs::utils::String( ));

            *ret = s->sendTo( static_cast< const char * >( buf ), n, conversion::message( flags ),
                              vfs->instance( ).resolveAddress( address ));
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    ssize_t _recvfrom(int sockfd, void *buf, size_t n, int flags, struct sockaddr *addr, socklen_t *len, divine::fs::VFS* vfs ) 
    {
        using Address = divine::fs::Socket::Address;
        Address address;
        struct sockaddr_un *target = reinterpret_cast< struct sockaddr_un * >( addr );
        if ( target && !len )
            throw Error( EFAULT );

        auto s = vfs->instance( ).getSocket( sockfd );
        n = s->receive( static_cast< char * >( buf ), n, conversion::message( flags ), address );

        if ( target ) {
            target->sun_family = AF_UNIX;
            char *end = std::copy( address.value( ).begin( ), address.value( ).end( ), target->sun_path );
            *end = '\0';
            *len = address.size( ) + 1 + sizeof( target->sun_family );
        }
        return n;
    }

    void recv( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto buf = va_arg( vl,  void* );
        auto n = va_arg( vl, size_t );
        auto flags = va_arg( vl, int );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;

        try {
             *ret = _recvfrom( sockfd, buf, n, flags, nullptr, nullptr, vfs );
        }catch( Error & e ){
             *err = e.code();
            *ret = -1;
        }
    }

    void recvfrom( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto buf = va_arg( vl,  void* );
        auto n = va_arg( vl, size_t );
        auto flags = va_arg( vl, int );
        auto addr = va_arg( vl,  struct sockaddr* );
        auto len = va_arg( vl, socklen_t* );
        auto ret = static_cast< ssize_t* >( retval );
        auto vfs = ctx.vfs;

        try {
             *ret = _recvfrom( sockfd, buf, n, flags, addr, len, vfs );
        }catch( Error & e ){
             *err = e.code();
            *ret = -1;
        }
    }

    void listen( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto n = va_arg( vl, int );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            auto s = vfs->instance( ).getSocket( sockfd );
            s->listen( n );
            *ret = 0;
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    int _accept4( int sockfd, struct sockaddr *addr, socklen_t *len, int flags, divine::fs::VFS* vfs ) 
    {
        using Address = divine::fs::Socket::Address;

        if ( addr && !len )
            throw Error( EFAULT );

        if (( flags | SOCK_NONBLOCK | SOCK_CLOEXEC ) != ( SOCK_NONBLOCK | SOCK_CLOEXEC ))
            throw Error( EINVAL );

        Address address;
        int newSocket = vfs->instance( ).accept( sockfd, address );

        if ( addr ) {
            struct sockaddr_un *target = reinterpret_cast< struct sockaddr_un * >( addr );
            target->sun_family = AF_UNIX;
            char *end = std::copy( address.value( ).begin( ), address.value( ).end( ), target->sun_path );
            *end = '\0';
            *len = address.size( ) + 1 + sizeof( target->sun_family );
        }
        if ( flags & SOCK_NONBLOCK )
            vfs->instance( ).getSocket( newSocket )->flags( ) |= divine::fs::flags::Open::NonBlock;

        return newSocket;
    }

    void accept( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto addr = va_arg( vl,  struct sockaddr* );
        auto len = va_arg( vl, socklen_t* );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            *ret = _accept4( sockfd, addr, len, 0, vfs );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void accept4( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto sockfd = va_arg( vl, int );
        auto addr = va_arg( vl,  struct sockaddr* );
        auto len = va_arg( vl, socklen_t* );
        auto flags = va_arg( vl, int );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;
        try {
            *ret = _accept4( sockfd, addr, len, flags, vfs );
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }

    void mkfifoat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto dirfd = va_arg( vl, int );
        auto path = va_arg( vl,  const char* );
        auto mode = va_arg( vl, mode_t );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;
        
        try {
           *ret = _mknodat( dirfd, path, ( ACCESSPERMS & mode ) | S_IFIFO, 0, vfs );
       }catch( Error &e ) {
            *ret = -1;
            *err = e.code();
       }
    }

    void mkfifo( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto path = va_arg( vl,  const char* );
        auto mode = va_arg( vl, mode_t );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            *ret = _mknodat( AT_FDCWD, path, ( ACCESSPERMS & mode ) | S_IFIFO, 0, vfs );
        }catch( Error &e ) {
            *ret = -1;
            *err = e.code();
       }
    }

    void isatty(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).getFile( fd );
            *err = EINVAL;
            *ret = -1;
        } catch ( Error & e ) {
            *ret = 0;
        }
        
    }

    void ttyname(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto ret = static_cast< char** >( retval );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).getFile( fd );
            *err = ENOTTY;
        } catch ( Error & e ) {
        }
        *ret = nullptr;
    }

    void ttyname_r(  __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto fd = va_arg( vl, int );
        auto buf = va_arg( vl, char * );
        auto count = va_arg( vl, size_t );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            vfs->instance( ).getFile( fd );
            *ret = ENOTTY;
        } catch ( Error & e ) {
            *ret = e.code( );
        }
    }

    int _renameitemat( int olddirfd, const char *oldpath, int newdirfd, const char *newpath,divine::fs::VFS* vfs ) 
    {
        vfs->instance( ).renameAt( newdirfd, newpath, olddirfd, oldpath );
        return 0;
    }

    void _FS_renameitemat( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto olddirfd = va_arg( vl, int );
        auto oldpath = va_arg( vl, const char* );
        auto newdirfd = va_arg( vl, int );
        auto newpath = va_arg( vl, const char* );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            *ret = _renameitemat(olddirfd, oldpath, newdirfd, newpath, vfs);
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }

    }

    void _FS_renameitem( __dios::Context& ctx, int* err, void* retval, va_list vl ) 
    {
        auto oldpath = va_arg( vl, const char* );
        auto newpath = va_arg( vl, const char* );
        auto ret = static_cast< int* >( retval );
        auto vfs = ctx.vfs;

        try {
            *ret = _renameitemat( AT_FDCWD, oldpath, AT_FDCWD, newpath, vfs);
        } catch ( Error & e ) {
            *err = e.code();
            *ret = -1;
        }
    }
} //end namespace __sc

extern "C" {

    static void _initStat( struct stat *buf ) {
        buf->st_dev = 0;
        buf->st_rdev = 0;
        buf->st_atime = 0;
        buf->st_mtime = 0;
        buf->st_ctime = 0;
    }

    static int _fillStat( const divine::fs::Node item, struct stat *buf ) {
        if ( !item )
            return -1;
        _initStat( buf );
        buf->st_ino = item->ino( );
        buf->st_mode = item->mode( );
        buf->st_nlink = item.use_count( );
        buf->st_size = item->size( );
        buf->st_uid = item->uid( );
        buf->st_gid = item->gid( );
        buf->st_blksize = 512;
        buf->st_blocks = ( buf->st_size + 1 ) / buf->st_blksize;
        return 0;
    }


    void swab( const void *_from, void *_to, ssize_t n ) {
        const char *from = reinterpret_cast< const char * >( _from );
        char *to = reinterpret_cast< char * >( _to );
        for ( ssize_t i = 0; i < n / 2; ++i ) {
            *to = *( from + 1 );
            *( to + 1 ) = *from;
            to += 2;
            from += 2;
        }
    }

   
#if defined(__divine__)
int alphasort( const struct dirent **a, const struct dirent **b ) {
    return std::strcoll( (*a)->d_name, (*b)->d_name );
}

#endif

}
