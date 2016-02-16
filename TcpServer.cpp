#include "TcpServer.h"
#include "TcpSession.h"
#include "SessionManager.h"


using boost::asio::ip::tcp;

TcpServer::TcpServer(short port) :
	_ios(),
	_socket(_ios),
	_acceptor(_ios, tcp::endpoint(tcp::v4(), port))
{
	doAccept();
}

TcpServer::~TcpServer()
{
}

void TcpServer::run()
{
	for (std::size_t i = 0; i < 3; ++i)
		_threads.push_back(std::thread([this]() {_ios.run(); }));
}

void TcpServer::stop()
{
	_acceptor.close();
	// all connected socket close

	for (auto& thread : _threads)
		thread.join();
}

void TcpServer::setDispatcher(std::shared_ptr<Dispatcher> disp)
{
	_dispatcher = disp;
}

void TcpServer::doAccept()
{
	_acceptor.async_accept(_socket,
		[this](boost::system::error_code ec)			// ???为什么lambda不用const boost::system::error_code& ec，而bind是用的
		{			
			BOOST_LOG_NAMED_SCOPE("handleAccept");
			FuncTracer tracer;

			if (ec)
			{
				LOG_ERROR(_logger) << "async_accept:" << ec.message();
			}
			else
			{
				boost::system::error_code ec;
				tcp::endpoint peer = _socket.remote_endpoint(ec);
				auto addr = peer.address().to_string();
				if (!ec)
					LOG_DEBUG(_logger) << logging::add_value("RemoteAddress", addr) << "Accepted";
				else
					LOG_ERROR(_logger) << "remote_endpoint:" << ec.message();

				auto pSession = std::make_shared<TcpSession>(std::move(_socket), std::move(addr));
				pSession->setDispatcher(_dispatcher);
				SessionManager::instance()->start(pSession);
			}

			doAccept();
		});
}
