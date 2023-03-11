#pragma once

#include <string>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace http_server {

namespace beast = boost::beast;

void ReportError(beast::error_code ec, std::string_view what) {
    std::cerr << what << ": " << ec.message() << std::endl;
}

}