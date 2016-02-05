#define BOOST_TEST_MODULE ServerTest
#include <boost/test/unit_test.hpp> 
#include <boost/log/sources/global_logger_storage.hpp>

#include <iostream>
#include "TcpServer.h"
#include "TcpSession.h"

BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(glogger, src::severity_logger_mt<SeverityLevel>)

int twowayAdd(int a, int b)
{
	auto session = getCurrentTcpSession();
	BOOST_LOG_SEV(session->_logger, debug) << "<--" << a << " + " << b;

	if (session)
	{
		auto on_result = [session, a, b](boost::shared_future<ObjectZone> fut)
		{
			try
			{
				int i = fut.get().first.as<int>();

				//BOOST_CHECK_EQUAL(i, a + b);	// ���߳���BOOST_CHECK_EQUAL��ʵ�ֿ��ܻ���쳣
				if (i != a + b)
					BOOST_LOG_SEV(session->_logger, fatal) << "-->" << a << " + " << b << " != " << i;
				else
					BOOST_LOG_SEV(session->_logger, debug) << "-->" << a << " + " << b << " == " << i;
			}
			catch (const boost::exception& e)
			{
				BOOST_LOG_SEV(session->_logger, error) << diagnostic_information(e);
			}
			catch (const std::exception& e)
			{
				BOOST_LOG_SEV(session->_logger, error) << e.what();
			}
			catch (...)
			{
				BOOST_LOG_SEV(session->_logger, error) << "twowayAddδ֪�쳣";
			}
		};
		session->call(on_result, "add", a, b);
		//session->call(on_result, "shutdown_send");
	}
	return a + b;
}

void setUuid(std::string uuid)
{
	auto session = getCurrentTcpSession();
	if (session)
	{
		session->_uuid = uuid;
	}
}

std::string getUuid()
{
	auto session = getCurrentTcpSession();
	if (session)
		return session->_uuid;
	else
		return "";
}


BOOST_AUTO_TEST_CASE(begin)
{
	initLogger();

	src::severity_logger<SeverityLevel> slg;
	BOOST_LOG_SEV(slg, info) << "start server";

	TcpServer server(8070);
	std::shared_ptr<Dispatcher> dispatcher = std::make_shared<Dispatcher>();
	dispatcher->add_handler("echo", [](std::string str) { return str; });
	dispatcher->add_handler("add", [](int a, int b) { return a + b; });
	dispatcher->add_handler("twowayAdd", &twowayAdd);
	dispatcher->add_handler("setUuid", &setUuid);
	dispatcher->add_handler("getUuid", &getUuid);
	server.setDispatcher(dispatcher);

	server.run();

	std::string s;
	std::cout << "exit? " << std::endl;
	std::cin >> s;
	server.stop();	
}

