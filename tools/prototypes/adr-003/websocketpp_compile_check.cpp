#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
int main() {
  websocketpp::server<websocketpp::config::asio> s;
  s.init_asio();
  s.listen(18082);
  s.start_accept();
}
