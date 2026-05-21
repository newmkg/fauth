#pragma once

#include <boost/json.hpp>
#include <boost/algorithm/string.hpp>

#include <curl/curl.h>

#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace env{
  inline const char*getEnvVariable(const char*variableName){
    const char*val=std::getenv(variableName);
    if(!val) throw std::runtime_error(std::format("envVariable={} doesn't exists",variableName));
    return val;
  }
}
namespace myBoostUtil{
  template<typename T>
  static void saveToJsonObject(boost::json::object&jobj,const char*key,const T&val){
    jobj.emplace(key,val);
  }
  template<typename T>
  static void saveToJsonObject(boost::json::object&jobj,const char*key,const std::optional<T>&val){
    if(val) saveToJsonObject(jobj,key,*val);
  }
  template<typename T>
  static std::optional<T> getFormJsonObject(const boost::json::object& jobj, const char* key){
    const auto it = jobj.find(key);
    if(it == jobj.end() || it->value().is_null())return std::nullopt;
    const auto& v = it->value();
    if constexpr (std::is_same_v<T, bool>) return v.as_bool();
    else if constexpr (std::is_same_v<T, std::string>) {
      std::string val{v.as_string().c_str()};
      boost::algorithm::trim(val);
      return val;
    }
    else if constexpr (std::is_same_v<T, long>) {
      return v.as_int64();
    }
    else if constexpr (std::is_same_v<T, ulong>) {
      return v.as_uint64();
    }
    else if constexpr (std::is_same_v<T, int>) {
      const auto x = v.as_int64();
      if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max()) {
	throw std::out_of_range{"Int32 out of range"};
      }
      return static_cast<int>(x);
    }
    else if constexpr (std::is_same_v<T, int>) {
      const auto x = v.as_uint64();
      if (x > std::numeric_limits<int>::max()) {
	throw std::out_of_range{"Uint32 out of range"};
      }
      return static_cast<uint>(x);
    }
    else if constexpr (std::is_same_v<T, short>) {
      const auto x = v.as_int64();
      if (x < std::numeric_limits<short>::min() || x > std::numeric_limits<short>::max()) {
	throw std::out_of_range{"Int16 out of range"};
      }
      return static_cast<short>(x);
    }
    else if constexpr (std::is_same_v<T, ushort>) {
      const auto x = v.as_uint64();
      if (x > std::numeric_limits<ushort>::max()) {
	throw std::out_of_range{"Uint16 out of range"};
      }
      return static_cast<ushort>(x);
    }
    else if constexpr (std::is_same_v<T, double>) {
      return v.as_double();
    }
    else if constexpr (std::is_same_v<T, float>) {
      return static_cast<float>(v.as_double());
    }
    else {
      static_assert(!std::is_same_v<T, T>, "Unsupported type");
    }
  }
  template <typename T>
  inline std::expected<boost::json::value,std::string> parse(const T &str) {
    boost::system::error_code ec;
    boost::json::value jval = boost::json::parse(str, ec);
    if (ec) return std::unexpected(ec.message());
    return jval;
  }
}
namespace curl {
  static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
  }
  struct CurlHandle {
    CURL* handle = nullptr;
    CurlHandle() {
      handle = curl_easy_init();
      if (!handle) throw std::runtime_error("curl init failed");
    }
    ~CurlHandle() {
      if (handle) curl_easy_cleanup(handle);
    }
    void reset(){curl_easy_reset(handle);}
    operator CURL *() const { return handle; } // implicit conversion
    std::string escape(const std::string &str) {
      char *output = curl_easy_escape(handle, str.c_str(), 0);
      std::string res(output);
      curl_free(output);
      return res;
    }
  };
  struct CurlSlistHandle {
    curl_slist* headers=nullptr;
    CurlSlistHandle() = default;
    ~CurlSlistHandle(){ curl_slist_free_all(headers); }
    CurlSlistHandle(const CurlSlistHandle&) = delete;
    CurlSlistHandle& operator=(const CurlSlistHandle&) = delete;
    CurlSlistHandle(CurlSlistHandle&& o) noexcept {
      headers = o.headers;
      o.headers = nullptr;
    }
    void append(const std::string& header){
      headers = curl_slist_append(headers, header.c_str());
    }
    operator curl_slist*() const { return headers; }
  };
  struct CurlMimeHandle {
    curl_mime *mime = nullptr;
    CurlMimeHandle(CurlHandle &curl) { mime = curl_mime_init(curl); }
    ~CurlMimeHandle(){curl_mime_free(mime);}
  };
} // namespace curl
