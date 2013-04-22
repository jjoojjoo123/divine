// -*- C++ -*- (c) 2013 Jan Kriho

#ifndef DIVINE_DVE_PREPROCESSOR_H
#define DIVINE_DVE_PREPROCESSOR_H

#include <divine/dve/parse.h>
#include <string>
#include <unordered_map>

namespace divine {
namespace dve {

namespace preprocessor {

struct Definition {
    std::string var, value;

    Definition( std::string def ) {
        int pos = def.find( "=" );
        if ( pos == std::string::npos ) {
            throw std::string( "Invalid definition: " + def );
        }
        var = def.substr( 0, pos );
        value = def.substr( pos + 1, std::string::npos );
    }
};

struct System {
    std::unordered_map< std::string, std::string > defs;

    void process( parse::System & ast ) {}

    System( std::vector< Definition > definitions ) {
        for ( Definition &d : definitions )
            defs[ d.var ] = d.value;
    }
};

}

}
}

#endif