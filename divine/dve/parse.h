// -*- C++ -*- (c) 2011 Petr Rockai
//             (c) 2012, 2013 Jan Kriho
#include <wibble/parse.h>
#include <divine/dve/lex.h>

#include <cassert>
#include <cstdlib> // atoi
#include <cstring>
#include <memory>

#ifndef DIVINE_DVE_PARSE_H
#define DIVINE_DVE_PARSE_H

namespace divine {
namespace dve {

struct SymTab;

/*
 * Avoid dependent scope in the parsers, which would make them quite *
 * unreadable. XXX use a virtualised StreamBase or such instead...
 */

struct IOStream {
    std::istream &i;

    std::string remove() {
        char block[1024];
        i.read( block, 1023 );
        block[i.gcount()] = 0;
        return block;
    }

    bool eof() {
        return i.eof();
    }

    IOStream( std::istream &i ) : i( i ) {}
};

typedef wibble::Parser< Token, Lexer< IOStream > > Parser;

namespace parse {

struct Constant : Parser {
    Token token;
    int value;

    void sign() {
        eat( Token::Minus );
        value = -1;
    }

    Constant( Context &c ) : Parser( c ) {
        value = 1;
        maybe( &Constant::sign );
        token = eat( Token::Constant );

        if ( token.data == "true" )
            value = 1;
        else if ( token.data == "false" )
            value = 0;
        else
            value = value * atoi( token.data.c_str() );
    }
    Constant() {}
};

struct Identifier : Parser {
    Token token;
    std::string name() const { return token.data; }
    Identifier( Context &c ) : Parser( c ) {
        token = eat( Token::Identifier );
    }

    Identifier( std::string name, Context &parent ) {
        std::stringstream realstream;
        realstream << name;
        dve::IOStream stream( realstream );
        dve::Lexer< dve::IOStream > lexer( stream );
        assert( &parent );
        *this = Identifier( parent.createChild( lexer, name ) );
    }

    Identifier() {}
};

struct Expression;

struct MacroNode : Parser {
    Identifier name;
    std::vector< Expression* > params;

    void param();

    MacroNode( Context &c ) : Parser( c ) {
        name = Identifier( context() );

        eat( Token::ParenOpen );
        do {
            maybe( &MacroNode::param );
        } while ( maybe( Token::Comma ) );
        eat( Token::ParenClose );
    }

    MacroNode( const MacroNode &mnode, bool independent );

    MacroNode() {}
};

struct RValue : Parser {
    Identifier ident;
    Constant value;
    Expression *idx;
    MacroNode mNode;

    void subscript();
    void macro() {
        mNode = MacroNode( context() );
    }

    void reference() {
        ident = Identifier( context() );
        maybe( &RValue::subscript );
    }

    void constant() {
        value = Constant( context() );
    }

    std::ostream& dump( std::ostream &o );

    RValue( Context &c ) : Parser( c ), idx( 0 ) {
        either( &RValue::macro, &RValue::reference, &RValue::constant );
    }

    RValue( const RValue &rval, bool independent );

    RValue() : idx( 0 ) {}
};

struct Expression : Parser {
    Token op;
    std::shared_ptr< Expression > lhs, rhs;
    std::shared_ptr< RValue > rval;

    void parens() {
        eat( Token::ParenOpen );
    }

    // TODO: Unary operators: minus and tilde (bitwise negation)
    void negation() {
        op = eat( Token::Bool_Not );
        Expression _lhs( context(), 0 );
        lhs.reset( new Expression( _lhs ) );
    }

    void rvalue() {
        RValue _rval( context() );
        rval.reset( new RValue( _rval ) );
    }

    std::ostream& dump( std::ostream &o ) {
        if ( lhs && rhs ) {
            o << "(";
            lhs->dump( o );
            o << ") " << op.data << " (";
            rhs->dump( o );
            o << ")";
        }
        else if ( lhs ) {
            o << op.data;
            lhs->dump( o );
        }
        else {
            rval->dump( o );
        }
        return o;
    }

    // TODO: Use the precedence climbing algorithm here, as it is likely
    // substantially more efficient
    Expression( Context &c, int prec = Token::precedences ) : Parser( c )
    {
        lhs = rhs = 0;
        rval = 0;

        if ( prec ) { // XXX: associativity
            Expression _lhs( context(), prec - 1 );
            lhs.reset( new Expression( _lhs ) );
            op = eat( false );
            if ( op.valid() && op.precedence( prec ) ) {
                Expression _rhs( context(), prec );
                rhs.reset( new Expression( _rhs ) );
            } else
                context().rewind( 1 );
        } else {
            if ( next( Token::ParenOpen ) ) {
                Expression _lhs( context() );
                eat( Token::ParenClose );
                lhs.reset( new Expression( _lhs ) );
            } else {
                either( &Expression::negation,
                        &Expression::rvalue );
            }
        }

        if ( !lhs && !rval )
            fail( "expression" );

        /* simplify expressions */
        if ( lhs && !rhs && !op.precedence( prec ) && !op.unary() ) {
            Expression ex = *lhs;
            lhs.reset();
            *this = ex;
        }
    }

    Expression( std::string name, Context &parent ) {
        std::stringstream realstream;
        realstream << name;
        dve::IOStream stream( realstream );
        dve::Lexer< dve::IOStream > lexer( stream );
        *this = Expression( parent.createChild( lexer, name ) );
    }

    Expression( const Expression &exp, bool independent ) : Expression( exp ) {
        if ( !independent )
            return;

        if ( exp.lhs )
            lhs.reset( new Expression( *exp.lhs, independent ) );

        if ( exp.rhs )
            rhs.reset( new Expression( *exp.rhs, independent ) );

        if ( exp.rval )
            rval.reset( new RValue( *exp.rval, independent ) );
    }

    Expression() : lhs( 0 ), rhs( 0 ), rval( 0 ) {}
};

struct ExpressionList : Parser {
    std::vector< Expression > explist;
    bool compound;
    
    std::ostream& dump( std::ostream &o ) {
        o << "{";
        for( auto it = explist.begin(); it != explist.end(); it++ ) {
            if ( it != explist.begin() )
                o << ",";
            it->dump( o );
        }
        o << "}";
        return o;
    }

    ExpressionList( Context &c ) : Parser( c )
    {
        if ( next( Token::BlockOpen ) ) {
                compound = true;
                list< Expression >( std::back_inserter( explist ), Token::Comma );
                eat( Token::BlockClose );
            }
            else
                explist.push_back( Expression( context() ) );
    }
    
    ExpressionList() {};

    ExpressionList( const ExpressionList &elist, bool independent )
        : Parser( elist ), compound( elist.compound )
    {
        for ( const Expression &e : elist.explist )
            explist.push_back( Expression( e, independent ) );
    }
};

inline void RValue::subscript() {
    eat( Token::IndexOpen );
    idx = new Expression( context() );
    eat( Token::IndexClose );
}

inline std::ostream& RValue::dump( std::ostream &o ) {
    if ( ident.valid() ) {
        o << ident.name();
    }
    else {
        o << value.value;
    }
    return o;
}

inline RValue::RValue( const RValue &rval, bool independent )
    : Parser( rval ), ident( rval.ident ), value( rval.value ),
      idx( rval.idx ), mNode( rval.mNode, independent )
{
    if ( !independent )
        return;

    if ( rval.idx ) {
        idx = new Expression( *rval.idx, independent );
    }
}

inline void MacroNode::param() {
    Expression* expr = new Expression( context() );
    params.push_back( expr );
}

inline MacroNode::MacroNode( const MacroNode &mnode, bool independent )
    : MacroNode( mnode )
{
    if ( !independent )
        return;

    params.clear();
    for ( Expression * ex : mnode.params )
        params.push_back( new Expression( *ex, independent ) );
}

struct LValue : Parser {
    Identifier ident;
    Expression idx;

    void subscript() {
        eat( Token::IndexOpen );
        idx = Expression( context() );
        eat( Token::IndexClose );
    }

    std::ostream& dump( std::ostream &o ) {
        o << ident.name();
        return o;
    }

    LValue( Context &c ) : Parser( c ) {
        ident = Identifier( c );
        maybe( &LValue::subscript );
    }
    LValue() {}

    LValue( const LValue &lval, bool independent )
        : Parser( lval ), ident( lval.ident ), idx( lval.idx, independent ) {}
};

struct LValueList: Parser {
    std::vector< LValue > lvlist;
    bool compound;
    
    std::ostream& dump( std::ostream &o ) {
        o << "{";
        for( auto it = lvlist.begin(); it != lvlist.end(); it++ ) {
            if ( it != lvlist.begin() )
                o << ",";
            it->dump( o );
        }
        o << "}";
        return o;
    }

    LValueList( Context &c ) : Parser( c )
    {
        if ( next( Token::BlockOpen ) ) {
                compound = true;
                list< LValue >( std::back_inserter( lvlist ), Token::Comma );
                eat( Token::BlockClose );
            }
            else
                lvlist.push_back( LValue( context() ) );
    }
    
    LValueList() {}

    LValueList( const LValueList & lvallist, bool independent )
        : Parser( lvallist ), compound( lvallist.compound )
    {
        for ( const LValue &lval : lvallist.lvlist )
            lvlist.push_back( LValue( lval, independent ) );
    }
};

struct Assignment : Parser {
    LValue lhs;
    Expression rhs;

    std::ostream& dump( std::ostream &o ) {
        lhs.dump( o );
        o << " = ";
        rhs.dump( o );
        return o;
    }

    Assignment( Context &c ) : Parser( c ) {
        lhs = LValue( c );
        eat( Token::Assignment );
        rhs = Expression( c );
    }

    Assignment( const Assignment &a, bool independent )
        : Parser( a ), lhs( a.lhs, independent ), rhs( a.rhs, independent ) {}
};

struct SyncExpr : Parser {
    bool write;
    bool compound;
    Identifier proc, chan;
    ExpressionList exprlist;
    LValueList lvallist;

    void lvalues() {
        lvallist = LValueList( context() );
    }

    void expressions() {
        exprlist = ExpressionList( context() );
    }

    std::ostream& dump( std::ostream &o ) {
        o << chan.name();
        if ( write ) {
            o << "!";
            if ( exprlist.valid() )
                exprlist.dump( o );
        }
        else {
            o << "?";
            if ( lvallist.valid() )
                lvallist.dump( o );
        }
        return o;
    }

    SyncExpr( Context &c ) : Parser( c ) {
        chan = Identifier( c );
        if ( next( Token::Arrow ) ) {
            proc = chan;
            chan = Identifier( c );
        }
        if ( next( Token::SyncWrite ) )
            write = true;
        else if ( next( Token::SyncRead ) )
            write = false;
        else
            fail( "sync read/write mark: ! or ?" );

        if ( write )
            maybe( &SyncExpr::expressions );
        else
            maybe( &SyncExpr::lvalues );
    }

    SyncExpr() {}

    SyncExpr( const SyncExpr &se, bool independent )
        : Parser( se ), write ( se.write ), compound( se.compound ),
          proc( se.proc ), chan( se.chan ), exprlist( se.exprlist, independent ),
          lvallist( se.lvallist, independent ) {}
};


/*
 * Never use this parser directly, only as an AST node. See Declarations.
 */
struct Declaration : Parser {
    bool is_const, is_compound, is_buffered;
    int width; // bytes
    bool is_array;
    Expression sizeExpr;
    int size;
    std::vector< int > components;
    std::vector< Expression > initialExpr;
    std::vector< int > initial;
    std::string name;
    bool is_input;

    void setSize( int s ) {
        size = s;
        if ( size < 1 )
            fail( ( "Invalid array size: " + wibble::str::fmt( size ) ).c_str(), FailType::Semantic );
    }

    void subscript() {
        eat( Token::IndexOpen );
        sizeExpr = Expression( context() );
        eat( Token::IndexClose );
        is_array = true;
    }

    void initialiser() {
        Token t = eat( Token::Assignment );

        if ( is_array )
            list< Expression >( std::back_inserter( initialExpr ),
                                Token::BlockOpen, Token::Comma, Token::BlockClose );
        else
            initialExpr.push_back( Expression( context() ) );
    }

    Declaration( Context &c ) : Parser( c ), is_buffered( 0 ), size( 1 )
    {
        Token t = eat( Token::Identifier );
        name = t.data;
        is_array = false;
        maybe( &Declaration::subscript );
        maybe( &Declaration::initialiser );
    }

    Declaration( const Declaration &d, bool independent ) : Declaration( d )
    {
       if ( !independent )
           return;

       sizeExpr = Expression( d.sizeExpr, true );
       initialExpr.clear();
       for ( const Expression &e : d.initialExpr )
           initialExpr.push_back( Expression( e, true ) );
    }

    void fold( SymTab* symtab );
};

struct Type : Parser {
    enum TypeSize { Byte = 1, Int = 2 };
    TypeSize size;
    
    Type( Context &c ) : Parser( c ) {
        if ( next( Token::Byte ) ) {
            size = TypeSize::Byte;
        }
        else if ( next ( Token::Int ) ) {
            size = TypeSize::Int;
        }
        else
            fail( "type name (int, byte)" );
    }
};

struct ChannelDeclaration : Parser {
    bool is_buffered, is_const;
    Expression sizeExpr;
    int size, width;
    std::string name;
    bool is_compound;
    std::vector< int > components;
    bool is_input;

    void setSize( int s ) {
        size = s;
        if ( size < 0 )
            fail( ( "Invalid array size: " + wibble::str::fmt( size ) ).c_str(), FailType::Semantic );
        is_buffered = ( size > 0 );
    }

    void subscript() {
        eat( Token::IndexOpen );
        sizeExpr = Expression( context() );
        eat( Token::IndexClose );
    }

    ChannelDeclaration( Context &c ) : Parser( c ), size( 1 )
    {
        Token t = eat( Token::Identifier );
        name = t.data;
        is_buffered = false;
        maybe( &ChannelDeclaration::subscript );
    }

    ChannelDeclaration( const ChannelDeclaration &cd, bool independent )
        : ChannelDeclaration( cd )
    {
        if ( !independent )
            return;

        sizeExpr = Expression( cd.sizeExpr, true );
    }

    void fold( SymTab* symtab );
};

/*
 * This parses a compact (comma-separated) declaration with a common type but
 * multiple identifiers. Always use this production, and never declaration
 * above. On the other hand, always use Declaration in the AST. See also
 * "declarations".
 */
struct Declarations : Parser {

    std::vector< Declaration > decls;
    std::vector< ChannelDeclaration > chandecls;
    std::vector< Type > types;

    bool is_const = false;
    bool is_chan = false;
    bool is_compound = false;
    bool is_input = false;
    int width = 2; // default for untyped channels

    template< typename T >
    void init( T &decllist )
    {
        for ( unsigned i = 0; i < decllist.size(); ++i ) {
            decllist[i].is_const = is_const;
            //decls[i].is_chan = is_chan;
            decllist[i].is_compound = is_compound;
            for( unsigned j = 0; j < types.size(); ++j ) {
                decllist[i].components.push_back( types[i].size );
                width += types[i].size;
            }
            decllist[i].width = width;
            decllist[i].is_input = is_input;
        }
    }

    Declarations( Context &c ) : Parser( c ) {
        if ( next( Token::Const ) )
            is_const = true;
        else if ( next( Token::Input ) )
            is_input = true;

        if ( next( Token::Int ) )
            width = 2;
        else if ( next( Token::Byte ) )
            width = 1;
        else if ( next( Token::Channel ) ) {
            is_chan = true;
            if ( next( Token::BlockOpen ) ) {
                width = 0;
                is_compound = true;
                list< Type >( std::back_inserter( types ), Token::Comma );
                eat( Token::BlockClose );
            }
        } else
            fail( "type name (int, byte, channel)" );
        if ( is_chan ) {
            list< ChannelDeclaration >( std::back_inserter( chandecls ), Token::Comma );
            init( chandecls );
        }
        else {
            list< Declaration >( std::back_inserter( decls ), Token::Comma );
            init( decls );
        }
        semicolon();
    }
};

struct Assertion : Parser {
    Identifier state;
    Expression expr;

    Assertion( Context &c ) : Parser( c ) {
        state = Identifier( c );
        colon();
        expr = Expression( c );
    }

    Assertion( const Assertion &a, bool independent )
        : Parser( a ), state( a.state ), expr( a.expr, independent ) {}
};

struct Transition : Parser {
    Identifier from, to;
    Identifier proc;
    std::vector< Expression > guards;
    std::vector< Assignment > effects;
    SyncExpr syncexpr;
    Token end;

    void guard() {
        eat( Token::Guard );
        list< Expression >( std::back_inserter( guards ), Token::Comma );
        semicolon();
    }

    void effect() {
        eat( Token::Effect );
        list< Assignment >( std::back_inserter( effects ), Token::Comma );
        semicolon();
    }

    void sync() {
        eat( Token::Sync );
        syncexpr = SyncExpr( context() );
        semicolon();
    }

    std::ostream& dump( std::ostream &o ) {
        o << from.name() << " -> " << to.name() << " { ";
        o << "guard ";
        for ( auto grd = guards.begin(); grd != guards.end(); grd++ ) {
            grd->dump( o );
            o << ",";
        }
        o << "; sync ";
        if ( syncexpr.valid() )
            syncexpr.dump( o );
        o << "; effect ";
        for ( auto eff = effects.begin(); eff != effects.end(); eff++) {
            eff->dump( o );
            o << ",";
        }
        o << ";}";
        return o;
    }

    Transition( Context &c ) : Parser( c ) {
        from = Identifier( c );
        eat( Token::Arrow );
        to = Identifier( c );

        eat( Token::BlockOpen );
        maybe( &Transition::guard );
        maybe( &Transition::sync );
        maybe( &Transition::effect );

        end = eat( Token::BlockClose );
    }
    Transition() {};

    Transition( const Transition &t, bool independent )
        : Parser( t ), from( t.from ), to( t.to ),
          syncexpr( t.syncexpr, independent ), end( t.end )
    {
        guards.clear();
        for ( const Expression &e : t.guards )
            guards.push_back( Expression( e, independent ) );

        effects.clear();
        for ( const Assignment &a : t.effects )
            effects.push_back( Assignment( a, independent ) );
    }
};

inline size_t declarations( Parser &p, std::vector< Declaration > &decls,
                          std::vector< ChannelDeclaration > &chandecls )
{
    std::vector< Declarations > declss;
    size_t count = 0;
    p.many< Declarations >( std::back_inserter( declss ) );
    for ( unsigned i = 0; i < declss.size(); ++i ) {
        std::copy( declss[i].decls.begin(), declss[i].decls.end(),
                   std::back_inserter( decls ) );
        count += declss[i].decls.size();

        std::copy( declss[i].chandecls.begin(), declss[i].chandecls.end(),
                   std::back_inserter( chandecls ) );
        count += declss[i].chandecls.size();
    }
    return count;
}

struct Automaton : Parser {
    Identifier name;
    std::vector< Declaration > decls;
    std::vector< ChannelDeclaration > chandecls;
    std::vector< Identifier > states, accepts, commits, inits;
    std::vector< Assertion > asserts;
    std::vector< Transition > trans;

    void accept() {
        eat( Token::Accept );
        list< Identifier >( std::back_inserter( accepts ), Token::Comma );
        semicolon();
    }

    void commit() {
        eat( Token::Commit );
        list< Identifier >( std::back_inserter( commits ), Token::Comma );
        semicolon();
    }

    void state() {
        eat( Token::State );
        list< Identifier >( std::back_inserter( states ), Token::Comma );
        semicolon();
    }

    void init() {
        eat( Token::Init );
        list< Identifier >( std::back_inserter( inits ), Token::Comma );
        semicolon();
    }

    void assertion() {
        if ( next( Token::Assert ) ) {
            list< Assertion >( std::back_inserter( asserts ), Token::Comma );
            semicolon();
        }
    }

    void optionalComma() {
        maybe( Token::Comma );
    }

    std::ostream& dump( std::ostream &o ) {
        o << "process " << name.name() << " {" << std::endl;

        o << "state ";
        for ( Identifier &s : states )
            o << s.name() << ", ";
        o << std::endl;

        o << "accept ";
        for ( Identifier &s : accepts )
            o << s.name() << ", ";
        o << std::endl;

        o << "init ";
        for ( Identifier &s : inits )
            o << s.name() << ", ";
        o << std::endl;

        o << "commit ";
        for ( Identifier &s : commits )
            o << s.name() << ", ";
        o << std::endl;

        o << "trans" << std::endl;
        for ( Transition &t : trans ) {
            t.dump( o );
            o << std::endl;
        }

        o << "}" << std::endl;
        return o;
    }

    void setName( Identifier newname ) {
        name = newname;
        for( Transition &t : trans ) {
            t.proc = name;
        }
    }

    Automaton( Context &c ) : Parser( c ) {
        eat( Token::BlockOpen );

        declarations( *this, decls, chandecls );

        arbitrary( &Automaton::accept,
                   &Automaton::commit,
                   &Automaton::state,
                   &Automaton::init,
                   &Automaton::assertion );

        if ( !states.size() )
            fail( "states" );

        if ( !inits.size() )
            fail( "initial" );

        if ( next( Token::Trans ) ) {
            list< Transition >( std::back_inserter( trans ), &Automaton::optionalComma );
            maybe( &Automaton::semicolon );
        }

        eat( Token::BlockClose );
    }

    Automaton() : Parser() {}

    Automaton( const Automaton &a, bool independent )
        : Parser( a ), name( a.name ), states( a.states ), accepts( a.accepts ),
          commits( a.commits ), inits( a.inits )
    {
        decls.clear();
        for ( const Declaration &d : a.decls )
            decls.push_back( Declaration( d, independent ) );

        chandecls.clear();
        for ( const ChannelDeclaration &d : a.chandecls )
            chandecls.push_back( ChannelDeclaration( d, independent ) );

        asserts.clear();
        for ( const Assertion &as : a.asserts )
            asserts.push_back( Assertion( as, independent ) );

        trans.clear();
        for ( const Transition &t : a.trans )
            trans.push_back( Transition( t, independent ) );
    }

    void fold( SymTab *parent );
};

typedef Automaton Process;
typedef Automaton Property;

template< typename T >
struct Macro : Parser {
    T content;
    Identifier name;
    std::vector< Identifier > params;

    void paramlist() {
        list< Identifier >( std::back_inserter( params ), Token::Comma );
    }

    Macro( Context &c ) : Parser( c )
    {
        name = Identifier( c );
        eat( Token::ParenOpen );
        maybe( &Macro::paramlist );
        eat( Token::ParenClose );

        eat( Token::BlockOpen );
        content = T( c );
        eat( Token::BlockClose );
    }
};

struct LTL : Parser {
    Identifier name;
    Token property;

    LTL( Context &c ) : Parser( c )
    {
        name = Identifier( c );
        property = eat( Token::String );
    }
};

struct System : Parser {
    std::vector< Declaration > decls;
    std::vector< ChannelDeclaration > chandecls;
    std::vector< Process > processes;
    std::vector< Property > properties;
    std::vector< LTL > ltlprops;
    std::vector< Macro< Expression > > exprs;
    std::vector< Macro< Automaton > > templates;
    std::vector< MacroNode > procInstances;
    Identifier property;
    bool synchronous;

    void _property() {
        eat( Token::Property );
        property = Identifier( context() );
    }

    void process() {
        eat( Token::Process );
        Identifier procname( context() );
        processes.push_back( Process( context() ) );
        processes.back().setName( procname );
    }

    void propDef() {
        eat( Token::Property );
        either( &System::procProperty, &System::LTLProperty );
    }

    void procProperty() {
        eat( Token::Process );
        Identifier procname( context() );
        properties.push_back( Property( context() ) );
        properties.back().setName( procname );
    }

    void LTLProperty() {
        eat( Token::LTL );
        ltlprops.push_back( LTL( context() ) );
    }

    void declaration() {
        if ( !declarations( *this, decls, chandecls ) )
            fail( "no declarations" );
    }

    void exprMacro() {
        eat( Token::Expression );
        exprs.push_back( Macro< Expression >( context() ) );
    }

    void templateMacro() {
        eat( Token::Process );
        templates.push_back( Macro< Automaton >( context() ) );
        templates.back().content.setName( templates.back().name );
    }

    void processInstance() {
        eat( Token::Process );
        procInstances.push_back( MacroNode( context() ) );
        semicolon();
    }

    System( Context &c ) : Parser( c )
    {
        synchronous = false;

        while ( maybe( &System::declaration,
                           &System::process,
                           &System::propDef,
                           &System::exprMacro,
                           &System::templateMacro,
                           &System::processInstance
                         ) );

        eat( Token::System );

        if ( next( Token::Async ) ); // nothing
        else if ( next( Token::Sync ) )
            synchronous = true;
        else
            fail( "sync or async" );

        maybe( &System::_property );
        semicolon();
        context().clearErrors();
    }

    void fold();
};

}
}
}

#endif
