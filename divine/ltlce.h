// -*- C++ -*- (c) 2009 Petr Rockai <me@mornfall.net>

#include <sstream>

#ifndef DIVINE_LTLCE_H
#define DIVINE_LTLCE_H

namespace divine {
namespace algorithm {

template< typename Node >
struct CeShared {
    Node initial;
    Node current;
    bool current_updated;
    template< typename I >
    I read( I i ) {
        FakePool fp;

        if ( *i++ )
            i = initial.read32( &fp, i );
        if ( *i++ )
            i = current.read32( &fp, i );
        current_updated = *i++;
        return i;
    }

    template< typename O >
    O write( O o ) {
        *o++ = initial.valid();
        if ( initial.valid() )
            o = initial.write32( o );
        *o++ = current.valid();
        if ( current.valid() )
            o = current.write32( o );
        *o++ = current_updated;
        return o;
    }
};

template< typename G, typename Shared, typename Extension >
struct LtlCE {
    typedef typename G::Node Node;
    typedef LtlCE< G, Shared, Extension > Us;

    G *_g;
    Shared *_shared;

    std::ostream *_o_ce, *_o_trail;

    G &g() { assert( _g ); return *_g; }
    Shared &shared() { assert( _shared ); return *_shared; }

    LtlCE() : _g( 0 ), _shared( 0 ), _o_ce( 0 ), _o_trail( 0 ) {}
    ~LtlCE() {
        if ( ( _o_ce != &std::cerr ) && ( _o_ce != &std::cout ) )
            divine::safe_delete( _o_ce );
        if ( ( _o_trail != &std::cerr ) && ( _o_trail != &std::cout ) )
            divine::safe_delete( _o_trail );
    }

    template< typename Hash, typename Worker >
    int owner( Hash &hash, Worker &worker, Node n ) {
        return g().owner( hash, worker, n );
    }

    void setup( G &g, Shared &s )
    {
        _g = &g;
        _shared = &s;
    }

    Extension &extension( Node n ) {
        return n.template get< Extension >();
    }

    bool updateIteration( Node t ) {
        int old = extension( t ).iteration;
        extension( t ).iteration = shared().iteration;
        return old != shared().iteration;
    }

    // -------------------------------------
    // -- Parent trace extraction
    // --

    template< typename Worker, typename Hasher, typename Equal, typename Table >
    void _parentTrace( Worker &w, Hasher &h, Equal &eq, Table &t ) {
        if ( shared().ce.current_updated )
            return;
        if ( owner( h, w, shared().ce.current ) == w.globalId() ) {
            Node n = t.get( shared().ce.current );
            assert( n.valid() );
            shared().ce.current = extension( n ).parent;
            assert( shared().ce.current.valid() );
            shared().ce.current_updated = true;
        }
    }

    // -------------------------------------
    // -- Local cycle discovery
    // --

    visitor::ExpansionAction cycleExpansion( Node n ) {
        return visitor::ExpandState;
    }

    visitor::TransitionAction cycleTransition( Node from, Node to ) {
        if ( from.valid() && to == shared().ce.initial ) {
            extension( to ).parent = from;
            return visitor::TerminateOnTransition;
        }
        if ( updateIteration( to ) ) {
            extension( to ).parent = from;
            return visitor::ExpandTransition;
        }
        return visitor::ForgetTransition;
    }

    template< typename Worker, typename Hasher, typename Table >
    void _traceCycle( Worker &w, Hasher &h, Table &t ) {
        typedef visitor::Setup< G, Us, Table, NoStatistics,
            &Us::cycleTransition,
            &Us::cycleExpansion > Setup;
        typedef visitor::Partitioned< Setup, Worker, Hasher > Visitor;

        Visitor visitor( g(), w, *this, h, &t );
        assert( shared().ce.initial.valid() );
        if ( visitor.owner( shared().ce.initial ) == w.globalId() ) {
            shared().ce.initial = t.get( shared().ce.initial );
            visitor.queue( Blob(), shared().ce.initial );
        }
        visitor.processQueue();
    }

    template< typename Alg >
    int successorNum( Alg &a, G &_g, Node current, Node next )
    {
        typename G::Successors succ = _g.successors( current );
        int edge = 0;
        while ( !succ.empty() ) {
            ++ edge;
            if ( a.equal( succ.head(), next ) )
                break;
            _g.release( succ.head() ); // not it
            succ = succ.tail();
            assert( !succ.empty() ); // we'd hope the node is actually there!
        }

        while ( !succ.empty() ) {
            _g.release( succ.head() );
            succ = succ.tail();
        }

        assert_leq( 1, edge );
        return edge;
    }

    // Obtaining CE output

    template< typename Alg, typename T >
    std::vector< int > numericTrace( Alg &a, G &_g, T trace )
    {
        std::vector< int > result;
        while ( !trace.empty() ) {
            Node current = trace.back();
            trace.pop_back();

            if ( trace.empty() )
                break;

            result.push_back( successorNum( a, _g, current, trace.back() ) );
        }
        return result;
    }

    std::ostream &filestream( std::ostream *&stream, std::string file ) {
        if ( !stream ) {
            if ( file.empty() )
                stream = new std::stringstream();
            else if ( file == "-" )
                stream = &std::cerr;
            else
                stream = new std::ofstream( file.c_str() );
        }
        return *stream;
    }

    template < typename A >
    std::ostream &o_ce( A &a ) { return filestream( _o_ce, a.config().ceFile ); }
    template < typename A >
    std::ostream &o_trail( A &a ) { return filestream( _o_trail, a.config().trailFile ); }

    template< typename Alg, typename T >
    std::string generateTrace( Alg &a, G &_g, T trace )
    {
        std::stringstream o_tr_str;

        std::vector< int > ntrace = numericTrace( a, _g, trace );

        while ( !trace.empty() ) {
            o_ce( a ) << _g.showNode( trace.back() ) << std::endl;
            _g.release( trace.back() );
            trace.pop_back();
        }

        for ( int i = 0; i < ntrace.size(); ++i ) {
            o_trail( a ) << ntrace[ i ] << std::endl;
            o_tr_str << ntrace[ i ] << ",";
        }

        // drop trailing comma
        return std::string( o_tr_str.str(), 0, o_tr_str.str().length() - 1 );
    }

    template< typename Alg, typename T >
    void generateLinear( Alg &a, G &_g, T trace ) {
        o_ce( a ) << std::endl << "===== Trace from initial =====" << std::endl << std::endl;
        o_trail( a ) << "# from initial" << std::endl;
        a.result().iniTrail = generateTrace( a, _g, trace );
    }

    template< typename Alg, typename T >
    void generateLasso( Alg &a, G &_g, T trace ) {
        o_ce( a ) << std::endl << "===== The cycle =====" << std::endl << std::endl;
        o_trail( a ) << "# cycle" << std::endl;
        a.result().cycleTrail = generateTrace( a, _g, trace );
    }

    // ------------------------------------------------
    // -- Lasso counterexample generation
    // --

    template< typename Domain, typename Alg >
    std::vector< Node > parentTrace( Domain &d, Alg &a, Node stop ) {
        std::vector< Node > trace;
        shared().ce.current = shared().ce.initial;
        do {
            shared().ce.current_updated = false;
            trace.push_back( shared().ce.current );
            d.parallel().runInRing( shared(), &Alg::_parentTrace );
            assert( shared().ce.current_updated );
        } while ( !a.equal( shared().ce.current, stop ) );

        trace.push_back( shared().ce.current );

        return trace;
    }

    template< typename Domain, typename Alg >
    void linear( Domain &d, Alg &a )
    {
        generateLinear( a, g(), parentTrace( d, a, g().initial() ) );
    }

    template< typename Domain, typename Alg >
    void lasso( Domain &d, Alg &a ) {
        linear( d, a );
        ++ shared().iteration;
        d.parallel().run( shared(), &Alg::_traceCycle );

        generateLasso( a, g(), parentTrace( d, a, shared().ce.initial ) );
    }

};

}
}

#endif
