#include "Dispatcher.h"
#include "TcpSession.h"

thread_local std::shared_ptr<TcpSession> currentSessionPtr;

std::shared_ptr<TcpSession> getCurrentTcpSession()
{
	return currentSessionPtr;
}

void setCurrentTcpSession(std::shared_ptr<TcpSession> pSession)
{
	currentSessionPtr = pSession;
}

std::string getBoostExceptionThrowLocation(const boost::exception& ex)
{
	std::ostringstream os;
	auto file = boost::get_error_info<boost::throw_file>(ex);
	auto line = boost::get_error_info<boost::throw_line>(ex);	//char const * const * fn = boost::get_error_info<boost::throw_function>(ex);
	if (file && line)
	{
		std::string tmp = *file;
		os << tmp.substr(tmp.rfind('\\') + 1) << ':' << *line;
	}
	return os.str();
}

void Dispatcher::dispatch(const msgpack::object &objMsg, msgpack::zone&& zone, std::shared_ptr<TcpSession> session)
{
	BOOST_LOG_FUNCTION();
	FuncTracer tracer;

	setCurrentTcpSession(session);
	MsgRequest<std::string, msgpack::object> req;
	MSGPACK_CONVERT(objMsg, req);

	try
	{
		auto found = m_handlerMap.find(req.method);
		if (found != m_handlerMap.end())
		{
			BOOST_LOG_SEV(_logger, debug) << logging::add_value("RemoteAddress", session->_peerAddr) << "rpc:" << req.method;
			session->asyncWrite(found->second(req.msgid, req.param));
		}
		else
			BOOST_THROW_EXCEPTION(FunctionNotFoundException() <<
				err_no(error_no_function) <<
				err_str(std::string("error_no_function:") + req.method));
	}
	catch (boost::exception& ex)
	{
		auto no = boost::get_error_info<err_no>(ex);
		auto str = boost::get_error_info<err_str>(ex);
		session->errorRespond(req.msgid, no ? *no : 0, str ? *str : "");

		auto loc = getBoostExceptionThrowLocation(ex);
		BOOST_LOG_NAMED_SCOPE("catch");
		BOOST_LOG_SEV(_logger, error) << logging::add_value("RemoteAddress", session->_peerAddr) <<
			(str ? *str : "") << "		" << loc;
	}
	catch (std::exception& ex)
	{
		session->errorRespond(req.msgid, 0, ex.what());

		BOOST_LOG_NAMED_SCOPE("catch");
		BOOST_LOG_SEV(_logger, error) << logging::add_value("RemoteAddress", session->_peerAddr) << ex.what();
	}
	catch (...)
	{
		session->errorRespond(req.msgid, 0, "unknown exception");

		BOOST_LOG_NAMED_SCOPE("catch");
		BOOST_LOG_SEV(_logger, error) << logging::add_value("RemoteAddress", session->_peerAddr) << "unknown exception";
	}
}
