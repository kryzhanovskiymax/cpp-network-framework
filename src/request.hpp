#pragma once

#include <utility>

#include <boost/json.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace http_request {

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;

class HttpRequest {
public:
    HttpRequest(const HttpRequest&) = default;
    HttpRequest(HttpRequest&&) = default;
    HttpRequest(http::request<http::string_body>&& request);
    
    std::vector<std::string> GetUrl() const;
    json::object GetBody() const;
    bool KeepAlive() const;
private:
    std::vector<std::string> url_;
    http::header<true, http::fields> headers_;
    json::object body_;
    bool keep_alive_;

    std::vector<std::string> ParseUrl(std::string&& url) const;
};

HttpRequest::HttpRequest(http::request<http::string_body>&& request) {
    url_ = ParseUrl(std::move(request.target().data()));
    body_ = json::parse(request.body()).get_object();
    headers_ = request.base();
    keep_alive_ = request.keep_alive();
}

std::vector<std::string> HttpRequest::GetUrl() const {
    return url_;
}

json::object HttpRequest::GetBody() const {
    return body_;
}

std::vector<std::string> HttpRequest::ParseUrl(std::string&& url) const {
    std::vector<std::string> res;
    std::string word;
    for (const char& c : url) {
        if (c == '/') {
            if (word.size() > 0) {
                res.push_back(word);
                word = "";
            }
        } else {
            word += c;
        }
    }
    res.push_back(word);
    return res;
}

}