#include "testing.h"

#include "relay_common.h"

int TestServerSmokeTest() {
  int sock1, sock2;
  // Connect socket 1
  // Send empty key
  // Receive key back
  // Connect socket 2
  // Send key

  // Send 1
  // read 2
  // Send 2
  // read 1

  // Close 1
  // 2 should be ended (closed)
  // close 2
  return 0;
}

int main(void) {
  run_test(TestServerSmokeTest, "TestServerSmokeTest");
  return 0;
}
