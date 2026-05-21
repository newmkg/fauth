#include "../include/myLibev.hpp"
//===========
int main(){
  std::cout <<"C++ Version = " << __cplusplus << std::endl;
  curl_global_init(CURL_GLOBAL_DEFAULT);
  static const char*socketPath=env::getEnvVariable("socketPath");
  myLibev::Server s(socketPath,myLibev::Server::acceptCb);
  std::cout << "dbInteract daemon listening: " << socketPath << '\n';
  s.run();
  curl_global_cleanup();
  return 0;
}
