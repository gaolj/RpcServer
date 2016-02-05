#pragma once
//#include <boost/log/sources/global_logger_storage.hpp>

//#include <boost/log/attributes.hpp>						
#include <boost/log/attributes/named_scope.hpp>				// BOOST_LOG_NAMED_SCOPE
//#include <boost/log/attributes/timer.hpp>
#include <boost/log/attributes/constant.hpp>				// attrs::constant

#include <boost/log/sources/record_ostream.hpp>				// BOOST_LOG_SEV
#include <boost/log/sources/severity_logger.hpp>			// severity_logger
#include <boost/log/sources/severity_channel_logger.hpp>	// severity_channel_logger

#include <boost/log/utility/manipulators/add_value.hpp>

#include <boost/log/support/exception.hpp>					// logging::current_scope

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace attrs = boost::log::attributes;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;

enum SeverityLevel
{
	trace,
	debug,
	info,
	warn,
	error,
	fatal
};

void initLogger();

class FuncTracer
{
public:
	src::severity_logger<SeverityLevel> _logger;
	FuncTracer()
	{
		BOOST_LOG_SEV(_logger, trace) << "BGN";
	}

	~FuncTracer(void)
	{
		BOOST_LOG_SEV(_logger, trace) << "END";
	}
};