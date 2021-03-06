#pragma once
#ifndef HTTPDBG_HTTPDBG_SERVER
#define HTTPDBG_HTTPDBG_SERVER

#ifdef HTTP_DBG

#include <thread>
#include "../mutex.hpp"
#include <memory>
#include <boost/asio.hpp>

class c_tunserver;
class server{
	public:
		server(boost::asio::io_service& io_service, short port,
				const c_tunserver & tunserver);

	private:
		void do_accept();
		boost::asio::ip::tcp::acceptor acceptor_;
		boost::asio::ip::tcp::socket socket_; ///< listen http on this socket
		const c_tunserver & tunserver_; ///< will debug this object [THREAD] must be locked by it's ->get_my_mutex()
};

/**
 * Read data from c_tunserver, generate a string (e.g. with HTML) that shows it's debug info.
*/
class c_httpdbg_raport {
	public:
		c_httpdbg_raport(const c_tunserver & target);
		std::string generate(std::string url);

	protected:
		const c_tunserver & m_target;
		std::string page_template;
		std::ostringstream& generate_current_date(std::ostringstream &);
		std::ostringstream& generate_table_of_peers(std::ostringstream &, std::string &url);
		std::ostringstream& generate_table_of_tunnels(std::ostringstream &, std::string &url);
		std::string get_data();
		std::string get_charts(std::string &url);
		static std::string HTML(const std::string & s);
		static std::string get_page_template(const std::string & file_name);
		template <class T>
		static std::string HTML(const T & obj);
};

template <class T>
std::string c_httpdbg_raport::HTML(const T & obj) {
	std::ostringstream out;
	out << obj;
	return HTML( out.str() ); // use the normal-string version of our function
}

class c_httpdbg_server{
	public:
		c_httpdbg_server(int opt_port, const c_tunserver & tunserver);
		int run();
		void stop();
		void set_port(int port);

	protected:
		int m_opt_port;
		boost::asio::io_service m_io_service;
		const c_tunserver & m_tunserver;
};

class session : public std::enable_shared_from_this<session>
{
	public:
		session(boost::asio::ip::tcp::socket socket, const c_tunserver & tunserver);
		void start();

	private:
		void do_read();
		void do_write(size_t length);
		boost::asio::ip::tcp::socket socket_;
		enum { max_length = 1024 };
		char data_[max_length];
		std::string m_reply;
		const c_tunserver & tunserver_;
};
#endif
#endif
