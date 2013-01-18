// -*- C++ -*- (c) 2009, 2012 Petr Rockai <me@mornfall.net>

#include <divine/graph/graph.h>

#ifndef DIVINE_GRAPH_POR_H
#define DIVINE_GRAPH_POR_H

namespace divine {
namespace graph {

template< typename G >
struct NonPORGraph : graph::Transform< G > {

    typedef typename G::Node Node;

    bool eliminate_done;

    NonPORGraph() : eliminate_done( false ) {}

    void porExpansion( Node ) {}
    void porTransition( Node, Node, void (*)( Node, int ) ) {}
    bool full( Node ) { return true; }

    template< typename Yield >
    void fullexpand( Yield, Node ) {}

    template< typename Algorithm >
    void _porEliminate( Algorithm & ) {}

    template< typename Domain, typename Alg >
    bool porEliminate( Domain &, Alg & ) {
        eliminate_done = true;
        return false;
    }

    template< typename Table >
    bool porEliminateLocally( Table & ) {
        eliminate_done = true;
        return false;
    }

    template< typename Yield >
    void initials( Yield yield ) {
        if ( eliminate_done )
            return;
        this->base().initials( yield );
    }
};

}
}

#endif
