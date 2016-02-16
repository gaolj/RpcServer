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
	auto line = boost::get_error_info<boost::throw_line>(ex);
	//auto func = boost::get_error_info<boost::throw_function>(ex);
	if (file && line)
	{
		std::string tmp = *file;
		os << "		throw@" << tmp.substr(tmp.rfind('\\') + 1) << ':' << *line;
	}
	return os.str();
}

void Dispatcher::dispatch(const msgpack::object &objMsg, msgpack::zone&& zone, std::shared_ptr<TcpSession> session)
{
	BOOST_LOG_FUNCTION();
	FuncTracer tracer;

	BOOST_LOG_SCOPED_THREAD_ATTR("Uptime", attrs::timer());
	setCurrentTcpSession(session);
	MsgRequest<std::string, msgpack::object> req;
	MSGPACK_CONVERT(objMsg, req);

	try
	{
		auto found = m_handlerMap.find(req.method);
		if (found != m_handlerMap.end())
		{
			session->asyncWrite(found->second(req.msgid, req.param));
			LOG_DEBUG(_logger) << logging::add_value("RemoteAddress", session->_peerAddr) << "<-" << req.method << ":OK";
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
		LOG_ERROR(_logger) << logging::add_value("RemoteAddress", session->_peerAddr) << (str ? *str : "") << loc;
	}
	catch (std::exception& ex)
	{
		session->errorRespond(req.msgid, 0, ex.what());
		LOG_ERROR(_logger) << logging::add_value("RemoteAddress", session->_peerAddr) << ex.what();
	}
	catch (...)
	{
		session->errorRespond(req.msgid, 0, "unknown exception");
		LOG_ERROR(_logger) << logging::add_value("RemoteAddress", session->_peerAddr) << "unknown exception";
	}
}
