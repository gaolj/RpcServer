#include "TcpSession.h"
#include "Exception.h"
#include "SessionManager.h"		// SessionManager
#include <boost/asio.hpp>
#include <boost/format.hpp>		// boost::format
#include "mstcpip.h"			// struct tcp_keepalive

using std::string;
using boost::asio::ip::tcp;

TcpSession::TcpSession(tcp::socket socket, string peer) :
	_socket(std::move(socket)),
	_peerAddr(std::move(peer))
{
	_peerAddrAttr = _logger.add_attribute("RemoteAddress",
		attrs::constant<string>(_peerAddr)).first;
}

TcpSession::~TcpSession()
{
}

void TcpSession::start()
{
	// set keepalive
	DWORD dwBytesRet = 0;
	SOCKET sock = _socket.native_handle();
	struct tcp_keepalive alive;
	alive.onoff = TRUE;
	alive.keepalivetime = 5 * 1000;
	alive.keepaliveinterval = 3000;

	if (WSAIoctl(sock, SIO_KEEPALIVE_VALS, &alive, sizeof(alive), NULL, 0, &dwBytesRet, NULL, NULL) == SOCKET_ERROR)
		LOG_ERROR(_logger) << "WSAIotcl(SIO_KEEPALIVE_VALS) failed:" << WSAGetLastError();

	readHead();
}

void TcpSession::stop()
{
	boost::system::error_code err;
	_socket.close(err);
	_logger.remove_attribute(_peerAddrAttr);
}

void TcpSession::setDispatcher(std::shared_ptr<Dispatcher> disp)
{
	_dispatcher = disp;
}

void TcpSession::handleNetError(const boost::system::error_code& ec)
{
	LOG_NTFY(_logger) << logging::add_value("ErrorCode", ec.value()) << ec.message();
	boost::system::error_code err;
	_socket.close(err);

	std::unique_lock<std::mutex> lck(_reqMutex);
	for (auto& mapReq : _reqPromiseMap)
	{
		try
		{
			if (!mapReq.second._future.is_ready())	// 如果_future is_ready，则_prom.set_exception会抛异常
			{
				auto pExcept = boost::copy_exception(
					NetException() <<
					err_no(ec.value()) <<
					err_str(ec.message()));

				mapReq.second._prom.set_exception(pExcept);			// Microsoft C++ 异常: boost::exception_detail::clone_impl<NetException>，位于内存位置 0x0125E2B0 处。
				if (mapReq.second._callback)
					mapReq.second._callback(mapReq.second._future);	// _callback如果_reqMutex.lock，则会异常
			}
		}
		catch (const boost::exception& ex)
		{
			auto no = boost::get_error_info<err_no>(ex);
			auto str = boost::get_error_info<err_str>(ex);
			auto loc = getBoostExceptionThrowLocation(ex);
			LOG_ERROR(_logger) << (str ? *str : "") << loc;
		}
	}
	_reqPromiseMap.clear();
	SessionManager::instance()->stop(shared_from_this());
}

void TcpSession::asyncWrite(std::shared_ptr<msgpack::sbuffer> msg)
{
	auto len = std::make_shared<uint32_t>(htonl(msg->size()));

	std::vector<boost::asio::const_buffer> bufs;
	bufs.push_back(boost::asio::buffer(len.get(), sizeof(uint32_t)));
	bufs.push_back(boost::asio::buffer(msg->data(), msg->size()));

	auto self = shared_from_this();
	boost::asio::async_write(_socket, bufs,
		[this, self, len, msg](const boost::system::error_code& ec, size_t bytesWrite)
		{
			if (ec)
			{
				BOOST_LOG_NAMED_SCOPE("handle Write");
				handleNetError(ec);
			}
		});
}

void TcpSession::errorNotify(const string& msg)
{
	MsgNotify<string, string> notify("errorNotify", msg);
	auto sbuf = std::make_shared<msgpack::sbuffer>();
	msgpack::pack(*sbuf, notify);

	auto len = std::make_shared<uint32_t>(htonl(sbuf->size()));

	std::vector<boost::asio::const_buffer> bufs;
	bufs.push_back(boost::asio::buffer(len.get(), sizeof(uint32_t)));
	bufs.push_back(boost::asio::buffer(sbuf->data(), sbuf->size()));

	auto self = shared_from_this();
	boost::asio::async_write(_socket, bufs,
		[this, self, len, sbuf](const boost::system::error_code& ec, size_t bytesWrite)
		{
			boost::system::error_code err;
			_socket.close(err);
		});
}

void TcpSession::errorRespond(int msgid, int errcode, const string & what)
{
	MsgResponse<std::tuple<int, string>, bool> rsp(
		std::make_tuple(errcode, what),
		true,
		msgid);

	auto bufPtr = std::make_shared<msgpack::sbuffer>();
	msgpack::pack(*bufPtr, rsp);
	asyncWrite(bufPtr);
}

void TcpSession::readHead()
{
	auto shared = shared_from_this();
	boost::asio::async_read(_socket,
		boost::asio::buffer(&_head, sizeof(_head)),
		[this, shared](const boost::system::error_code& ec, std::size_t bytesRead)
		{
			if (ec)
			{
				BOOST_LOG_NAMED_SCOPE("handle readHead");
				handleNetError(ec);
			}
			else
				readBody();
		});
}

void TcpSession::readBody()
{
	uint32_t length = ntohl(_head);
	if (length == 0)	// ping package
	{
		readHead();
		return;
	}
	if (length > MAX_MSG_LENGTH)
	{
		string msg = (boost::format("MsgTooLong: %d bytes") % length).str();
		errorNotify(msg);
		LOG_ERROR(_logger) << msg;
		return;
	}

	auto shared = shared_from_this();
	boost::asio::async_read(_socket,
		boost::asio::buffer(_buf, length),
		[this, shared](const boost::system::error_code& ec, std::size_t bytesRead)
		{
			BOOST_LOG_NAMED_SCOPE("handle readBody");
			if (ec)
				handleNetError(ec);
			else
			{
				msgpack::unpacked upk;
				try
				{
					upk = msgpack::unpack(_buf, bytesRead);	// 解好包，才能继续async_read _buf

					readHead();			// 异步接收下一条消息
					processMsg(upk);	// 同步处理当前的消息
				}
				catch (const msgpack::unpack_error& ex)
				{
					errorNotify(ex.what());
					LOG_ERROR(_logger) << "unpack_error:" << ex.what() << '(' << bytesRead << ')';
				}
				catch (const boost::exception& ex)
				{
					auto str = boost::get_error_info<err_str>(ex);
					errorNotify(str ? *str : "");
					auto loc = getBoostExceptionThrowLocation(ex);
					LOG_ERROR(_logger) << (str ? *str : "") << loc;
				}
				catch (const std::exception& ex)
				{
					errorNotify(ex.what());
					LOG_ERROR(_logger) << ex.what();
				}
				catch (...)
				{
					errorNotify("unknown server exception");
					LOG_ERROR(_logger) << "unknown server exception";
				}
			}
		});
}

void TcpSession::processMsg(msgpack::unpacked upk)
{
	using msgpack::object;

	object objMsg(upk.get());
	MsgRpc rpc;
	MSGPACK_CONVERT(objMsg, rpc);


	switch (rpc.type)
	{
	case MSG_TYPE_REQUEST:
	{
		_dispatcher->dispatch(objMsg, std::move(*(upk.zone())), shared_from_this());
	}
	break;

	case MSG_TYPE_RESPONSE:
	{
		MsgResponse<object, object> rsp;
		MSGPACK_CONVERT(objMsg, rsp);

		std::unique_lock<std::mutex> lck(_reqMutex);
		auto found = _reqPromiseMap.find(rsp.msgid);
		if (found == _reqPromiseMap.end())
		{
			BOOST_THROW_EXCEPTION(RequestNotFoundException() <<
				err_no(error_RequestNotFound) <<
				err_str((boost::format("RequestNotFound, msgid = %d") % rsp.msgid).str()));
		}
		CallPromise callProm(std::move(found->second));
		_reqPromiseMap.erase(found);
		lck.unlock();

		if (rsp.error.type != msgpack::type::BOOLEAN)
			callProm._prom.set_exception(boost::copy_exception(ReturnErrorException() <<
				err_no(rsp.error.type) <<
				err_str("MsgResponse::error type is not bool")));
		else
		{
			bool isError;
			MSGPACK_CONVERT(rsp.error, isError);
			if (isError)
			{
				std::tuple<int, string> tup;
				MSGPACK_CONVERT(rsp.result, tup);
				callProm._prom.set_exception(boost::copy_exception(ReturnErrorException() <<
					err_no(std::get<0>(tup)) <<
					err_str(std::get<1>(tup))));
			}
			else
				callProm._prom.set_value(std::make_pair(rsp.result, std::move(*(upk.zone()))));
		}

		if (callProm._callback)
			callProm._callback(callProm._future);
	}
	break;

	case MSG_TYPE_NOTIFY:
	{
		MsgNotify<object, string> notify;
		MSGPACK_CONVERT(objMsg, notify);
		LOG_ERROR(_logger) << "Server error notify: " << notify.param;
	}
	break;

	default:
		BOOST_THROW_EXCEPTION(MessageException() << err_no(0) << err_str("Msg type not found"));
	}
}
