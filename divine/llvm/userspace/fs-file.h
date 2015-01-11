#include <algorithm>

#include "fs-utils.h"

#ifndef _FS_FILE_H_
#define _FS_FILE_H_

namespace divine {
namespace fs {

struct FSItem : Grantsable {
    FSItem( unsigned mode = Grants::ALL ) :
        _grants( mode )
    {}
    virtual ~FSItem() {}
    virtual size_t size() const = 0;
    virtual bool read( char *, size_t, size_t & ) = 0;
    virtual bool write( const char *, size_t, size_t ) = 0;
    virtual bool isRegularFile() const {
        return true;
    }

    Grants &grants() override {
        return _grants;
    }
    Grants grants() const override {
        return _grants;
    }
private:
    Grants _grants;
};

struct File : public FSItem {

    File( const char *content, size_t size ) :
        _snapshot( bool( content ) ),
        _size( content ? size : 0 ),
        _roContent( content )
    {}

    File() :
        _snapshot( false ),
        _size( 0 ),
        _roContent( nullptr )
    {}

    File( const File &other ) = default;
    File( File &&other ) = default;
    File &operator=( File ) = delete;

    size_t size() const override {
        return _size;
    }

    bool read( char *buffer, size_t offset, size_t &length ) override {
        const char *source = isSnapshot() ?
                          _roContent + offset :
                          _content.data() + offset;
        if ( offset + length > _size )
            length = _size - offset;
        std::copy( source, source + length, buffer );
        return true;
    }

    bool write( const char *buffer, size_t offset, size_t length ) override {
        if ( isSnapshot() )
            copyOnWrite();

        if ( _content.size() < offset + length ) {
            _content.resize( offset + length );
            _size = _content.size();
        }
        std::copy( buffer, buffer + length, _content.begin() + offset );
        return true;
    }

private:

    bool isSnapshot() const {
        return _snapshot;
    }

    void copyOnWrite() {
        const char *roContent = _roContent;
        _content.resize( _size );

        std::copy( roContent, roContent + _size, _content.begin() );
        _snapshot = false;
    }

    bool _snapshot;
    size_t _size;
    const char *_roContent;
    utils::Vector< char > _content;
};

struct FileNull : public FSItem {

    size_t size() const override {
        return 0;
    }
    bool read( char *, size_t, size_t & ) override {
        return false;
    }
    bool write( const char*, size_t, size_t ) override {
        return true;
    }
};

struct Pipe : public FSItem {

    bool isRegularFile() const override {
        return false;
    }

    size_t size() const override {
        return _content.size();
    }

    bool read( char *buffer, size_t, size_t &length ) override {
        if ( length > _content.size() )
            length = _content.size();
        if ( length == 0 )
            return true;

        auto b = _content.begin();
        auto e = b + length;
        std::copy( b, e, buffer );

        _content.erase( b, e );
        return true;
    }

    bool write( const char *buffer, size_t offset, size_t length ) override {
        if ( _content.size() < offset + length )
            _content.resize( offset + length );
        std::copy( buffer, buffer + length, _content.begin() + offset );
        return true;
    }
private:
    utils::Vector< char > _content;
};

struct Socket : public FSItem {
    // TODO: implement
};

} // namespace fs
} // namespace divine

#endif
