#ifndef SYN_PARSING_H_
#define SYN_PARSING_H_
#include <vlc_common.h>

#define MESSAGE_LENGTH 16

enum SynCommandType {
  SYNCOMMAND_PLAY = 0,
  SYNCOMMAND_PAUSE,
  SYNCOMMAND_SEEK,
  SYNCOMMAND_MYNAMEIS,
  SYNCOMMAND_NUM,
  SYNCOMMAND_ERROR = SYNCOMMAND_NUM,
};
typedef enum SynCommandType SynCommandType;

struct SynCommand {
  SynCommandType type;
  vlc_value_t data;
  char message[MESSAGE_LENGTH];
};
typedef struct SynCommand SynCommand;

SynCommand CommandFromString(char* buffer, int length);
// Returns number of bytes written, or negative for error
int StringFromCommand(SynCommand command, char* outbuffer, int length);
#endif
