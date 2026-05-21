#pragma once

#include "./mylib.hpp"
#include "./myFauth.hpp"

#include <boost/json/impl/serialize.hpp>
#include <boost/json/value.hpp>
#include <cstddef>
#include <ev.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
//-------------------------
namespace myLibev{
  struct Client {
    struct ev_loop* loop{};
    int fd{-1};
    ev_io readWatcher{}, writeWatcher{};
    ev_timer readTimer{}, writeTimer{};
    std::string inBuf, outBuf;
    Client(
	   struct ev_loop* loop_,int fd_,
	   void (*readCb)(EV_P_ ev_io*, int),
	   void (*writeCb)(EV_P_ ev_io*, int),
	   void (*readTimeoutCb)(EV_P_ ev_timer*, int),
	   void (*writeTimeoutCb)(EV_P_ ev_timer*, int),
	   double readTimeoutSec=5.0,double writeTimeoutSec=5.0
	   ) : loop(loop_),fd(fd_){
      ev_io_init(&readWatcher, readCb, fd, EV_READ);
      ev_io_init(&writeWatcher, writeCb, fd, EV_WRITE);
      ev_timer_init(&readTimer, readTimeoutCb, 0.0,readTimeoutSec);
      ev_timer_init(&writeTimer, writeTimeoutCb, 0.0,writeTimeoutSec);
      readWatcher.data = this;
      writeWatcher.data = this;
      readTimer.data = this;
      writeTimer.data = this;
    }
    ~Client(){
      if(loop){
	ev_io_stop(loop, &readWatcher);
	ev_io_stop(loop, &writeWatcher);
	ev_timer_stop(loop, &readTimer);
	ev_timer_stop(loop, &writeTimer);
      }
      if(fd >= 0){
	::close(fd);
	fd = -1;
      }
    }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    //------
    void startReadTimer(){
      ev_timer_again(loop, &readTimer);
    }
    void startWriteTimer(){
      ev_timer_again(loop, &writeTimer);
    }
    void stopReadTimer(){
      ev_timer_stop(loop,&readTimer);
    }
    void stopWriteTimer(){
      ev_timer_stop(loop,&writeTimer);
    }
    void startReadWatcher(){
      ev_io_start(loop,&readWatcher);
      startReadTimer();
    }
    void startWriteWatcher(){
      ev_io_start(loop,&writeWatcher);
      startWriteTimer();
    }
    void stopReadWatcher(){
      ev_io_stop(loop,&readWatcher);
      stopReadTimer();
    }
    void stopWriteWatcher(){
      ev_io_start(loop,&writeWatcher);
      stopWriteTimer();
    }
    //------------
    void sendJson(const boost::json::value&jv){
      outBuf=boost::json::serialize(jv);
      //std::clog<<"outBuf="<<outBuf<<std::endl;
      startWriteWatcher();
    }
    void processInput(){
      if(inBuf.empty()) return;
      //std::clog<<"inBuff="<<inBuf<<std::endl;
      try{
	const auto jo=myBoostUtil::parse(inBuf);
	inBuf.clear();
	if(!jo){
	  sendJson(boost::json::value{{"success",false},{"error",jo.error()}});
	  return;
	}
	const std::string_view&cmd=jo->at("cmd").as_string();
	if(cmd=="verfToken"){
	  const std::string_view&token=jo->at("token").as_string();
	  const std::string_view&projectId=jo->at("projectId").as_string();
	  sendJson(myFauth::getVerfResult(projectId,token));
	}else{
	  sendJson(boost::json::object{{"success",false},{"error","unknown cmd"}});
	}
      }catch(const std::exception&e){
	sendJson(boost::json::object{{"success",false},{"error",e.what()}});
      }
    }
    //---------
    static void closeClient(EV_P_ Client* c){
      //std::clog<<"closeClient"<<std::endl;
      delete c;
    }
    static void readTimeoutCb(EV_P_ ev_timer* w, int revents){
      //std::clog<<"readTimeoutCb"<<std::endl;
      auto* c = static_cast<Client*>(w->data);
      closeClient(EV_A_ c);
    }
    static void writeTimeoutCb(EV_P_ ev_timer* w, int revents){
      //std::clog<<"writeTimeoutCb"<<std::endl;
      auto* c = static_cast<Client*>(w->data);
      closeClient(EV_A_ c);
    }
    static void clientReadCb(EV_P_ ev_io* w, int revents){
      //std::clog<<"clientReadCb"<<std::endl;
      Client* c = static_cast<Client*>(w->data);
      char buf[200];
      static constexpr std::size_t maxReadPerCb = 256*1024;
      std::size_t totalRead=0;
      while(totalRead<maxReadPerCb){
	//std::clog<<"n="<<n<<std::endl;
	if(auto n=::read(c->fd,buf,sizeof(buf));n>0){
	  c->startReadTimer();
	  c->inBuf.append(buf, static_cast<std::size_t>(n));
	  totalRead += static_cast<std::size_t>(n);
	  //std::clog<<"inBuff now="<<c->inBuf<<std::endl;
	  continue;
	}else if(n == 0){
	  //std::clog<<"no read available now"<<std::endl;
	  //std::clog<<"process input now"<<std::endl;
	  c->stopReadWatcher();
	  c->processInput();
	  return;
	}else{
	  if(errno == EAGAIN || errno == EWOULDBLOCK){
	    //std::clog<<"Break: clientReadCb err: "<<errno<<std::endl;
	    break;
	  }else{
	    //std::clog<<"closeClient & return"<<std::endl;
	    closeClient(EV_A_ c);
	    return;
	  }
	}
      }
    }
    static void writeCb(EV_P_ ev_io* w, int revents){
      //std::clog<<"writeCb"<<std::endl;
      auto* c = static_cast<Client*>(w->data);
      while(!c->outBuf.empty()){
	//std::clog<<"writeCb send loop n="<<n<<std::endl;
	if(auto n=::write(c->fd,c->outBuf.data(),c->outBuf.size());n>0){
	  //std::clog<<"writeCb send response data to client, n="<<n<<std::endl;
	  c->startWriteTimer();
	  c->outBuf.erase(0, static_cast<std::size_t>(n));
	  continue;
	}else if(n==0){
	  //std::clog<<"writeCb Return: send response to client completed, n=0"<<std::endl;
	  closeClient(EV_A_ c);
	  return;
	}else{
	  if(errno == EAGAIN || errno == EWOULDBLOCK){
	    //std::clog<<"writeCb Return: clientReadCb err: "<<errno<<std::endl;
	    return;
	  }else{
	    //std::clog<<"writeCb closeClient & return"<<std::endl;
	    closeClient(EV_A_ c);
	    return;
	  }
	}
      }
      closeClient(EV_A_ c);
    }
  };
  //--------------------------
  struct Server{
    static void setNonBlock(int fd){
      int flags = fcntl(fd, F_GETFL, 0);
      if(flags < 0) throw std::runtime_error(std::string("fcntl F_GETFL failed: ") + std::strerror(errno));
      if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) throw std::runtime_error(std::string("fcntl F_SETFL failed: ") + std::strerror(errno));
    }
    int fd{-1};
    ev_io acceptWatcher{};
    std::string serverPath;
    struct ev_loop* loop{EV_DEFAULT};
    Server(const char*path,void (*acceptCb)(EV_P_ ev_io*, int)):serverPath(path){
      ::unlink(path);
      fd=::socket(AF_UNIX, SOCK_STREAM, 0);
      if(fd < 0) throw std::runtime_error("socket failed");
      setNonBlock(fd);
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
      if(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) throw std::runtime_error("bind failed");
      if(listen(fd, 128) < 0) throw std::runtime_error("listen failed");
      //----------
      ev_io_init(&acceptWatcher, acceptCb, fd, EV_READ);
      acceptWatcher.data = this;
      ev_io_start(loop, &acceptWatcher);
    }
    ~Server(){
      if(loop) ev_io_stop(loop, &acceptWatcher);
      if(fd >= 0){
	::close(fd);
	fd = -1;
      }
      if(!serverPath.empty()) ::unlink(serverPath.c_str());
    }
    void run(){
      ev_run(loop, 0);
    }
    int getCfd() const noexcept {
      return ::accept(fd, nullptr, nullptr);
    }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    //--------------------------------------------------
    Client*getClient(){
      static constexpr double readTimeoutSec=5.0,writeTimeoutSec=5.0;
      int cfd = getCfd();
      if(cfd < 0){
	if(errno != EAGAIN && errno != EWOULDBLOCK) std::cerr<<"accept failed: "<<std::strerror(errno)<<std::endl;
	return nullptr;
      }
      return new Client{loop,cfd,Client::clientReadCb,Client::writeCb,Client::readTimeoutCb,Client::writeTimeoutCb,readTimeoutSec,writeTimeoutSec};
    }
    static void acceptCb(EV_P_ ev_io* w, int revents){
      Server* s = static_cast<Server*>(w->data);
      while(true){
	Client*c=s->getClient();
	if(!c)break;
	c->startReadWatcher();
      }
    }
  };
}
