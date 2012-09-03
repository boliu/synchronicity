#ifndef _CONNECTION_MAP_H_
#define _CONNECTION_MAP_H_

#include <stdint.h>
#include <stdlib.h>
#include <iostream>
#include <stdio.h>

#define KEY_BUFFER_LENGTH 16
#define KEY_LENGTH 8
typedef uint64_t ConnectionMapKey;
typedef char ConnectionMapKeyBuffer[KEY_BUFFER_LENGTH];

namespace {
char DigitToChar(unsigned value) {
  if (value < 10) {
    return (char)('0'+value);
  } else {
    return (char)('a'+value-10);
  }
}

}

ConnectionMapKey RandomKey() {
  ConnectionMapKey key;
  char* pointer = (char*)&key;
  FILE *urand = fopen("/dev/urandom", "r");
  if (urand != NULL) {
    for(unsigned int i = 0; i < KEY_LENGTH; ++i) {
      *pointer = (char)fgetc(urand);
      ++pointer;
    }
    fclose(urand);
  } else {
    printf("Falling back to rand\n");
    key = rand();
  }
  return key;
}

void PrintKey(ConnectionMapKey key, ConnectionMapKeyBuffer& buffer) {
  char* out_buffer = buffer;
  for(unsigned int i = 0; i < KEY_BUFFER_LENGTH; ++i) {
    buffer[i] = '-';
  }
  out_buffer += KEY_BUFFER_LENGTH;
  for(unsigned int i = 0; i < KEY_BUFFER_LENGTH; ++i) {
    out_buffer -= 1;
    unsigned int value = (unsigned int)(0x000000000000000f & key);
    key = key >> 4;
    *out_buffer = DigitToChar(value);
  }
}

int ParseKey(ConnectionMapKeyBuffer buffer, ConnectionMapKey& key) {
  key = 0;
  for(unsigned int i = 0; i < KEY_BUFFER_LENGTH; ++i) {
    if( !('0' <= buffer[i] && buffer[i] <= '9') &&
        !('a' <= buffer[i] && buffer[i] <= 'f') ) {
      return 1;
    }

    char value;
    if ('0' <= buffer[i] && buffer[i] <= '9') {
      value = buffer[i] - '0';
    } else {
      value = 10 + buffer[i] - 'a';
    }
    key = key << 4;
    key += value;
  }
  return 0;
}

std::ostream& operator<< (std::ostream& out, ConnectionMapKeyBuffer& val ) {
  for (unsigned int i = 0; i < KEY_BUFFER_LENGTH; ++i) {
    out << val[i];
  }
  return out;
}

#endif
