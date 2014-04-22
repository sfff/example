
#include <cstdlib>
#include <cstdint>
#include <csignal>
#include <array>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/thread/externally_locked_stream.hpp>


template<std::size_t BUFFER_SIZE = 0x10000>
class server : boost::noncopyable
{
public:
	explicit server(unsigned short port)
		:
		signals_(io_), socket_(io_)
	{
		signals_.add(SIGINT);
		signals_.add(SIGTERM);
		signals_.add(SIGQUIT);
		signals_.async_wait(
				[this] (boost::system::error_code, int /* signo */)
				{
					io_.stop();
				}
		);

		boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::udp::v6(), port);
		socket_.open(endpoint.protocol());
		socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
		socket_.bind(endpoint);

		receive();
	}

	void run()
	{
		io_.run();
	}

private:
	void receive()
	{
		socket_.async_receive_from(
				boost::asio::buffer(buffer_), sender_endpoint_,
				[this] (boost::system::error_code ec, std::size_t bytes_recvd)
				{
					if (!ec)
						send(bytes_recvd);
					else
						receive();
				}
		);
	}

	void send(std::size_t length)
	{
		socket_.async_send_to(
				boost::asio::buffer(buffer_, length), sender_endpoint_,
				[this] (boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/)
				{
					receive();
				}
		);
	}

private:
	boost::asio::io_service           io_;
	boost::asio::signal_set           signals_;
	boost::asio::ip::udp::socket      socket_;
	boost::asio::ip::udp::endpoint    sender_endpoint_;
	std::array<uint8_t, BUFFER_SIZE>  buffer_;
};

class session : boost::noncopyable
{
public:
	explicit session(boost::asio::ip::address const& address, unsigned short port);

	void echo(std::vector<uint8_t> const& request);

private:
	boost::asio::io_service         io_;
	boost::asio::ip::udp::socket    socket_;
	boost::asio::ip::udp::endpoint  endpoint_;
};

class client : boost::noncopyable
{
public:
	explicit client(std::ostream& ostream, boost::asio::ip::address const& address, unsigned short port)
		: ostream_(ostream, terminal_mutex_), address_(address), port_(port)
	{}

	void run_test();

private:
	void thread_func(unsigned n);

private:
	boost::thread_group                            threads_;
	boost::recursive_mutex                         terminal_mutex_;
	boost::externally_locked_stream<std::ostream>  ostream_;
	boost::asio::ip::address                       address_;
	unsigned short                                 port_;
};

int main(int argc, char* argv[])
{
	try
	{
		unsigned short port = 12122;
		boost::asio::ip::address addr = boost::asio::ip::address::from_string("::1");

		server<> serv(port);
		client cli(std::cout, addr, port);

		boost::shared_ptr<boost::thread> th(new boost::thread([&serv] () { serv.run(); }));

		cli.run_test();

		// stop server
		std::raise(SIGINT);

		th->join();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}

	return 0;
}

void client::run_test()
{
	unsigned optimal_threads_count = boost::thread::hardware_concurrency();

	if (optimal_threads_count == 0)
		optimal_threads_count = 1;

	ostream_ << "Create " << optimal_threads_count << " threads for client" << std::endl;

	for (unsigned i = 0; i < optimal_threads_count; ++i)
		threads_.add_thread(new boost::thread([this, i] () { thread_func(i); }));

	threads_.join_all();
	ostream_ << "All client's threads done" << std::endl;
}

void client::thread_func(unsigned n)
{
	session s(address_, port_);

	std::size_t message_sizes[] = { 0, 1, 3, 7, 234, 6432, 23221, 3311, 34, 4521, 333 };

	for (auto i : message_sizes)
	{
		std::vector<uint8_t> mess(i, 'x');

		try
		{
			const std::size_t cnt = 1024 * 10;

			for (auto i = 0; i < cnt; ++i)
				s.echo(mess);

			ostream_ << "#" << n << ": " << cnt << " echos (size " << mess.size() << " bytes) received" << std::endl;
		}
		catch (std::exception& e)
		{
			ostream_ << "#" << n << ": " << "Exception: " << e.what() << std::endl;
		}
	}
}

session::session(boost::asio::ip::address const& address, unsigned short port)
	: socket_(io_)
{
	endpoint_.address(address);
	endpoint_.port(port);

	socket_.open(endpoint_.protocol());
}

void session::echo(std::vector<uint8_t> const& request)
{
	auto reply = request;

	socket_.send_to(boost::asio::buffer(request), endpoint_);

	std::size_t reply_length = socket_.receive_from(boost::asio::buffer(reply), endpoint_);

	if (reply_length != request.size())
		throw std::logic_error("Different size of reply and request");
	if (reply != request)
		throw std::logic_error("Different data value of request and reply");
}

