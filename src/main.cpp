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

namespace net = boost::asio;
using namespace std::literals;
namespace sys = boost::system;
namespace http = boost::beast::http;

namespace http_server {

namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;
namespace sys = boost::system;
namespace http = beast::http;

void ReportError(beast::error_code ec, std::string_view what) {
    std::cerr << what << ": " << ec.message() << std::endl;
}

class SessionBase {
public:
    SessionBase(const SessionBase&) = delete;
    SessionBase& operator=(const SessionBase&) = delete;

    void Run() {
        net::dispatch(stream_.get_executor(), beast::bind_front_handler(&SessionBase::Read, GetSharedThis()));
    }

protected:
    explicit SessionBase(tcp::socket&& socket): stream_(std::move(socket)) {}
    using HttpRequest = http::request<http::string_body>;
    ~SessionBase() = default;

    template <typename Body, typename Fields>
    void Write(http::response<Body, Fields>&& response) {
        auto safe_response = std::make_shared<http::response<Body, Fields>>(std::move(response));
        auto self = GetSharedThis();
        http::async_write(stream_, *safe_response, [safe_response, self] (beast::error_code ec, std::size_t bytes_written) {
            self->OnWrite(safe_response->need_eof(), ec, bytes_written);
        });
    }
private:
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    HttpRequest request_;

    void Read() {
        using namespace std::literals;
        request_ = {};
        stream_.expires_after(30s);

        http::async_read(stream_, buffer_, request_, beast::bind_front_handler(&SessionBase::OnRead, GetSharedThis()));
    }

    void OnRead(beast::error_code ec, [[maybe_unused]] std::size_t bytes_read) {
        using namespace std::literals;
        if (ec == http::error::end_of_stream) {
            return Close();
        }

        if (ec) {
            return ReportError(ec, "read"sv);
        }

        HandleRequest(std::move(request_));
    }

    void OnWrite(bool close, beast::error_code ec, [[maybe_unused]] std::size_t bytes_written) {
        if (ec) {
            return ReportError(ec, "write"sv);
        }

        if (close) {
            return Close();
        }

        Read();
    }

    void Close() {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    }

    virtual void HandleRequest(HttpRequest&& request) = 0;
    virtual std::shared_ptr<SessionBase> GetSharedThis() = 0;
};

template <typename RequestHandler>
class Session : public SessionBase, public std::enable_shared_from_this<Session<RequestHandler>> {
public:
    template <typename Handler>
    Session(tcp::socket&& socket, Handler&& request_handler)
        : SessionBase(std::move(socket)), request_handler_(request_handler) {}
private:
    RequestHandler request_handler_;

    std::shared_ptr<SessionBase> GetSharedThis() override {
        return this->shared_from_this();
    }

    void HandleRequest(HttpRequest&& request) override {
        request_handler_(std::move(request), [self = this->shared_from_this()](auto&& response) {
            self->Write(std::move(response));
        });
    }
};

template <typename RequestHandler>
class Listener : public std::enable_shared_from_this<Listener<RequestHandler>> {
public:
    template <typename Handler>
    Listener(net::io_context& ioc, const tcp::endpoint& endpoint, Handler&& request_handler)
            : ioc_(ioc),
              acceptor_(net::make_strand(ioc)),
              request_handler_(std::forward<Handler>(request_handler)) {

        acceptor_.open(endpoint.protocol()); // open acceptor to receive connections
        acceptor_.set_option(net::socket_base::reuse_address(true)); 

        acceptor_.bind(endpoint);
        acceptor_.listen(net::socket_base::max_listen_connections);
    }


    void Run() {
        DoAccept();
    }
private:
    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    RequestHandler request_handler_;

    void DoAccept() {
        acceptor_.async_accept(net::make_strand(ioc_), beast::bind_front_handler(&Listener::OnAccept, this->shared_from_this()));
    }

    void OnAccept(sys::error_code ec, tcp::socket socket) {
        using namespace std::literals;

        if (ec) {
            return ReportError(ec, "accept"sv);
        }

        AsyncRunSession(std::move(socket));
        DoAccept();
    }

    void AsyncRunSession(tcp::socket&& socket) {
        std::make_shared<Session<RequestHandler>>(std::move(socket), request_handler_)->Run();
    }
};

template <typename RequestHandler>
void ServeHttp(net::io_context& ioc, const tcp::endpoint& endpoint, RequestHandler&& handler) {
    using MyListener = Listener<std::decay_t<RequestHandler>>;
    std::make_shared<MyListener>(ioc, endpoint, std::forward<RequestHandler>(handler))->Run();
}

}

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
            // Добавляем заголовок Content-Type: text/html
    response.set(http::field::content_type, "text/html");
    // std::string str = request.target();
    std::string_view trgt = request.target();
    response.body() = "<strong>Hello, "s + ProcessTarget(trgt.data()) + "</strong>"s;
            // Формируем заголовок Content-Length, сообщающий длину тела ответа
    response.content_length(response.body().size());
            // Формируем заголовок Connection в зависимости от значения заголовка в запросе
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
    // to stop server in the right way
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