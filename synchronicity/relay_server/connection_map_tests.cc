#include <iostream>
#include "connection_map.h"
#include <string>
#include "testing.h"



struct Data1{
  ConnectionMapKey key;
  ConnectionMapKeyBuffer buffer;
} PrintKeyData[] = {
  {0x0, {'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0'}},
  {0x1, {'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '1'}},
  {0xffffffffffffffff, {'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f'}},
  {0x123456789abcdef0, {'1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0'}}
};

bool BufferEqual(ConnectionMapKeyBuffer buffer1, ConnectionMapKeyBuffer buffer2) {
  for(unsigned int i = 0; i < KEY_BUFFER_LENGTH; ++i) {
    if (buffer1[i] != buffer2[i]) {
      return false;
    }
  }
  return true;
}

int TestSimplePrintKey(void) {
  for (unsigned int i = 0; i < sizeof(PrintKeyData) / sizeof(Data1); ++i) {
    ConnectionMapKeyBuffer buffer;
    PrintKey(PrintKeyData[i].key, buffer);
    if (!BufferEqual(buffer, PrintKeyData[i].buffer)) {
      std::cerr << "Expected: " << PrintKeyData[i].buffer <<
          " Got: " << buffer << std::endl;
      return 1;
    }
  }
  return 0;
}

int TestSimpleParse(void) {
  for (unsigned int i = 0; i < sizeof(PrintKeyData) / sizeof(Data1); ++i) {
    ConnectionMapKey key;
    int result = ParseKey(PrintKeyData[i].buffer, key);
    if (result != 0) {
      std::cerr << "Parse " << PrintKeyData[i].buffer << " failed" << std::endl;
      return 1;
    }
    if (!(PrintKeyData[i].key == key)) {
      std::cerr << PrintKeyData[i].buffer << std::endl;
      std::cerr << "Expected: " << PrintKeyData[i].key << " Got: " << key << std::endl;
      return 1;
    }
  }
  return 0;
}

int main(void) {
  run_test(TestSimplePrintKey, "TestSimplePrintKey");
  run_test(TestSimpleParse, "TestSimpleParse");
  return 0;
}
