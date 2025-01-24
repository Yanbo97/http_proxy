#include "Cache.h"

std::unordered_map <std::string, CachedResponse> Cache::cache;
std::mutex Cache::cache_mutex;

#include <boost/beast/http.hpp>

void CachedResponse::parseCacheControl(const boost::beast::http::response<boost::beast::http::dynamic_body>& response) {
    bool maxAgePresent = false;

    for (const auto& header : response) {
        std::string name = header.name_string().to_string();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        std::string value = header.value().to_string();

        if (name == "last-modified") {
            Last_Modified = value;
        } else if (name == "etag") {
            ETag = value;
        } else if (name == "cache-control") {
            no_cache = value.find("no-cache") != std::string::npos;
            no_store = value.find("no-store") != std::string::npos;
            must_revalidate = value.find("must-revalidate") != std::string::npos;
            is_private = value.find("private") != std::string::npos;

            auto maxAgePos = value.find("max-age=");
            if (maxAgePos != std::string::npos) {
                auto maxAgeStart = maxAgePos + 8;
                auto maxAgeEnd = value.find(",", maxAgeStart);
                max_age = std::stoi(value.substr(maxAgeStart, maxAgeEnd - maxAgeStart));
                maxAgePresent = true;
                expiration = std::chrono::system_clock::now() + std::chrono::seconds(max_age);
            } else if (name == "transfer-encoding" && value == "chunked") {
                is_chunk = true;
            }
        }
    }

    for (const auto& header : response) {
        std::string name = header.name_string().to_string();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        if (name == "expires") {
            std::string value = header.value().to_string();
            std::tm tm = {};
            std::istringstream ss(value);
            ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S %Z");
            if (!ss.fail()) {
                expire = std::chrono::system_clock::from_time_t(std::mktime(&tm));

                if (!maxAgePresent) {
                    expiration = expire;
                }
            }
            break;
        }
    }
}

//boost::beast::http::request<boost::beast::http::empty_body> CachedResponse::create_revalidation_request() const {
//    boost::beast::http::request<boost::beast::http::empty_body> req;
//    req.method(boost::beast::http::verb::get);
//    req.target(uri); // Assuming 'uri' is the request URI
//    if (!ETag.empty()) {
//        req.set(boost::beast::http::field::if_none_match, ETag);
//    } else if (!Last_Modified.empty()) {
//        req.set(boost::beast::http::field::if_modified_since, Last_Modified);
//    }
//    return req;
//}


bool Cache::check_revalidate(const CachedResponse &cached_response, std::uint64_t session_id) {


    if (cached_response.must_revalidate) {
        //BOOST_LOG_TRIVIAL(info) << session_id << ": Must revalidate due to must-revalidate directive." << std::endl;
        return true;
    }


    return false;
}
bool Cache::check_expiration(const CachedResponse &cached_response, std::uint64_t session_id) {

    auto now = std::chrono::system_clock::now();
    // expiration is based on max age or expires provided by response headed
    if (cached_response.expiration < now) {
        // BOOST_LOG_TRIVIAL(info) << session_id << ": Requires revalidation due to expiration." << std::endl;
        return true;
    }

    return false;
}

void Cache::update_expiration(const std::string& uri, const std::string& cacheControl, const std::string& expires, std::uint64_t session_id) {
    std::lock_guard<std::mutex> guard(cache_mutex);
    auto it = cache.find(uri);
    if (it != cache.end()) {
        // Update based on Cache-Control max-age
        auto maxAgePos = cacheControl.find("max-age=");
        if (maxAgePos != std::string::npos) {
            auto maxAgeEnd = cacheControl.find(",", maxAgePos);
            int maxAge = std::stoi(cacheControl.substr(maxAgePos + 8, maxAgeEnd - (maxAgePos + 8)));
            it->second.expiration = std::chrono::system_clock::now() + std::chrono::seconds(maxAge);
        }

        else if (!expires.empty()) {
            std::tm tm = {};
            std::istringstream ss(expires);
            ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S %Z");
            if (!ss.fail()) {
                it->second.expire = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                it->second.expiration = it->second.expire; // Use expires as the new expiration
            }
        }
        BOOST_LOG_TRIVIAL(info) << session_id << ": Cache expiration updated for URI: " << uri;
    }
}

std::string CachedResponse::getExpirationTime() const {
    std::time_t expirationTimeT = std::chrono::system_clock::to_time_t(expiration);
    std::tm utc_tm = *std::gmtime(&expirationTimeT);
    char buffer[128];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &utc_tm);

    return std::string(buffer);
}

bool CachedResponse::checkCacheable(std::uint64_t session_id) {
    if (is_private) {
        BOOST_LOG_TRIVIAL(info) << session_id << ": Not cacheable because is_private is true." << std::endl;
        return false;
    }
//    if (ETag.empty() && Last_Modified.empty()) {
//        BOOST_LOG_TRIVIAL(info) << session_id << ": Not cacheable because both ETag and Last_Modified are missing." << std::endl;
//        return false;
//    }
    if (max_age == -1 && expire == std::chrono::system_clock::time_point{}) {
        BOOST_LOG_TRIVIAL(info) << session_id << ": Not cacheable because both max-age and Expires are missing."
                                << std::endl;
        return false;
    }

    if (no_store) {
        BOOST_LOG_TRIVIAL(info) << session_id << ": Not cacheable because no_store is true." << std::endl;
        return false;
    }
//    if (is_chunk) {
//        BOOST_LOG_TRIVIAL(info) << session_id << ": Not cacheable because is_chunk is true." << std::endl;
//        return false;
//    }

    return true;
}

void CachedResponse::printCacheStatus(std::uint64_t session_id) {
    //BOOST_LOG_TRIVIAL(info) << "URI: " << uri;
    //BOOST_LOG_TRIVIAL(info) << "Cache Status: ";
//    if (!ETag.empty()){
//        BOOST_LOG_TRIVIAL(NOTE) << "ETag: " << ETag;
//    }
//    if (!Last_Modified.empty()){
//        BOOST_LOG_TRIVIAL(NOTE) << "Last_Modified: " << Last_Modified;
//    }
    if (max_age!=-1){
        BOOST_LOG_TRIVIAL(info) << session_id << ": NOTE Cache-Control max-age: " << max_age;
    }
    if (no_store){
        BOOST_LOG_TRIVIAL(info) << session_id<< ": NOTE Cache-Control no-store: " << "True";
    }
    if (is_private){
        BOOST_LOG_TRIVIAL(info) << session_id << ": NOTE Cache-Control is-private: " << "True";
    }
    if (must_revalidate){
        BOOST_LOG_TRIVIAL(info) << session_id << ": NOTE Cache-Control must-revalidate: " << "True";
    }
    if (is_chunk){
        BOOST_LOG_TRIVIAL(info) << session_id << ": NOTE Cache-Control Chunked: " << "True";
    }
    //BOOST_LOG_TRIVIAL(info) << "Last-Modified: " << (Last_Modified.empty() ? "None" : Last_Modified);
    //BOOST_LOG_TRIVIAL(info) << "Max-Age: " << max_age;
//    BOOST_LOG_TRIVIAL(info) << "Must-Revalidate: " << (must_revalidate ? "True" : "False");
//    BOOST_LOG_TRIVIAL(info) << "No-Cache: " << (no_cache ? "True" : "False");
//    BOOST_LOG_TRIVIAL(info) << "No-Store: " << (no_store ? "True" : "False");
//    BOOST_LOG_TRIVIAL(info) << "Private: " << (is_private ? "True" : "False");
}

CachedResponse *Cache::get_request(const std::string &uri, std::uint64_t session_id) { // Send cache response to session
    std::lock_guard <std::mutex> guard(cache_mutex);
    auto it = cache.find(uri);
    if (it != cache.end()) { // in cache
        return &it->second;
        }
    return nullptr;
}

//CachedResponse *Cache::get_request(const std::string &uri, std::uint64_t session_id) {
//    std::lock_guard <std::mutex> guard(cache_mutex);
//    auto it = cache.find(uri);
//    if (it != cache.end()) { // in cache
//        auto now = std::chrono::system_clock::now();
//        if (it->second.expiration > now && !check_revalidate(it->second, session_id)) {
//            BOOST_LOG_TRIVIAL(info) << session_id << ": in cache, valid";
//            return &it->second;
//        }
//        std::string expirationTimeStr = it->second.getExpirationTime();
//        BOOST_LOG_TRIVIAL(info) << session_id << ": in cache, but expired at " << expirationTimeStr;
//        cache.erase(it);
//    }
//    return nullptr;
//}

void Cache::store_request(const std::string &uri, const boost::beast::http::response<boost::beast::http::dynamic_body> &response, std::uint64_t session_id) {
    std::lock_guard<std::mutex> guard(cache_mutex);

    CachedResponse cachedResponse;
    cachedResponse.uri = uri;
    cachedResponse.response = response;
    cachedResponse.parseCacheControl( response);
    cachedResponse.printCacheStatus(session_id);
    if (cachedResponse.checkCacheable(session_id)) {
        cache[uri] = std::move(cachedResponse);
        BOOST_LOG_TRIVIAL(info) << session_id << ": Stored in cache: " << uri;
    } else {
        BOOST_LOG_TRIVIAL(info) << session_id << ": Response not stored in cache due to cacheability rules";
    }
}

//void Cache::store_request(const std::string& uri, const std::string& body, int ttl, const std::string& responseStr, std::uint64_t session_id) {
//    std::lock_guard<std::mutex> guard(cache_mutex);
//    CachedResponse response;
//    response.body = body;
//    response.expiration = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);
//    response.parseCacheControl(responseStr);
//    response.uri = uri;
////    response.printCacheStatus();
//    auto now = std::chrono::system_clock::now();
//    // Check if the response is cacheable before storing
//    if (response.checkCacheable(session_id)) {
//        if (response.max_age != -1) {
//            response.expiration = now + std::chrono::seconds(response.max_age);
//        } else if (response.expiration != std::chrono::system_clock::time_point{}) {
//        } else {
//            // Neither max-age nor Expires is available; fallback to ttl or default behavior
//            response.expiration = now + std::chrono::seconds(ttl);
//        }
//        cache[uri] = response;
//        BOOST_LOG_TRIVIAL(info) << session_id<<": Stored in cache: " << uri << std::endl;
//    } else {
//        cache.erase(uri);
//        BOOST_LOG_TRIVIAL(info) << session_id << ": Response not stored in cache due to cacheability rules" << std::endl;
//    }
//}



