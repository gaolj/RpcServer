#pragma once
#include "Logger.h"
#include <thread>						// thread
#include <boost/asio/io_service.hpp>	// io_service
#include <boost/asio/ip/tcp.hpp>		// tcp::socket, tcp::acceptor
#include "Dispatcher.h"


class TcpServer
{
public:
	TcpServer(short port);
	virtual ~TcpServer();

	void run();
	void stop();

	void setDispatcher(std::shared_ptr<Dispatcher> disp);
private:
	void doAccept();

	boost::asio::io_service _ios;
	boost::asio::ip::tcp::socket _socket;
	boost::asio::ip::tcp::acceptor _acceptor;
	std::vector<std::thread> _threads;

	std::shared_ptr<Dispatcher> _dispatcher;

	src::severity_channel_logger<SeverityLevel> _logger{ keywords::channel = "net" };
	logging::attribute_set::iterator _net_remote_addr;
};

