#include <synchronicity/syn_key.h>
#include "synchronicity/syn_key_internal.h"

#include "libvlc.h"
#include <ctype.h>

int SynConnection_IsAddrValid(const char* string) {
  if(0 == string) {
    return 0;
  }

  if(SYN_KEY_BUFFER_LENGTH != strlen(string)) {
    return 0;
  }
  int i;
  int valid = 1;
  int has_non_zero = 0;
  for(i = 0; i < 10; ++i) {
    if(isdigit(string[i]) || ('a' <= string[i] && string[i] <= 'f')) {
      if(!has_non_zero && '0' != string[i]) {
        has_non_zero = 1;
      }
    } else {
      valid = 0;
      break;
    }
  }

  return valid && has_non_zero;
}

void uint64_to_char(uint64_t key, char* string) {
	memset(string, '0', SYN_KEY_BUFFER_LENGTH + 1);
  string[SYN_KEY_BUFFER_LENGTH] = '\0';
  for(int i = SYN_KEY_BUFFER_LENGTH - 1; i >= 0; --i) {
    int digit = key % 16;
    key = (key >> 4);
    if(digit < 10) {
      string[i] = '0' + digit;
    } else {
      string[i] = 'a' + digit - 10;
    }
  }
}

int char_to_uint64(uint64_t* key, const char* string) {
  uint64_t value = 0;
  for(int i = 0; i < SYN_KEY_BUFFER_LENGTH; ++i) {
    value = (value << 4);
    if('0' <= string[i] && string[i] <= '9') {
      value += string[i] - '0';
    } else if('a' <= string[i] && string[i] <= 'f') {
      value += string[i] - 'a' + 10;
    } else {
      return -1;
    }
  }
  *key = value;
  return 0;
}
