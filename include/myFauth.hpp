// /include/myFauth.hpp
#pragma once

#include "mylib.hpp"
#include <expected>
#include <jwt-cpp/traits/boost-json/traits.h>
#include <jwt-cpp/traits/boost-json/defaults.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/base.h>
#include <shared_mutex>
#include <string_view>
#include <map>

namespace myFauth{
  struct DecodedIdToken{
    std::string alg,kid;
    std::time_t exp,iat,authTime;
    std::string aud,iss,sub;
    //
    std::string userId,email;
    std::optional<std::string> name,picture;
    bool emailVerified;
    boost::json::value firebase;
    DecodedIdToken(const jwt::decoded_jwt<jwt::traits::boost_json>&decodedIdToken){
      const boost::json::value&jval=decodedIdToken.get_payload_json();
      const boost::json::object&jobj=jval.as_object();
      //std::clog<<jobj<<std::endl;
      alg=decodedIdToken.get_header_claim("alg").as_string();
      kid=decodedIdToken.get_header_claim("kid").as_string();
      exp=static_cast<std::time_t>(jval.at("exp").as_int64());
      iat=static_cast<std::time_t>(jval.at("iat").as_int64());
      authTime=static_cast<std::time_t>(jval.at("auth_time").as_int64());
      aud=jval.at("aud").as_string().c_str();
      iss=jval.at("iss").as_string().c_str();
      sub=jval.at("sub").as_string().c_str();
      {
	auto it=jobj.find("name");
	if(it!=jobj.end()) name=it->value().as_string().c_str();
      }
      userId=jval.at("user_id").as_string().c_str();
      email=jval.at("email").as_string().c_str();
      {
	auto it=jobj.find("picture");
	if(it!=jobj.end()) picture=it->value().as_string().c_str();
      }
      emailVerified=jval.at("email_verified").as_bool();
      firebase=jval.at("firebase");
    }
    bool isIssOk(const std::string_view&firebaseProjectId)const noexcept{
      static constexpr std::string_view PREFIX="https://securetoken.google.com/";
      return iss.size()==PREFIX.size()+firebaseProjectId.size()
	&& iss.starts_with(PREFIX)
	&& iss.substr(PREFIX.size()) == firebaseProjectId;
    }
    bool isValid(const std::string_view&firebaseProjectId)const noexcept{
      std::time_t now=std::time(nullptr);
      return alg=="RS256" && exp>now && iat-300<=now
	&& aud==firebaseProjectId && isIssOk(firebaseProjectId)
	&& (!sub.empty() || sub==userId) && authTime-300<=now;
    }
    boost::json::value toJval(const std::string_view&firebaseProjectId)const noexcept{
      boost::json::object jobj{
	{"userId",userId},
	{"email",email},
	{"emailVerified",emailVerified},
	{"firebase",firebase},
	{"isValid",isValid(firebaseProjectId)},
      };
      if(name) jobj["name"]=*name;
      if(picture) jobj["picture"]=*picture;
      return jobj;
    }
  };
  class FauthTokenVerifier {
    struct HeaderInfo {
      long maxAge = 3600;
    };
    static size_t headerCallback(char* buffer,size_t size,size_t nitems,void* userdata){
      size_t total = size * nitems;
      std::string_view line(buffer, total);
      auto* h = static_cast<HeaderInfo*>(userdata);
      if (line.starts_with("cache-control:")) {
	auto pos = line.find("max-age=");
	if (pos != std::string_view::npos) {
	  pos += 8;
	  long val = 0;
	  while (pos < line.size() && std::isdigit((unsigned char)line[pos])) {
	    val = val * 10 + (line[pos] - '0');
	    ++pos;
	  }
	  if (val > 0) h->maxAge = val;
	}
      }
      return total;
    }
    std::map<std::string, std::string> publicKeys;
    std::time_t nextRefreshAt;
    mutable std::shared_mutex mtx;
    boost::json::object getFauthPublicKeys() {
      static constexpr const char *authProvider="https://www.googleapis.com/robot/v1/metadata/x509/securetoken@system.gserviceaccount.com";
      curl::CurlHandle curl;
      std::string readBuffer;
      readBuffer.clear(); 
      curl_easy_setopt(curl, CURLOPT_URL, authProvider);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl::WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
      HeaderInfo info;
      curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
      curl_easy_setopt(curl, CURLOPT_HEADERDATA, &info);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "jwt-verifier/1.0");
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
      curl_easy_setopt(curl, CURLOPT_CAINFO,"/etc/ssl/certs/ca-certificates.crt");
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
      CURLcode res = curl_easy_perform(curl);
      if (res != CURLE_OK) {
	throw std::runtime_error(std::string("Failed to fetch Fauth Keys: ") + curl_easy_strerror(res));
      }
      nextRefreshAt=std::time(nullptr)+info.maxAge-60;
      return boost::json::parse(readBuffer).as_object();
    }      
    void fetchFauthKeys() {
      {
	std::shared_lock lck(mtx);
	if (!publicKeys.empty() && std::time(nullptr) < nextRefreshAt) return;
      }
      {
	std::unique_lock lck(mtx);
	auto jkeys = getFauthPublicKeys();
	publicKeys.clear();
	for (auto &[kid, cert] : jkeys) {
	  publicKeys[kid] = cert.as_string();
	}
      }      
    }
  public:
    FauthTokenVerifier() = default;
    std::expected<void,std::string> verifyIdToken(const jwt::decoded_jwt<jwt::traits::boost_json>&decodedJwt,const std::string_view&fauthProjectId) {
      fetchFauthKeys();
      //jwt::decoded_jwt<jwt::traits::boost_json> decoded = jwt::decode<jwt::traits::boost_json>(idToken);
      std::string kid = decodedJwt.get_header_claim("kid").as_string();
      auto getKey = [&](const std::string &kid) -> std::string {
	std::shared_lock lck(mtx);
	auto it = publicKeys.find(kid);
	if (it != publicKeys.end()) return it->second;
	return {};
      };
      std::string pubkey = getKey(kid);
      if (pubkey.empty()) {
	//force refresh keys if kid not found
	fetchFauthKeys();
	pubkey = getKey(kid);
	if (pubkey.empty()) return std::unexpected("No matching public key found after refresh for kid=" + kid);
      }
      const std::string issuer="https://securetoken.google.com/"+std::string(fauthProjectId);
      auto verifier =jwt::verify<jwt::traits::boost_json>()
	.allow_algorithm(jwt::algorithm::rs256(pubkey, "", "", ""))
	.with_issuer(issuer).with_audience(fauthProjectId.data()); 
      try {
	verifier.verify(decodedJwt);
	return {};
      } catch (const jwt::error::token_verification_exception& e) {
	return std::unexpected(std::string("Verification failed: ") + e.what());
      } catch (const std::exception& e) {
	return std::unexpected(std::string("Error: ") + e.what());
      }
    }
  };
  inline boost::json::value getVerfResult(const std::string_view&fauthProjectId,const std::string_view&idToken){
    auto decodedJwt=jwt::decode<jwt::traits::boost_json>(idToken.data());
    static FauthTokenVerifier idVerifier;
    auto verified=idVerifier.verifyIdToken(decodedJwt,fauthProjectId);
    if(!verified) {
      return {
	{"success",false},
	{"error",std::format("Verification failure={}",verified.error())},
      };
    }
    return {
      {"success",true},
      {"idtoken",DecodedIdToken(decodedJwt).toJval(fauthProjectId)},
    };
  }
}
