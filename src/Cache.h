#ifndef CACHE_H
#define CACHE_H

#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <regex>
#include <sstream>
#include <ctime>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <map>
#include <iomanip>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>



struct CachedResponse {
//    std::string body;
    boost::beast::http::response<boost::beast::http::dynamic_body> response;
    std::chrono::system_clock::time_point expiration;
    std::chrono::system_clock::time_point expire;
    std::string ETag;
    std::string Last_Modified;
    std::string uri;

    int max_age = -1;
    bool must_revalidate = false;
    bool no_cache = false;
    bool no_store = false;
    bool is_private = false;
    bool is_chunk = false;

    void parseCacheControl(const boost::beast::http::response<boost::beast::http::dynamic_body>& response);

    bool checkCacheable(std::uint64_t session_id);

    void printCacheStatus(std::uint64_t session_id);

    std::string getExpirationTime() const;
};

class Cache {

private:
    static std::unordered_map<std::string, CachedResponse> cache;
    static std::mutex cache_mutex;

public:

    static CachedResponse *get_request(const std::string &uri, std::uint64_t session_id);
    static void update_expiration(const std::string& uri, const std::string& cacheControl, const std::string& expires, std::uint64_t session_id);
    // static void store_request(const std::string& uri, const std::string& body, int ttl);
    static void store_request(const std::string &uri, const boost::beast::http::response<boost::beast::http::dynamic_body> &response, std::uint64_t session_id);
    static bool check_revalidate(const CachedResponse &cached_response, std::uint64_t session_id);
    static bool check_expiration(const CachedResponse &cached_response, std::uint64_t session_id);

};

#endif // CACHE_H
