an light c++ rpc framework, use boost asio for network communication , and msgpack for serialization

sample

// server side
void setUuid(std::string uuid)
{
	auto session = getCurrentTcpSession();
	if (session)
		session->_uuid = uuid;
}

std::string getUuid()
{
	auto session = getCurrentTcpSession();
	if (session)
		return session->_uuid;
	else
		return "";
}

main()
{
	TcpServer server(8070);
	std::shared_ptr<Dispatcher> dispatcher = std::make_shared<Dispatcher>();
	dispatcher->add_handler("echo", [](std::string str) { return str; });
	dispatcher->add_handler("add", [](int a, int b) { return a + b; });
	dispatcher->add_handler("setUuid", &setUuid);
	dispatcher->add_handler("getUuid", &getUuid);
	server.setDispatcher(dispatcher);

	server.run();
}


// client side
main()
{
	boost::asio::io_service client_io;
	boost::asio::io_service::work work(client_io);
	boost::thread clinet_thread([&client_io]() { client_io.run(); });
	try
	{
		auto ses = std::make_shared<msgpack::rpc::TcpSession>(client_io, nullptr);
		ses->asyncConnect(peer).get();
		auto fut = ses->call("add", i, i);
		BOOST_CHECK_EQUAL(fut.get().first.as<int>(), i + i);
	}
	catch (const boost::exception& e){std::cerr << diagnostic_information(e) << std::endl;}
	catch (const std::exception& e){std::cerr << e.what() << std::endl;}
	catch (...){std::cerr << "unknow exception" << std::endl;}

	client_io.stop();
	clinet_thread.join();
}