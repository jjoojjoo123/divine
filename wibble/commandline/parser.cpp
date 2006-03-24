#include <wibble/config.h>
#include <wibble/commandline/parser.h>
#include <wibble/commandline/doc.h>
#include <iostream>

using namespace std;

namespace wibble {
namespace commandline {

void StandardParser::outputHelp(std::ostream& out)
{
	commandline::Help help(m_appname, m_version);
	commandline::Engine* e = foundCommand();

	if (e)
		// Help on a specific command
		help.outputHelp(out, *e);
	else
		// General help
		help.outputHelp(out, *this);
}

bool StandardParser::parse(int argc, const char* argv[])
{
	if (Parser::parse(argc, argv))
		return true;

	if (help->boolValue())
	{
		// Provide help as requested
		outputHelp(cout);
		return true;
	}
	if (version->boolValue())
	{
		// Print the program version
		commandline::Help help(m_appname, m_version);
		help.outputVersion(cout);
		return true;
	}
	return false;
}

bool StandardParserWithManpage::parse(int argc, const char* argv[])
{
	if (StandardParser::parse(argc, argv))
		return true;
	if (manpage->boolValue())
	{
		// Output the manpage
		commandline::Manpage man(m_appname, m_version, m_section, m_author);
		string hooks(manpage->stringValue());
		if (!hooks.empty())
			man.readHooks(hooks);
		man.output(cout, *this);
		return true;
	}
	return false;
}

bool StandardParserWithMandatoryCommand::parse(int argc, const char* argv[])
{
	if (StandardParserWithManpage::parse(argc, argv))
		return true;

	if (!foundCommand())
	{
		commandline::Help help(m_appname, m_version);
		help.outputHelp(cout, *this);
		return true;
	}
	if (foundCommand() == helpCommand)
	{
		commandline::Help help(m_appname, m_version);
		if (hasNext())
		{
			// Help on a specific command
			string command = next();
			if (Engine* e = this->command(command))
				help.outputHelp(cout, *e);
			else
				throw exception::BadOption("unknown command " + command + "; run '" + argv[0] + " help' "
						"for a list of all the available commands");
		} else {
			// General help
			help.outputHelp(cout, *this);
		}
		return true;
	}
	return false;
}


}
}


#ifdef WIBBLE_COMPILE_TESTSUITE
#include <wibble/tests.h>

namespace wibble {
namespace tut {

struct commandline_parser_shar {
};
TESTGRP( commandline_parser );

using namespace wibble::commandline;

template<> template<>
void to::test<1>()
{
}

}
}

#endif

// vim:set ts=4 sw=4:
