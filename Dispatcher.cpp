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

std::shared_ptr<msgpack::sbuffer> Dispatcher::processCall(uint32_t msgid, msgpack::object method, msgpack::object args)
{
	std::string methodName;
	method.convert(&methodName);

	auto found = m_handlerMap.find(methodName);
	if (found == m_handlerMap.end())
	{
		BOOST_THROW_EXCEPTION(
			FunctionNotFoundException() <<
			err_no(error_no_function) <<
			err_str(std::string("error_no_function: ") + methodName));
	}
	else
	{
		Procedure proc = found->second;
		return proc(msgid, args);
	}
}

void Dispatcher::dispatch(const msgpack::object &objMsg, msgpack::zone&& zone, std::shared_ptr<TcpSession> session)
{
	BOOST_LOG_FUNCTION();
	FuncTracer tracer;

	setCurrentTcpSession(session);
	MsgRequest<msgpack::object, msgpack::object> req;
	objMsg.convert(&req);	//+++
	try
	{
#ifndef _DEBUG
		if (req.method.as<std::string>() == "shutdown_send")
		{
			session->getSocket().shutdown(boost::asio::socket_base::shutdown_send);
			return;
		}
		else if (req.method.as<std::string>() == "shutdown_receive")
		{
			session->getSocket().shutdown(boost::asio::socket_base::shutdown_receive);
			return;
		}
		else if (req.method.as<std::string>() == "shutdown_both")
		{
			session->getSocket().shutdown(boost::asio::socket_base::shutdown_both);
			return;
		}
#endif
		std::shared_ptr<msgpack::sbuffer> bufPtr = processCall(req.msgid, req.method, req.param);
		session->asyncWrite(bufPtr);
		return;
	}
	catch (boost::exception& ex)
	{
		auto no = boost::get_error_info<err_no>(ex);
		auto str = boost::get_error_info<err_str>(ex);

		MsgResponse<std::tuple<int, std::string>, bool> rsp(
			std::make_tuple(no ? *no : 0, str ? *str : ""),
			true,
			req.msgid);

		auto bufPtr = std::make_shared<msgpack::sbuffer>();
		msgpack::pack(*bufPtr, rsp);
		session->asyncWrite(bufPtr);

		char const * const * f = boost::get_error_info<boost::throw_file>(ex);
		int const * l = boost::get_error_info<boost::throw_line>(ex);		//char const * const * fn = boost::get_error_info<boost::throw_function>(ex);
		std::ostringstream tmp;
		if (f && l)
		{
			std::string file = *f;
			tmp << file.substr(file.rfind('\\') + 1) << ':' << *l;
		}

		BOOST_LOG_NAMED_SCOPE("catch");
		BOOST_LOG_SEV(_logger, error) << logging::add_value("RemoteAddress", session->_peerAddr) <<
			(str ? *str : "") << "		" << tmp.str();
	}
	catch (std::exception& ex)
	{
		MsgResponse<std::tuple<int, std::string>, bool> rsp(
			std::make_tuple(0, ex.what()),
			true,
			req.msgid);

		auto bufPtr = std::make_shared<msgpack::sbuffer>();
		msgpack::pack(*bufPtr, rsp);
		session->asyncWrite(bufPtr);

		BOOST_LOG_NAMED_SCOPE("catch");
		BOOST_LOG_SEV(_logger, error) << logging::add_value("RemoteAddress", session->_peerAddr) << ex.what();
	}
	catch (...)
	{
		BOOST_LOG_NAMED_SCOPE("catch");
		BOOST_LOG_SEV(_logger, error) << logging::add_value("RemoteAddress", session->_peerAddr) << "unknown exception";
	}
}
