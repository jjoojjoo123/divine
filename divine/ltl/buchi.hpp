/*
 * (c) 2017 Tadeáš Kučera <>
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

#include "ltl.hpp"
#include <string>
#include <algorithm>
#include <map>
#include <cassert>
#include <stack>

#ifndef LTL2C_BUCHI_H
#define LTL2C_BUCHI_H



namespace divine {
namespace ltl {


size_t uCount = 0; // number of Until subformulae  ==  number of possible classes of states

size_t newNodeId();
size_t newClassId();


struct Node;
struct State;
struct TGBA;
struct TGBA2;
using NodePtr = std::shared_ptr< Node >;
using StatePtr = std::shared_ptr< State >;


struct State
{
    struct Comparator
    {
        bool operator()( StatePtr s1, StatePtr s2 ) const
        {
            return s1->id < s2->id;
        }
    };
    struct Edge
    {
        std::set< size_t > sources; // ids of states
        std::set< LTLPtr, LTLComparator2 > label; // set of literals that must hold if choosing this edge
        std::vector< bool > accepting; // says to which accepting sets this edge belongs
        Edge( const std::set< size_t >& _sources, const std::set< LTLPtr, LTLComparator2 >& _label, const std::vector< bool >& _accepting )
            : sources( _sources )
            , label( _label )
            , accepting( _accepting )
        {
        }
        friend std::ostream& operator<<( std::ostream & os, const Edge& e ) {
            os << "(";
            for( const auto & source : e.sources )
                os << source << ", ";
            os << " --- ";
            for( auto ltlIt = e.label.begin(); ltlIt != e.label.end(); ) {
                os << (*ltlIt)->string();
                if( ++ltlIt != e.label.end() )
                    os << " & ";
            }
            os << " --> . Accepting = ";
            for( auto b : e.accepting ) {
                os << b;
            }
            os << ")";
            return os;
        }

        std::string accSets() const {
            std::stringstream output;
            output << " {";
            bool flag = false;
            for( size_t i = 0; i != accepting.size(); ++i )
                if( accepting[i] ) {
                    if( flag )
                        output << " ";
                    flag = true;
                    output << i;
                }
            if( !flag )
                return std::string();
            output << "}";
            return output.str();
        }
    };

    size_t id;
    std::vector< Edge > edgesIn;
    std::set< LTLPtr, LTLComparator > next; //TODO use second comparator?

    State() = delete;
    State( const State& other )
        : id( other.id )
        , edgesIn( other.edgesIn )
        , next( other.next )
    {
    }
    State( Node* node );

    void addEdge( const std::set< size_t >& _sources, const std::set< LTLPtr, LTLComparator2 >& _label, const std::vector< bool >& _accepting )
    {
        edgesIn.emplace_back( _sources, _label, _accepting );
    }
    void merge( Node* node );

    friend std::ostream& operator<<( std::ostream & os, const State& s ) {
        os << "  State " << s.id << std::endl;
        for( const auto & e : s.edgesIn )
            os << e << ", ";
        os << std::endl;
        return os;
    }
};


struct Node
{
    struct Comparator
    {
        bool operator()( NodePtr nodeA, NodePtr nodeB ) const
        {
            return nodeA->id == nodeB->id;
        }
    };

    size_t id; // unique identifier for the node
    std::set< size_t > incomingList; // the list of predecessor nodes
    std::set< LTLPtr, LTLComparator > old; // LITERAL subformulas of Phi already processed - must hold in corresponding state
    std::set< LTLPtr, LTLComparator > toBeDone; // subformulas of Phi yet to be processed
    std::set< LTLPtr, LTLComparator > next; // subformulas of Phi that must hold in immediate successors of states that satisfy all properties in Old
    std::vector< bool > untils;
    std::vector< bool > rightOfUntils;

    static size_t depthOfRecursion;
    size_t localDepthOfRecursion = 1;

    void print() const;

    Node()
        : id( newNodeId() )
    {
        untils.resize( uCount );
        rightOfUntils.resize( uCount );
        resetUntils();
        resetRightOfUntils();
    }

    Node( const Node& o )
        : id( newNodeId() )
        , incomingList( o.incomingList )
        , old( o.old )
        , toBeDone( o.toBeDone )
        , next( o.next )
        , untils( o.untils )
        , rightOfUntils( o.rightOfUntils )
    {
    }

    void resetUntils( )
    {
        for( bool b : untils )
            b = false;
    }
    void resetRightOfUntils( )
    {
        for( bool b : rightOfUntils )
            b = false;
    }
    /**
      * Finds (first) "node with same old and next" in specified set of nodes
      * @return node with same old and next / nullptr iff not present
    **/
    StatePtr findTwin( const std::set< StatePtr, State::Comparator >& states );

    NodePtr split( LTLPtr form );

    bool isinSI( LTLPtr phi, const std::set< LTLPtr, LTLComparator >& A, const std::set< LTLPtr, LTLComparator >& B );

    bool contradics( LTLPtr phi ); //true if neg(phi) in SI(node.old, node.next)

    bool isRedundant( LTLPtr phi );

    std::set< StatePtr, State::Comparator > expand( std::set< StatePtr, State::Comparator >& states );
};

/**
 * Finds (first) "node with same old and next" in given set of nodes
 * @return node with same old and next / nullptr iff not present
 * */
NodePtr findTwin( NodePtr nodeP, const std::set< NodePtr, Node::Comparator >& list )
{
    for ( auto nodeOther: list )
        if ( ( nodeP->old == nodeOther->old ) && ( nodeP->next == nodeOther->next ) )
            return nodeOther;
    return nullptr;
}

struct TGBA1 {
    LTLPtr formula;
    std::vector< StatePtr > states;
    std::vector< std::vector< State::Edge > > acceptingSets;
    std::vector< LTLPtr > allLiterals;
    std::vector< LTLPtr > allTrivialLiterals; //those which do not have negation

    TGBA1( LTLPtr _formula, std::set< StatePtr, State::Comparator >& _states )
        : formula( _formula )
    {
        std::copy( _states.begin(), _states.end(), std::back_inserter( states ) );
        std::set< LTLPtr, LTLComparator2 > allLiteralsSet;
        acceptingSets.resize( uCount );

        std::vector< size_t > ids;
        for( auto state : states )
            ids.push_back( state->id );
        std::sort( ids.begin(), ids.end() );
        std::map< size_t, size_t > idsMap;
        for( size_t i = 0; i < ids.size(); ++i )
            idsMap.emplace( std::make_pair( ids[i], i ) );

        for( auto state : states )
        {
            state->id = idsMap.at( state->id );
            for( State::Edge & edge : state->edgesIn )
            {
                std::set< size_t > newSources;
                for( size_t s : edge.sources )
                    newSources.insert( idsMap.at( s ) );
                edge.sources = newSources;
                for( size_t i = 0; i < uCount; ++i )
                    if( edge.accepting[i] )
                        acceptingSets[i].push_back( edge );
                for( auto l : edge.label ) {
                    if( !l->is< Boolean >() ) {
                        allLiteralsSet.insert( l );
                        if( l->isType( Unary::Neg ) )
                            allLiteralsSet.insert( l->get< Unary >().subExp );
                    }
                }
            }
        }
        for( auto l : allLiteralsSet ) {
            allLiterals.emplace_back( l );
            if( !l->isType( Unary::Neg ) )
                allTrivialLiterals.emplace_back( l );
        }
        for( size_t i = 0; i < states.size(); ++i )
            assert( i == states.at( i )->id );
        assert( allLiterals.size() == allLiteralsSet.size() );
    }
    void print() const
    {
        std::cout << "TGBA made from formula " << formula->string() << ":" << std::endl;
        for( auto state : states )
            std::cout << *state;
    }

    std::string indexOfLiteral( LTLPtr literal ) const {
        std::stringstream output;
        auto wanted = literal->string();
        for( size_t i = 0; i < allTrivialLiterals.size(); ++i ) {
            if( allTrivialLiterals[i]->string() == wanted ) {
                output << i;
                return output.str();
            }
            if( "!" + allTrivialLiterals[i]->string() == wanted ) {
                output << "!" << i;
                return output.str();
            }
        }
        std::cerr << std::endl<< std::endl << "Literal " << wanted << " not found in " << std::endl;
        for( auto l : allTrivialLiterals )
            std::cerr << l->string() << ", ";
        std::cerr << std::endl;
        assert( false && "unused literal in TGBA" );
        return "error";
    }

    friend std::ostream& operator<<( std::ostream & os, const TGBA1& tgba ) {
        os << "HOA: v1" << std::endl;
        os << "name: \"" << tgba.formula->string() << "\"" << std::endl;
        os << "States: " << tgba.states.size() << std::endl;
        os << "Start: 0" << std::endl;
        os << "AP: " << tgba.allTrivialLiterals.size();
        for( auto literal : tgba.allTrivialLiterals )
            os << " \"" << literal->string() << "\"";
        if( uCount == 0 )
            os << std::endl << "acc-name: all" << std::endl;
        else if( uCount == 1 )
            os << std::endl << "acc-name: Buchi" << std::endl;
        else
            os << std::endl << "acc-name: generalized-Buchi " << uCount << std::endl;
        os << "Acceptance: " << uCount;
        if( uCount == 0 )
            os << " t";
        for( size_t i = 0; i < uCount; ++i) {
            os << " Inf(" << i << ")";
            if ( i + 1 != uCount )
                os << "&";
        }
        os << std::endl;
        os << "properties: trans-labels trans-acc " << std::endl;
        os << "--BODY--" << std::endl;
        for( auto state : tgba.states ) {
            os << "State: " << state->id << std::endl;
            for( auto s : tgba.states ) {
                for( const auto & e : s->edgesIn ) {
                    if( e.sources.count( state->id ) != 0 ) { //this transition goes from state state
                        os << "[";
                        bool flag = false;
                        for( auto l : e.label ) {
                            if( flag )
                                os << "&";
                            os << tgba.indexOfLiteral( l );
                            flag = true;
                        }
                        if( !flag )
                            os << "t";
                        os << "] " << s->id;
                        os << e.accSets() << std::endl;
                    }
                }
            }
        }
        os << "--END--" << std::endl;
        return os;
    }
};

struct Transition{
    size_t target;
    std::set< std::pair< bool, size_t > > label; // of indices of literals that must hold if choosing this transition
    std::set< size_t > accepting; // says to which accepting sets this transition belongs to
    Transition( size_t _target, std::set< std::pair< bool, size_t > > _label, std::set< size_t > _accepting )
        : target( _target )
        , label( _label )
        , accepting( _accepting )
    {
    }
    friend std::ostream& operator<<( std::ostream & os, const Transition& t )
    {
        os << "[";
        bool flag = false;
        for( auto l : t.label ) {
            if( flag )
                os << "&";
            if( !l.first )
                os << "!";
            os << l.second;
            flag = true;
        }
        if( !flag )
            os << "t";
        os << "] " << t.target;
        if( !t.accepting.empty() ) {
            os << " {";
            bool flag = false;
            for( size_t acc : t.accepting ) {
                if( flag )
                    os << " ";
                flag = true;
                os << acc;
            }
            os << "}";
        }
        return os;
    }
};

std::pair< bool, size_t > indexOfLiteral( LTLPtr literal, const std::vector< LTLPtr >& literals ) {
    auto wanted = literal->string();
    for( size_t i = 0; i < literals.size(); ++i ) {
        if( literals.at( i )->string() == wanted )
            return std::make_pair( true, i );
        if( "!" + literals.at( i )->string() == wanted )
            return std::make_pair( false, i );
    }
    std::cerr << std::endl<< std::endl << "Literal " << wanted << " not found in " << std::endl;
    assert( false && "unused literal in TGBA" );
    return std::make_pair( true, 0 );
}


/*
 * While TGBA1 is designed such a way that in each node it stores all the transitions
 * that are incoming in the node (which is needed for the translation process), the
 * TGBA2 structure stores for each node all the transitions that go out of it. This
 * property is needed for the product construction as well as for the computation of
 * the accepting strongly connected components: Both representations TGBA1 and
 * TGBA2 of the same automata A together give us access to A and A^T both efficiently.
*/
struct TGBA2 {
    TGBA1 tgba1;
    LTLPtr formula;
    size_t nStates;
    size_t start = 0;
    size_t nAcceptingSets;
    std::vector< LTLPtr > allLiterals;
    std::vector< LTLPtr > allTrivialLiterals; //those which do not have negation
    std::vector< std::vector< Transition > > states;
    std::vector< std::optional< size_t > > accSCC; //each state is assigned id of optional acc. SCC

    TGBA2( TGBA1&& _tgba )
        : tgba1( _tgba )
        , formula( tgba1.formula )
        , nStates( tgba1.states.size() )
        , nAcceptingSets( tgba1.acceptingSets.size() )
        , allLiterals( tgba1.allLiterals )
        , allTrivialLiterals( tgba1.allTrivialLiterals )
    {
        states.resize( tgba1.states.size() );
        for( auto state : tgba1.states ) {
            assert( state->id < tgba1.states.size() );
            for( const auto& edge : state->edgesIn ) {
                std::set< size_t > acc;
                for( size_t i = 0; i < edge.accepting.size(); ++i )
                    if( edge.accepting.at( i ) )
                        acc.insert( i );
                std::set< std::pair< bool, size_t > > label;
                for( auto literal : edge.label )
                    label.insert( indexOfLiteral( literal, allTrivialLiterals ) );
                for( size_t s : edge.sources )
                    states.at( s ).emplace_back( state->id, label, acc );
            }
        }
        computeAccSCC();
    }
    friend std::ostream& operator<<( std::ostream & os, const TGBA2& tgba2 )
    {
        os << "HOA: v1" << std::endl;
        os << "name: \"" << tgba2.formula->string() << "\"" << std::endl;
        os << "States: " << tgba2.nStates << std::endl;
        os << "Start: 0" << std::endl;
        os << "AP: " << tgba2.allTrivialLiterals.size();
        for( auto literal : tgba2.allTrivialLiterals )
            os << " \"" << literal->string() << "\"";
        if( uCount == 0 )
            os << std::endl << "acc-name: all" << std::endl;
        else if( uCount == 1 )
            os << std::endl << "acc-name: Buchi" << std::endl;
        else
            os << std::endl << "acc-name: generalized-Buchi " << uCount << std::endl;
        os << "Acceptance: " << tgba2.nAcceptingSets;
        if( uCount == 0 )
            os << " t";
        for( size_t i = 0; i < uCount; ++i) {
            os << " Inf(" << i << ")";
            if ( i + 1 != uCount )
                os << "&";
        }
        os << std::endl;
        os << "properties: trans-labels trans-acc " << std::endl;
        os << "--BODY--" << std::endl;
        for( size_t stateId = 0; stateId < tgba2.states.size(); ++stateId ) {
            os << "State: " << stateId << std::endl;
            for( const auto& trans : tgba2.states.at( stateId ) )
                os << trans << std::endl;
        }
        os << "--END--" << std::endl;
        return os;
    }

    void DFSUtil1( StatePtr state, std::vector< size_t >& visited, std::vector< size_t >& leaved, std::stack< size_t >& leavedStack, size_t& counter )
    {
        visited.at( state->id ) = ++counter;
        for( const auto& edge : state->edgesIn )
            for( size_t child : edge.sources )
            {
                if( visited.at(child) == 0 )
                    DFSUtil1( tgba1.states.at( child ), visited, leaved, leavedStack, counter );
            }
        leaved.at( state->id ) = ++counter;
        leavedStack.push( state->id );
    }
    //returns vector of all states in accepting SCC containing state state
    void DFSUtil2( size_t state, std::set< size_t >& comp, std::vector< size_t >& visited, std::vector< size_t >& leaved, size_t& counter ) {
        visited[state] = ++counter;
        for( const auto& trans : states.at( state ) )
            if( visited.at( trans.target ) == 0 )
                DFSUtil2( trans.target, comp, visited, leaved, counter );
        comp.insert( state );
        leaved.at( state ) = ++counter;
    }
    bool isAccepting( const std::set< size_t >& component ) {
        if( component.empty() )
            return false;
        std::set< size_t > presentColors;
        for( size_t s : component ) {
            for( const Transition& trans : states.at( s ) ) {
                if( component.count( trans.target ) != 0 )
                    presentColors.insert( trans.accepting.begin(), trans.accepting.end() );
            }
        }
        return presentColors.size() == nAcceptingSets;
    }
    //returns number of accepting components in the TGBA
    size_t computeAccSCC() {
        accSCC.resize( states.size() );
        // first run dfs in G^T (tgba1) to determine the order of states
        std::vector< size_t > visited( states.size(), 0 );
        std::vector< size_t > leaved( states.size(), 0 );
        std::stack< size_t > leavedStack;

        size_t counter = 0;
        for( StatePtr state : tgba1.states )
        {
            if( visited[state->id] == 0 ) //state was not visited yet -> start from it
                DFSUtil1( state, visited, leaved, leavedStack, counter );
        }
        // second round
        leaved = std::vector< size_t >( states.size(), 0 );
        visited = std::vector< size_t >( states.size(), 0 );
        counter = 0;
        size_t componentCounter = 0;

        while( !leavedStack.empty() )
        {
            size_t state = leavedStack.top();
            leavedStack.pop();
            if( visited[state] == 0 ) //state was not visited yet -> start from it
            {
                std::set< size_t > component;
                DFSUtil2( state, component, visited, leaved, counter );
                if( isAccepting( component ) ) {
                    ++componentCounter;
                    //std::cout << "Accepting component:" ;
                    for( size_t s : component ) {
                        //std::cout << ", " << s;
                        accSCC.at( s ) = componentCounter;
                    }
                }
            }
        }
        return componentCounter;
    }
};



TGBA1 ltlToTGBA1( LTLPtr _formula, bool negate )
{
    LTLPtr formula = _formula->normalForm( negate );
    uCount = formula->countAndLabelU();
    formula->computeUParents();

    NodePtr init = std::make_shared< Node >();

    init->next.insert( formula );
    std::set< StatePtr, State::Comparator > states;
    states = init->expand( states ); //Expanding the initial node

    TGBA1 tbga( formula, states );
    return tbga;
}

struct LTL2TGBA /* normal forms */
{
    TEST(simpleFormulae)
    {
        std::string str( " a U b " );
        LTLPtr ltl = LTL::parse( str );
        auto tgba1 = ltlToTGBA1( ltl, false );
    }
};

}
}

#endif //LTL2C_BUCHI_H
