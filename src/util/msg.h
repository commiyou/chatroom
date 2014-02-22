#ifndef SRC_UTIL_MSG_H_
#define SRC_UTIL_MSG_H_

#include <netinet/in.h>

#define MSG_TYPE_LEN
#define MSG_TIME_MAX_LEN (sizeof("1111-11-11 11:11:11") - 1)
#define MSG_USERNAME_MAX_LEN 20
#define MSG_BODY_MAX_LEN  1000

/**
 * client连接时向server发送MSG_CONNECT，后跟用户名，以'\n'结束
 * 比如 MSG_CONNECT+"commi\n"
 **/
#define MSG_CONNECT ((char)1)

/**
 * client与用户私聊时发送MSG_SPECIFY，后跟用户名+'\n',再接消息长度+具体消息
 * 比如 MSG_SPECIFY+"commi\n"+5+"hello"
 **/
#define MSG_SPECIFY ((char)2)

/**
 * client群聊时向server发送MSG_ALL，后跟消息长度+具体消息
 * 比如 MSG_ALL+5+"hello"
 **/
#define MSG_ALL ((char)3)

struct msg_t {
  char msg_type;
  char msg_username[MSG_USERNAME_MAX_LEN + 1];
  char msg_body[MSG_BODY_MAX_LEN +1];
  int msg_body_len;
};

#endif // SRC_UTIL_MSG_H_
