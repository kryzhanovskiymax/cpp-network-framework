#include <iostream>
#include <thread>
#include <vector>
#include <string>

#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "listener.hpp"
#include "session.hpp"

namespace net = boost::asio;
using namespace std::literals;
namespace sys = boost::system;
namespace http = boost::beast::http;

using StringRequest = http::request<http::string_body>;
using StringResponse = http::response<http::string_body>;

void DumpRequest(const StringRequest& req) {
    std::cout << req.method() << ' ' << req.target() << std::endl;
    for (const auto& header : req) {
        std::cout << " "sv << header.name_string() << ": "sv << header.value() << std::endl;
    }
}

std::string ProcessTarget(std::string s) {
    std::string str = "";
    for (const auto& ch : s) {
        if (ch != '/') {
            str += ch;
        }
    }
    return str;
}
StringResponse HandleRequest(StringRequest&& request) {
    DumpRequest(request);
    using namespace std::literals;
    StringResponse response(http::status::ok, request.version());
    response.set(http::field::content_type, "text/html");
    std::string_view trgt = request.target();
    response.body() = "<strong>Hello, "s + ProcessTarget(trgt.data()) + "</strong>"s;
    response.content_length(response.body().size());
    response.keep_alive(request.keep_alive());
    return response;
}

template <typename Fn>
void RunWorkers(unsigned int n, const Fn& fn) {
    n = std::max(1u, n);
    std::vector<std::thread> workers;
    workers.reserve(n - 1);
    while (--n) {
        workers.emplace_back(fn);
    }
    fn();

    for (int i = 0; i < workers.size(); ++i) {
        workers[i].join();
    }
}

int main() {
    const int num_threads = std::thread::hardware_concurrency();
    net::io_context ioc(num_threads);

    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&ioc](const sys::error_code& ec, [[maybe_unused]] int signal_number) {
        if (!ec) {
            std::cout << "Signal "sv << signal_number << " received"sv << std::endl;
            ioc.stop();
        }
    });

    const auto address = net::ip::make_address("0.0.0.0");
    const int PORT = 8181;
    constexpr net::ip::port_type port = PORT;
    http_server::ServeHttp(ioc, {address, port}, [](auto&& req, auto&& sender) {
        sender(HandleRequest(std::forward<decltype(req)>(req)));
    });

    std::cout << "Asynchronous server started on PORT: " << PORT << std::endl; 
    RunWorkers(num_threads, [&ioc] {
        ioc.run();
    });
    std::cout << "Shutting down"sv << std::endl;    

    return 0;
}