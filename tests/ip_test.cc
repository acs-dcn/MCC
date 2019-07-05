#include "inet_addr.h"
#include <iostream>

using namespace infgen;

int main() {
  socket_address sa;

  ipv4_addr addr("192.168.1.1", 1024);
  std::cout << addr << std::endl;
  sa = make_ipv4_address(addr);

  ipv4_addr addr2(sa);
  std::cout << addr2 << std::endl;

  return 0;

}
