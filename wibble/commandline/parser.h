#ifndef WIBBLE_COMMANDLINE_PARSER_H
#define WIBBLE_COMMANDLINE_PARSER_H

#include <wibble/commandline/engine.h>
#include <iosfwd>

namespace wibble {
namespace commandline {

/**
 * Generic parser for commandline arguments.
 */
class Parser : public Engine
{
	ArgList m_args;

protected:
	MemoryManager m_manager;

public:
	Parser(const std::string& usage = std::string(),
		   const std::string& description = std::string(),
		   const std::string& longDescription = std::string())
		: Engine(&m_manager, std::string(), usage, description, longDescription) {}

	/**
	 * Parse the commandline
	 *
	 * @returns true if it also took care of performing the action requested by
	 *   the user, or false if the caller should do it instead.
	 */
	bool parse(int argc, const char* argv[])
	{
		m_args.clear();
		for (int i = 1; i < argc; i++)
			m_args.push_back(argv[i]);
		parseList(m_args);
		return false;
	}

	bool hasNext() const { return !m_args.empty(); }

	std::string next()
	{
		if (m_args.empty())
			return std::string();
		std::string res(*m_args.begin());
		m_args.erase(m_args.begin());
		return res;
	}
};

/**
 * Parser for commandline arguments, with builting help functions.
 */
class StandardParser : public Parser
{
protected:
	std::string m_appname;
	std::string m_version;

public:
	StandardParser(const std::string& appname, const std::string& version) :
		m_appname(appname), m_version(version)
	{
		helpGroup = createGroup("Help options");
		help = helpGroup->create<BoolOption>("help", 'h', "help", "",
				"print commandline help and exit");
		help->addAlias('?');
		this->version = helpGroup->create<BoolOption>("version", 0, "version", "",
				"print the program version and exit");
	}

	void outputHelp(std::ostream& out);

	bool parse(int argc, const char* argv[]);

	OptionGroup* helpGroup;
	BoolOption* help;
	BoolOption* version;
};

/**
 * Parser for commandline arguments, with builting help functions and manpage
 * generation.
 */
class StandardParserWithManpage : public StandardParser
{
protected:
	int m_section;
	std::string m_author;

public:
	StandardParserWithManpage(
			const std::string& appname,
			const std::string& version,
			int section,
			const std::string& author) :
		StandardParser(appname, version),
		m_section(section), m_author(author)
	{
		manpage = helpGroup->create<StringOption>("manpage", 0, "manpage", "[hooks]",
				"output the " + m_appname + " manpage and exit");
	}

	bool parse(int argc, const char* argv[]);

	StringOption* manpage;
};

/**
 * Parser for commandline arguments, with builting help functions and manpage
 * generation, and requiring a mandatory command.
 */
class StandardParserWithMandatoryCommand : public StandardParserWithManpage
{
public:
	StandardParserWithMandatoryCommand(
			const std::string& appname,
			const std::string& version,
			int section,
			const std::string& author) :
		StandardParserWithManpage(appname, version, section, author)
	{
		helpCommand = createEngine("help", "[command]", "print help information",
				"With no arguments, print a summary of available commands.  "
				"If given a command name as argument, print detailed informations "
				"about that command.");
	}

	bool parse(int argc, const char* argv[]);

	Engine* helpCommand;
};

}
}

// vim:set ts=4 sw=4:
#endif
