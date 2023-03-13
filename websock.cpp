#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <boost/thread.hpp>



//------------------------------------------------------------------------------


class TcpSession : public std::enable_shared_from_this<TcpSession>
{
	boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
	boost::beast::flat_buffer buffer_;
	std::string text_;
	std::function<void(const std::vector<unsigned char>)> readCallback;
	std::function<void(boost::beast::error_code error, std::string message, std::string ip, unsigned short port)> errorCallback;
	std::string ipAddress;
	unsigned short port_;

public:
	// Take ownership of the socket
	explicit
		TcpSession(boost::asio::ip::tcp::socket&& socket, const std::string ip, unsigned short port)
		: ws_(std::move(socket)),
		ipAddress(ip),
		port_(port)
	{
	}
	~TcpSession()
	{
		//mapten sil
		std::cout << "destroyed" << std::endl;
	}
	// Get on the correct executor
	void run()
	{
		// We need to be executing within a strand to perform async operations
		// on the I/O objects in this TcpSession. Although not strictly necessary
		// for single-threaded contexts, this example code is written to be
		// thread-safe by default.
		boost::asio::dispatch(ws_.get_executor(), boost::beast::bind_front_handler(&TcpSession::on_run, shared_from_this()));
	}

	// Start the asynchronous operation
	void on_run()
	{
		// Set suggested timeout settings for the websocket
		ws_.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::server));
		//boost::beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));
		// Set a decorator to change the Server of the handshake

		ws_.set_option(boost::beast::websocket::stream_base::decorator(
			[](boost::beast::websocket::response_type& res)
			{
				res.set(boost::beast::http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-async");
			}));
		// Accept the websocket handshake
		//do_read();
		ws_.async_accept(boost::beast::bind_front_handler(&TcpSession::on_accept, shared_from_this()));
	}

	void on_accept(boost::beast::error_code ec)
	{
		if (ec)
		{
			std::cout << "Error: " << ec.what() << std::endl;
			if (errorCallback) errorCallback(ec, "socket handler accept", ipAddress, port_);
			return;
		}
		do_read();
		Send("server running !");
		// Read a message
	}

	void do_read()
	{
		// Clear the buffer
		buffer_.consume(buffer_.size());
		// Read a message into our buffer
		ws_.async_read(buffer_, boost::beast::bind_front_handler(&TcpSession::on_read, shared_from_this()));
	}

	void on_read(boost::beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);

		//      // This indicates that the TcpSession was closed
			  //if (ec == boost::beast::websocket::error::closed)
			  //{
			  //	if (errorCallback) errorCallback(ec, "socket handler session closed", ipAddress, port);
			  //	return;
			  //}

		if (ec)
		{
			if (errorCallback) errorCallback(ec, "socket handler read", ipAddress, port_);
			return;
		}



		std::string bl = boost::beast::buffers_to_string(buffer_.data());

		if (readCallback) readCallback(buffers_to_uchar_vector(buffer_.data()));


		Send("server running !");
		do_read();
	}

	void Send(const std::string data) {

		text_ = data;
		ws_.async_write(boost::asio::buffer(text_), boost::beast::bind_front_handler(&TcpSession::on_write, shared_from_this()));
	}

	void on_write(boost::beast::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);

		if (ec)
		{
			if (errorCallback) errorCallback(ec, "socket handler send", ipAddress, port_);
			return;
		}
		Send("server running !");
	}

	void setReadCallback(const std::function<void(const std::vector<unsigned char>)>& callback)
	{
		readCallback = callback;
	}

	void clearReadCallback()
	{
		std::function<void(const std::vector<unsigned char>)> empty;
		readCallback.swap(empty);
	}

	void setErrorCallback(const std::function<void(boost::beast::error_code error, std::string message, std::string ip, unsigned short port)>& callback)
	{
		errorCallback = callback;
	}

	void clearErrorCallback()
	{
		std::function<void(boost::beast::error_code error, std::string message, std::string ip, unsigned short port)> empty;
		errorCallback.swap(empty);
	}

	template<class ConstBufferSequence>
	std::vector<unsigned char>
		buffers_to_uchar_vector(ConstBufferSequence const& buffers)
	{
		static_assert(
			boost::beast::is_const_buffer_sequence<ConstBufferSequence>::value,
			"ConstBufferSequence type requirements not met");
		std::vector<unsigned char> result;
		result.reserve(boost::beast::buffer_bytes(buffers));
		auto const buff = static_cast<char const*>(buffers.data());
		//std::vector<unsigned char> vec(buff, buff + buffers.size());
		result.assign(buff, buff + buffers.size());
		return result;
	}
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the TcpSessions
class TcpServer : public std::enable_shared_from_this<TcpServer>
{
	boost::asio::io_context& ioc_;
	boost::asio::ip::tcp::acceptor acceptor_;//gelen baglantilari kabul eder
	boost::system::error_code& ec;
	std::function<void(const std::vector<unsigned char>)> readCallback;
	std::function<void(boost::beast::error_code error, std::string message, std::string ip, unsigned short port)> errorCallback;
	std::string ipAddress;
	unsigned short port;

public:
	TcpServer(boost::asio::io_context& ioc, boost::asio::ip::tcp::endpoint endpoint, boost::system::error_code& ec_)
		: ioc_(ioc)
		, acceptor_(ioc_)
		, ec(ec_)

	{
		ipAddress = endpoint.address().to_string();
		port = endpoint.port();
	
		// Open the acceptor
		acceptor_.open(endpoint.protocol(), ec);
		if (ec)
		{
			if (errorCallback) errorCallback(ec, "socket server open acceptor", ipAddress, port);
			return;
		}

		// Allow address reuse
		acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
		if (ec)
		{
			if (errorCallback) errorCallback(ec, "socket server acceptor set option", ipAddress, port);
			return;
		}

		// Bind to the server address
		acceptor_.bind(endpoint, ec);
		if (ec)
		{
			if (errorCallback) errorCallback(ec, "socket server acceptor bind", ipAddress, port);
			return;
		}

		// Start listening for connections
		acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
		if (ec)
		{
			if (errorCallback) errorCallback(ec, "socket server acceptor listen", ipAddress, port);
			return;
		}
	}

	// Start accepting incoming connections
	void run()
	{

		do_accept();
	}

	void setReadCallback(const std::function<void(const std::vector<unsigned char>)>& callback)
	{
		readCallback = callback;
	}

	void clearReadCallback()
	{
		std::function<void(const std::vector<unsigned char>)> empty;
		readCallback.swap(empty);
	}

	void setErrorCallback(const std::function<void(boost::beast::error_code error, std::string message, std::string ip, unsigned short port)>& callback)
	{
		errorCallback = callback;
	}

	void clearErrorCallback()
	{
		std::function<void(boost::beast::error_code error, std::string message, std::string ip, unsigned short port)> empty;
		errorCallback.swap(empty);
	}

private:
	void
		do_accept()
	{
		// The new connection gets its own strand
		acceptor_.async_accept(boost::asio::make_strand(ioc_), boost::beast::bind_front_handler(&TcpServer::on_accept, shared_from_this()));

	}

	void
		on_accept(boost::beast::error_code ec_, boost::asio::ip::tcp::socket socket)
	{
		ec = ec_;
		if (ec)
		{
			if (errorCallback) errorCallback(ec, "socket server on accept", ipAddress, port);
		}
		else
		{
			std::string r_ip = socket.remote_endpoint().address().to_string();
			unsigned short r_p = socket.remote_endpoint().port();
			// Create the TcpSession and run it
			std::cout<<"handle: "<<socket.native_handle();
			auto session = std::make_shared<TcpSession>(std::move(socket), r_ip, r_p);

			if (errorCallback) session->setErrorCallback(errorCallback);
			if (readCallback) session->setReadCallback(readCallback);
			session->run();
		}

		// Accept another connection
		do_accept();
	}
};

int main()
{
	std::cout << "Kuday Tcp Server Started! " << std::endl;
	//TcpServer 
	auto const address = boost::asio::ip::make_address("0.0.0.0");
	auto const port = static_cast<unsigned short>(std::atoi("12347"));
	boost::system::error_code ec;
	boost::asio::io_context ioc{ 1 };
	std::make_shared<TcpServer>(ioc, boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v4(), port }, ec)->run();
	ioc.run();

	return EXIT_SUCCESS;


}
