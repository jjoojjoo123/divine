// -*- C++ -*- (c) 2007, 2008 Petr Rockai <me@mornfall.net>

#include <divine/algorithm.h>
#include <divine/visitor.h>
#include <divine/parallel.h>
#include <divine/report.h>

#ifndef DIVINE_OWCTY_H
#define DIVINE_OWCTY_H

namespace divine {
namespace algorithm {

template< typename > struct Owcty;

// ------------------------------------------
// -- Some drudgery for MPI's sake
// --
template< typename G >
struct _MpiId< Owcty< G > >
{
    static void (Owcty< G >::*from_id( int n ))()
    {
        switch ( n ) {
            case 0: return &Owcty< G >::_initialise;
            case 1: return &Owcty< G >::_reachability;
            case 2: return &Owcty< G >::_elimination;
            case 3: return &Owcty< G >::_counterexample;
            case 4: return &Owcty< G >::_checkCycle;
            case 5: return &Owcty< G >::_parentTrace;
            case 6: return &Owcty< G >::_traceCycle;
            default: assert_die();
        }
    }

    static int to_id( void (Owcty< G >::*f)() ) {
        if( f == &Owcty< G >::_initialise ) return 0;
        if( f == &Owcty< G >::_reachability ) return 1;
        if( f == &Owcty< G >::_elimination ) return 2;
        if( f == &Owcty< G >::_counterexample ) return 3;
        if( f == &Owcty< G >::_checkCycle) return 4;
        if( f == &Owcty< G >::_parentTrace) return 5;
        if( f == &Owcty< G >::_traceCycle) return 6;
        assert_die();
    }

    template< typename O >
    static void writeShared( typename Owcty< G >::Shared s, O o ) {
        *o++ = s.size;
        *o++ = s.oldsize;
        *o++ = s.iteration;
        *o++ = s.cycle_found;
        *o++ = s.cycle_node.valid();
        if ( s.cycle_node.valid() )
            o = s.cycle_node.write32( o );
    }

    template< typename I >
    static I readShared( typename Owcty< G >::Shared &s, I i ) {
        s.size = *i++;
        s.oldsize = *i++;
        s.iteration = *i++;
        s.cycle_found = *i++;
        bool valid = *i++;
        if ( valid ) {
            FakePool fp;
            i = s.cycle_node.read32( &fp, i );
        }
        return i;
    }
};

template< typename G >
struct Owcty : Algorithm, DomainWorker< Owcty< G > >
{
    typedef typename G::Node Node;
 
    // -------------------------------
    // -- Some useful types
    // --

    struct Shared {
        size_t size, oldsize;
        Node cycle_node;
        bool cycle_found;
        int iteration;
        G g;
    } shared;

    struct Extension {
        Blob parent;
        uintptr_t map;
        size_t predCount:15;
        size_t iteration:15;
        bool inS:1;
        bool inF:1;
    };

    Hasher hasher;

    typedef HashMap< Node, Unit, Hasher,
                     divine::valid< Node >, Equal > Table;

    Domain< Owcty< G > > *m_domain;
    Table *m_table;

    bool want_ce;

    Domain< Owcty< G > > &domain() {
        if ( !m_domain ) {
            assert( this->m_master );
            return *this->m_master;
        }
        return *m_domain;
    }

    // ------------------------------------------------------
    // -- generally useful utilities
    // --

    Table &table() {
        if ( !m_table ) {
            hasher.setSlack( sizeof( Extension ) );
            m_table = new Table( hasher, divine::valid< Node >(),
                                 Equal( hasher.slack ) );
        }
        return *m_table;
    }

    Extension &extension( Node n ) {
        return n.template get< Extension >();
    }

    bool cycleFound() {
        for ( int i = 0; i < domain().peers(); ++i ) {
            if ( domain().shared( i ).cycle_found )
                return true;
        }
        return false;
    }

    Node cycleNode() {
        for ( int i = 0; i < domain().peers(); ++i ) {
            if ( domain().shared( i ).cycle_found )
                return domain().shared( i ).cycle_node;
        }
        assert_die();
    }

    int totalSize() {
        int sz = 0;
        for ( int i = 0; i < domain().peers(); ++i ) {
            Shared &s = domain().shared( i );
            sz += s.size;
        }
        return sz;
    }

    void resetSize() {
        shared.size = 0;
        for ( int i = 0; i < domain().peers(); ++i ) {
            Shared &s = domain().shared( i );
            s.size = 0;
        }
    }

    template< typename V >
    void queueAll( V &v, bool reset = false ) {
        for ( size_t i = 0; i < table().size(); ++i ) {
            Node st = table()[ i ].key;
            if ( st.valid() ) {
                if ( reset )
                    extension( st ).predCount = 0;
                if ( extension( st ).inS && extension( st ).inF ) {
                    assert_eq( v.owner( st ), v.worker.globalId() );
                    v.queue( Blob(), st );
                }
            }
        }
    }

    uintptr_t mapId( Node t ) {
        return reinterpret_cast< uintptr_t >( t.ptr );
    }

    bool updateIteration( Node t, int n ) {
        int old = extension( t ).iteration;
        extension( t ).iteration = 2 * shared.iteration + n;
        return old != 2 * shared.iteration + n;
    }

    // -----------------------------------------------
    // -- REACHABILITY implementation
    // --

    visitor::ExpansionAction reachExpansion( Node st )
    {
        assert( extension( st ).predCount > 0 );
        assert( extension( st ).inS );
        ++ shared.size;
        return visitor::ExpandState;
    }

    visitor::TransitionAction reachTransition( Node f, Node t )
    {
        ++ extension( t ).predCount;
        extension( t ).inS = true;
        if ( extension( t ).inF && f.valid() )
            return visitor::ForgetTransition;
        else {
            return updateIteration( t, 0 ) ?
                visitor::ExpandTransition :
                visitor::ForgetTransition;
        }
    }

    void _reachability() { // parallel
        typedef visitor::Setup< G, Owcty< G >, Table,
            &Owcty< G >::reachTransition,
            &Owcty< G >::reachExpansion > Setup;
        typedef visitor::Parallel< Setup, Owcty< G >, Hasher > Visitor;

        hasher.setSlack( sizeof( Extension ) );
        Visitor visitor( shared.g, *this, *this, hasher, &table() );
        queueAll( visitor, true );
        visitor.visit();
    }

    void reachability() {
        shared.size = 0;
        domain().parallel().run( shared, &Owcty< G >::_reachability );
        shared.size = totalSize();
    }

    // -----------------------------------------------
    // -- INITIALISE implementation
    // --

    visitor::TransitionAction initTransition( Node from, Node to )
    {
        if ( !extension( to ).parent.valid() )
            extension( to ).parent = from;
        if ( from.valid() ) {
            uintptr_t fromMap = extension( from ).map;
            uintptr_t fromId = mapId( from );
            if ( fromMap > extension( to ).map )
                extension( to ).map = fromMap;
            if ( shared.g.isAccepting( from ) ) {
                if ( from.pointer() == to.pointer() ) {
                    shared.cycle_node = to;
                    shared.cycle_found = true;
                    return visitor::TerminateOnTransition;
                }
                if ( fromId > extension( to ).map )
                    extension( to ).map = fromId;
            }
            if ( mapId( to ) == fromMap ) {
                shared.cycle_node = to;
                shared.cycle_found = true;
                return visitor::TerminateOnTransition;
            }
        }
        return visitor::FollowTransition;
    }

    visitor::ExpansionAction initExpansion( Node st )
    {
        extension( st ).inF = extension( st ).inS = shared.g.isAccepting( st );
        shared.size += extension( st ).inS;
        return visitor::ExpandState;
    }

    void _initialise() { // parallel
        typedef visitor::Setup< G, Owcty< G >, Table,
            &Owcty< G >::initTransition,
            &Owcty< G >::initExpansion > Setup;
        typedef visitor::Parallel< Setup, Owcty< G >, Hasher > Visitor;

        Visitor visitor( shared.g, *this, *this,
                         Hasher( sizeof( Extension ) ), &table() );
        visitor.visit( shared.g.initial() );
    }

    void initialise() {
        domain().parallel().run( shared, &Owcty< G >::_initialise );
        shared.oldsize = shared.size = totalSize();
    }

    // -----------------------------------------------
    // -- ELIMINATION implementation 
    // --

    visitor::ExpansionAction elimExpansion( Node st )
    {
        assert( extension( st ).predCount == 0 );
        extension( st ).inS = false;
        if ( extension( st ).inF )
            shared.size ++;
        return visitor::ExpandState;
    }

    visitor::TransitionAction elimTransition( Node f, Node t )
    {
        assert( t.valid() );
        assert( extension( t ).inS );
        assert( extension( t ).predCount >= 1 );
        -- extension( t ).predCount;
        // we follow a transition only if the target state is going to
        // be eliminated
        if ( extension( t ).predCount == 0 ) {
            return updateIteration( t, 1 ) ?
                visitor::ExpandTransition :
                visitor::ForgetTransition;
        } else
            return visitor::ForgetTransition;
    }

    void _elimination() {
        typedef visitor::Setup< G, Owcty< G >, Table,
            &Owcty< G >::elimTransition,
            &Owcty< G >::elimExpansion > Setup;
        typedef visitor::Parallel< Setup, Owcty< G >, Hasher > Visitor;

        Visitor visitor( shared.g, *this, *this,
                         Hasher( sizeof( Extension ) ), &table() );
        queueAll( visitor );
        visitor.visit();
    }

    void elimination() {
        shared.size = 0;
        domain().parallel().run( shared, &Owcty< G >::_elimination );
        shared.oldsize = shared.size = shared.oldsize - totalSize();
    }


    // -------------------------------------------
    // -- COUNTEREXAMPLES (to be done)
    // --

    visitor::TransitionAction ccTransition( Node from, Node to )
    {
        if ( from.valid() && to == shared.cycle_node ) {
            shared.cycle_found = true;
            return visitor::TerminateOnTransition;
        }

        return updateIteration( to, 0 ) ?
            visitor::ExpandTransition :
            visitor::ForgetTransition;
    }

    visitor::ExpansionAction ccExpansion( Node ) {
        return visitor::ExpandState;
    }

    void _checkCycle() {
        typedef visitor::Setup< G, Owcty< G >, Table,
            &Owcty< G >::ccTransition,
            &Owcty< G >::ccExpansion > Setup;
        typedef visitor::Parallel< Setup, Owcty< G >, Hasher > Visitor;

        Visitor visitor( shared.g, *this, *this,
                         Hasher( sizeof( Extension ) ), &table() );
        assert( shared.cycle_node.valid() );
        if ( visitor.owner( shared.cycle_node ) == this->globalId() )
            visitor.queue( Blob(), shared.cycle_node );
        visitor.visit();
    }

    void _counterexample() {
        for ( int i = 0; i < table().size(); ++i ) {
            if ( cycleFound() ) {
                shared.cycle_node = cycleNode();
                shared.cycle_found = true;
                return;
            }
            Node st = shared.cycle_node = table()[ i ].key;
            if ( !st.valid() )
                continue;
            if ( extension( st ).iteration == 2 * shared.iteration )
                continue; // already seen
            if ( !extension( st ).inS || !extension( st ).inF )
                continue;
            domain().parallel().run( shared, &Owcty< G >::_checkCycle );
            if ( cycleFound() ) {
                std::cerr << std::endl << "===== Accepting state ====="
                          << std::endl << std::endl;
                std::cerr << shared.g.showNode( cycleNode() ) << std::endl;
            }
        }
    }

    visitor::ExpansionAction traceExpansion( Node n ) {
        std::cerr << shared.g.showNode( n ) << std::endl;
        return visitor::ExpandState;
    }

    visitor::TransitionAction traceTransition( Node, Node to ) {
        if ( updateIteration( to, 0 ) ) {
            // std::cerr << std::endl;
            return visitor::ExpandTransition;
        }
        return visitor::ForgetTransition;
    }

    void _parentTrace() {
        typedef ParentGraph< G, Extension > PG;
        typedef visitor::Setup< PG, Owcty< G >, Table,
            &Owcty< G >::traceTransition,
            &Owcty< G >::traceExpansion > VisitorSetup;

        PG pg( shared.cycle_node );
        visitor::Parallel< VisitorSetup, Owcty< G >, Hasher >
            vis( pg, *this, *this, hasher, &table() );

        if ( vis.owner( shared.cycle_node ) == this->globalId() )
            vis.queue( Blob(), shared.cycle_node );
        vis.visit();
    }

    visitor::ExpansionAction cycleExpansion( Node n ) {
        return visitor::ExpandState;
    }

    visitor::TransitionAction cycleTransition( Node from, Node to ) {
        if ( from.valid() && to == shared.cycle_node ) {
            extension( to ).parent = from;
            return visitor::TerminateOnTransition;
        }
        if ( updateIteration( to, 1 ) ) {
            extension( to ).parent = from;
            return visitor::ExpandTransition;
        }
        return visitor::ForgetTransition;
    }

    void _traceCycle() {
        typedef visitor::Setup< G, Owcty< G >, Table,
            &Owcty< G >::cycleTransition,
            &Owcty< G >::cycleExpansion > Setup;
        typedef visitor::Parallel< Setup, Owcty< G >, Hasher > Visitor;

        Visitor visitor( shared.g, *this, *this,
                         Hasher( sizeof( Extension ) ), &table() );
        assert( shared.cycle_node.valid() );
        if ( visitor.owner( shared.cycle_node ) == this->globalId() )
            visitor.queue( Blob(), shared.cycle_node );
        visitor.visit();
    }

    void counterexample() {
        if ( !cycleFound() ) {
            std::cerr << "obtaining counterexample..." << std::endl;
            domain().parallel().runInRing(
                shared, &Owcty< G >::_counterexample );
        }

        assert( cycleFound() );
        shared.cycle_node = cycleNode();

        ++ shared.iteration;

        std::cerr << std::endl << "===== Trace to initial ====="
                  << std::endl << std::endl;
        domain().parallel().run( shared, &Owcty< G >::_parentTrace );
        domain().parallel().run( shared, &Owcty< G >::_traceCycle );

        ++ shared.iteration;

        std::cerr << std::endl << "===== The cycle ====="
                  << std::endl << std::endl;
        domain().parallel().run( shared, &Owcty< G >::_parentTrace );
    }

    // -------------------------------------------
    // -- MAIN LOOP implementation
    // --

    void printSize() {
        if ( cycleFound() )
            std::cerr << "   (MAP: cycle found)" << std::endl << std::flush;
        else
            std::cerr << "   |S| = " << shared.size << std::endl << std::flush;
    }

    void printIteration( int i ) {
        std::cerr << "------------- iteration " << i
                  << " ------------- " << std::endl;
    }

    Result run()
    {
        size_t oldsize = 0;

        std::cerr << " initialise...\t\t" << std::flush;
        shared.size = 0;
        initialise();
        printSize();

        shared.iteration = 1;

        if ( !cycleFound() ) {
            do {
                oldsize = shared.size;

                printIteration( shared.iteration );

                std::cerr << " reachability...\t" << std::flush;
                reachability();
                printSize();

                std::cerr << " elimination & reset...\t" << std::flush;
                elimination();
                printSize();

                ++shared.iteration;

            } while ( oldsize != shared.size && shared.size != 0 );
        }

        bool valid = cycleFound() ? false : ( shared.size == 0 );
        this->resultBanner( valid );

        if ( want_ce && !valid )
            counterexample();

        Result res;
        res.ltlPropertyHolds = valid ? Result::Yes : Result::No;
        res.fullyExplored = shared.cycle_node.valid() ? Result::No : Result::Yes;
        return res;
    }

    Owcty( Config *c = 0 )
        : m_table( 0 )
    {
        shared.g.setSlack( sizeof( Extension ) );
        shared.cycle_found = false;
        shared.size = 0;
        m_domain = 0;
        if ( c ) {
            shared.g.read( c->input() );
            want_ce = c->generateCounterexample();
            m_domain = new Domain< Owcty< G > >( &shared, workerCount( c ) );
        }
    }

    ~Owcty() {
        if ( m_domain )
            delete m_domain;
    }
};

}
}

#endif
