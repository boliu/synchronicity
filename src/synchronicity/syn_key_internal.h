#ifndef SYN_KEY_INTERNAL_H_
#define SYN_KEY_INTERNAL_H_

#define SYN_ADDRESS_SIZE 8
#define SYN_KEY_BUFFER_LENGTH 16

struct SynAddress {
  uint64_t relay_server_key;
};
typedef struct SynAddress SynAddress;

void uint64_to_char(uint64_t key, char* string);
int char_to_uint64(uint64_t* key, const char* string);

#endif
