#pragma once
#include "Logger.h"
#include "BufferManager.h"				// ArrayBuffer, BufferManager
#include <boost/asio/ip/tcp.hpp>		// tcp::socket, tcp::acceptor
#include <boost/thread/future.hpp>		// boost::shared_future, boost::promise
#include <msgpack.hpp>					// msgpack::object
#include "Protocol.h"					// MsgRequest
#include <mutex>						// std::mutex, std::unique_lock
#include "Dispatcher.h"

#define MSG_BUF_LENGTH	MAX_MSG_LENGTH

typedef std::pair<msgpack::object, msgpack::zone> ObjectZone;
typedef std::function<void(boost::shared_future<ObjectZone>)> RpcCallback;
struct CallPromise
{
	boost::promise<ObjectZone> _prom;
	boost::shared_future<ObjectZone> _future;
	RpcCallback _callback;

	CallPromise()
	{
		_future = _prom.get_future().share();
	}

	CallPromise(RpcCallback&& callback) : _callback(std::move(callback))
	{
		_future = _prom.get_future().share();
	}

	CallPromise(CallPromise&& call) :
		_prom(std::move(call._prom)),
		_future(call._future),
		_callback(std::move(call._callback))
	{
	}
};

class TcpSession : public std::enable_shared_from_this<TcpSession>
{
public:
	std::string _uuid;
	TcpSession(boost::asio::ip::tcp::socket socket, std::string peer);
	virtual ~TcpSession();
	void start();
	void stop();

	void readHead();
	void readBody();
	void asyncWrite(std::shared_ptr<msgpack::sbuffer> msg);

	boost::asio::ip::tcp::socket& getSocket();
	void setDispatcher(std::shared_ptr<Dispatcher> disp);

	void errorNotify(const std::string& msg);	// 没有request msgid
	void errorRespond(int msgid, int errcode, const std::string& what);	// 有request msgid

	// Async call
	template<typename... TArgs>
	boost::shared_future<ObjectZone> call(const std::string& method, TArgs... args);

	template<typename... TArgs>
	void call(RpcCallback&& callback, const std::string& method, TArgs... args);
private:
	void handleNetError(const boost::system::error_code& ec);

	void processMsg(msgpack::unpacked upk);

	boost::asio::ip::tcp::socket _socket;
	uint32_t _head;
	char _buf[MSG_BUF_LENGTH];

	std::mutex _reqMutex;
	std::atomic<uint32_t> _reqNextMsgid{ 1 };
	std::unordered_map<uint32_t, CallPromise> _reqPromiseMap;
	std::shared_ptr<Dispatcher> _dispatcher;

public:
	std::string _peerAddr;
	logging::attribute_set::iterator _peerAddrAttr;
	src::severity_channel_logger<SeverityLevel> _logger{ keywords::channel = "net" };
};

typedef std::shared_ptr<TcpSession> SessionPtr;

inline boost::asio::ip::tcp::socket& TcpSession::getSocket()
{
	return _socket;
}

template<typename... TArgs>
inline boost::shared_future<ObjectZone> TcpSession::call(const std::string& method, TArgs... args)
{
	LOG_DEBUG(_logger) << "->" << method;
	auto msgreq = MsgRequest<std::string, std::tuple<TArgs...>>(method, std::tuple<TArgs...>(args...), _reqNextMsgid++);

	auto sbuf = std::make_shared<msgpack::sbuffer>();
	msgpack::pack(*sbuf, msgreq);

	std::unique_lock<std::mutex> lck(_reqMutex);
	auto ret = _reqPromiseMap.emplace(msgreq.msgid, CallPromise());
	// ***千万注意不要在这里unlock, 因为下面还要用到_reqPromiseMap的迭代器ret

	asyncWrite(sbuf);
	return ret.first->second._future;
}

template<typename... TArgs>
inline void TcpSession::call(RpcCallback&& callback, const std::string& method, TArgs... args)
{
	LOG_DEBUG(_logger) << "->" << method;
	auto msgreq = MsgRequest<std::string, std::tuple<TArgs...>>(method, std::tuple<TArgs...>(args...), _reqNextMsgid++);

	auto sbuf = std::make_shared<msgpack::sbuffer>();
	msgpack::pack(*sbuf, msgreq);

	std::unique_lock<std::mutex> lck(_reqMutex);
	_reqPromiseMap.emplace(msgreq.msgid, CallPromise(std::move(callback)));
	lck.unlock();

	asyncWrite(sbuf);
}

