#include <synchronicity/syn_parsing.h>

#include <vlc_common.h>

// 8 characters for command, then data

#define TYPE_LENGTH 8
#define COMMAND_LENGTH ((int)(TYPE_LENGTH+sizeof(vlc_value_t)+MESSAGE_LENGTH))

const char* SynCommandTypeNames[] = {
  "play    ",
  "pause   ",
  "seek    ",
  "beat    ",
  "mynameis",
  "error   "
};

SynCommand CommandFromString(char* buffer, int length) {
  SynCommand return_value;
  return_value.type = SYNCOMMAND_ERROR;
  if (length < COMMAND_LENGTH) {
    return return_value;
  }
  char* next_token = buffer;
  int i;
  for(i = SYNCOMMAND_PLAY; i < SYNCOMMAND_NUM; ++i) {
    if(0 == strncmp(next_token, SynCommandTypeNames[i], TYPE_LENGTH)) {
      return_value.type = i;
      next_token += TYPE_LENGTH;
      break;
    }
  }
  if (SYNCOMMAND_ERROR == return_value.type) {
    return return_value;
  }

  memcpy(&return_value.data, next_token, sizeof(return_value.data));
  next_token += sizeof(return_value.data);
  
  if (i == SYNCOMMAND_MYNAMEIS) {
    memcpy(&return_value.message, next_token, sizeof(return_value.message));
  }
  next_token += sizeof(return_value.message);

  if (COMMAND_LENGTH != (next_token - buffer) ||
      buffer + length < next_token) {
    return_value.type = SYNCOMMAND_ERROR;
  }
  return return_value;
}

int StringFromCommand(SynCommand command, char* outbuffer, int length) {
  if (length < COMMAND_LENGTH) {
    return -2;
  }
  int num_bytes;
  switch(command.type) {
    case SYNCOMMAND_PLAY:
    case SYNCOMMAND_PAUSE:
    case SYNCOMMAND_SEEK:
    case SYNCOMMAND_MYNAMEIS:
    case SYNCOMMAND_BEAT:
      num_bytes = snprintf(outbuffer, length,
          "%s", SynCommandTypeNames[command.type]);
      if(TYPE_LENGTH != num_bytes) {
        return -1;
      }
      memcpy(outbuffer + TYPE_LENGTH, &command.data, sizeof(command.data));

      break;

    default:
      // error
      return -1;
      break;
  }
  if (command.type == SYNCOMMAND_MYNAMEIS) {
    memcpy(outbuffer + TYPE_LENGTH + sizeof(command.data), &command.message, sizeof(command.message));
  }
  return COMMAND_LENGTH;
}

