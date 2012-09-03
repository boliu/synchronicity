#include <string>
#include <iostream>

typedef int TestFunction(void);

void run_test(TestFunction function, std::string test_name) {
  int result = function();
  if (result != 0) {
    std::cerr << "FAIL: " << test_name << std::endl;
  } else {
    std::cout << "PASS: " << test_name << std::endl;
  }
}
