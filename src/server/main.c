#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include "../util/msg.h"

#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>

#include <err.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>


// void echo_buffer(struct evbuffer* buffer) {
//   int len = evbuffer_get_length(buffer);
//   struct evbuffer_iovec ei;
//   char msg[100];
//   ei.iov_base = msg;
//   ei.iov_len = len;
//   evbuffer_peek(buffer, -1, NULL, &ei, 1);
//   printf("[debug] buffer len %d, data \"",ei.iov_len);
//   for(len = 0;len<ei.iov_len;len++){
//     printf("%c",msg[len]);
//   }
//   printf("\"\n");
// }

struct client {
  struct bufferevent *bev;      // client的bufferevent
  char username[MSG_USERNAME_MAX_LEN + 1]; // client端用户名
  char addr_info[INET_ADDRSTRLEN];  // client端地址
  TAILQ_ENTRY(client) entries;  // 存放所有client的双链表
};

TAILQ_HEAD(, client) client_tailq_head; //存放所有client的双链表表头
pthread_rwlock_t client_lock = PTHREAD_RWLOCK_INITIALIZER;  // client_tailq_head的读写缩

struct evbuffer *msg_buffer;  // 按时间顺序存放所有群聊消息的evbuffer
pthread_mutex_t msg_lock = PTHREAD_MUTEX_INITIALIZER; // msg_buffer的胡册变量
pthread_cond_t msg_cond = PTHREAD_COND_INITIALIZER; // msg_buffer上的条件变量，若msg_buffer有消息，通知
                                                    // 群聊消息分发线程

/* 对每个client连接可读时的回调函数 */
static void
client_read_cb(struct bufferevent *bev, void *arg)
{
  struct client *this_client = (struct client *)arg;
  char data[120];
  char msg_type;
  int msg_title_len;
  int msg_len;
  char *username;
  size_t username_len;
  int total_len;
  struct tm *curr_tm;
  time_t curr_time;
  //struct evbuffer_iovec ei;
  /* 取得当前系统时间 */
  curr_time = time(NULL);
  curr_tm = localtime(&curr_time);

 	struct evbuffer *input = bufferevent_get_input(bev);

  /* 解析in消息 */
  /* 1.判断in消息类型 */
  evbuffer_remove(input, &msg_type, sizeof(char));
  printf("[debug] rcv msg type %d\n",msg_type);
  if (msg_type == MSG_ALL) {  /* 为群聊消息 */
    /* 2. 取得in消息长度 */
    evbuffer_remove(input, &msg_len, sizeof(int));
    msg_len  = ntohl(msg_len);
    if (msg_len > MSG_BODY_MAX_LEN ) {
      /* in消息长度不合法，清空input */
      evbuffer_drain(input, evbuffer_get_length(input));
      return;
    }
    //printf("[debug] buff len %d, msg len %d \n",evbuffer_get_length(input), msg_len);
    /* 求得out消息头 */
    msg_title_len = snprintf(data, sizeof(data), "%s@%s %d-%d-%d %d:%d:%d\n",
            this_client->username,
            this_client->addr_info,
            curr_tm->tm_year+1900,
            curr_tm->tm_mon,
            curr_tm->tm_mday,
            curr_tm->tm_hour,
            curr_tm->tm_min,
            curr_tm->tm_sec);
    //printf("[debug] rcv msg from %s, len %d\n",data,msg_len);
    /* 求得out消息总长度 */
    total_len = msg_title_len+msg_len;
    pthread_mutex_lock(&msg_lock);
    /* 3.将out消息长度加入msg_buffer */
    evbuffer_add(msg_buffer, &total_len, sizeof(int));
    /* 4.将out消息头加入msg_buffer */
    evbuffer_add(msg_buffer, &data, msg_title_len);
    /* 5.将input中的消息体移到msg_buffer */
    evbuffer_remove_buffer(input, msg_buffer, msg_len);
    /* 6.通知群聊消息分发线程 */
    pthread_cond_signal(&msg_cond);
    pthread_mutex_unlock(&msg_lock);
  }
  else if (msg_type == MSG_CONNECT) {  // 如果是连接请求
    /* 2.取得用户名 */
    username = evbuffer_readln(input,&username_len,EVBUFFER_EOL_CRLF);
    strncpy(this_client->username, username, MSG_USERNAME_MAX_LEN);
    this_client->username[MSG_USERNAME_MAX_LEN] = 0;
    printf("user %s@%s has joined the chatroom\n",
           this_client->username,
           this_client->addr_info);
    total_len = snprintf(data,sizeof(data),
             "User %s@%s has joined the chatroom.\n",
             this_client->username,
             this_client->addr_info);
    pthread_mutex_lock(&msg_lock);
    /* 3.将用户上线消息长度加入msg_buffer */
    evbuffer_add(msg_buffer, &total_len, sizeof(int));
    /* 4.将用户上线消息加入msg_buffer */
    evbuffer_add(msg_buffer, data, total_len);
    /* 5.通知群聊消息分发线程 */
    pthread_cond_signal(&msg_cond);
    pthread_mutex_unlock(&msg_lock);
    free(username);

  }
  else {  // 如果是私聊
    //TODO
  }
}

/* 每个client连接的事件回调函数 */
static void
client_event_cb(struct bufferevent *bev, short what, void *arg)
{
  struct client *this_client = (struct client *)arg;
  char data[120];
  int len;
  if (what & BEV_EVENT_EOF) {
    /* Client disconnected, remove the read event and the
     * free the client structure. */
    printf("user %s@%s has left the chatroom.\n",this_client->username,this_client->addr_info);

  }
  else {
    warn("Client socket error, disconnecting.\n");
  }

  /* Remove the client from the tailq. */
  pthread_rwlock_wrlock(&client_lock);
  TAILQ_REMOVE(&client_tailq_head, this_client, entries);
  pthread_rwlock_unlock(&client_lock);

  /* 将client下线消息加入msg_buffer */
  len = snprintf(data,sizeof(data),
           "User %s@%s has left the chatroom.\n",
           this_client->username,
           this_client->addr_info);
  pthread_mutex_lock(&msg_lock);
  /* 将用户下线消息加入msg_buffer */
  evbuffer_add(msg_buffer, &len, sizeof(int));
  evbuffer_add(msg_buffer, data, len);
  /* 通知群聊消息分发线程 */
  pthread_cond_signal(&msg_cond);
  pthread_mutex_unlock(&msg_lock);

  bufferevent_free(this_client->bev);
  evutil_closesocket(bufferevent_getfd(this_client->bev));
  free(this_client);

}
/* 连接监听器的回调函数 */
static void
accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd,
		struct sockaddr *address, int socklen, void *ctx)
{
	struct event_base *base = evconnlistener_get_base(listener);
	struct bufferevent *bev;
  struct client *client;

  bev = bufferevent_socket_new(base, fd, 0);
  if (bev == NULL) {
    err(-1, "bufferevent_socket_new in accept_conn_cb wrong");
  }

  client = (struct client *)calloc(1,sizeof(struct client));
  if (client == NULL) {
    err(-1, "calloc client in accept_conn_cb failed");
  }

	bufferevent_setcb(bev, client_read_cb, NULL, client_event_cb, client);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
  client->bev = bev;

  evutil_inet_ntop(address->sa_family, address->sa_data, client->addr_info, sizeof(client->addr_info));

  /* Add the new client to the tailq. */
  pthread_rwlock_wrlock(&client_lock);
  TAILQ_INSERT_TAIL(&client_tailq_head, client, entries);
  pthread_rwlock_unlock(&client_lock);

}

/* 连接监听器错误回调函数 */
static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
	struct event_base *base = evconnlistener_get_base(listener);
	int err = EVUTIL_SOCKET_ERROR();
	fprintf(stderr, "Got an error %d (%s) on the listener."
			"shutting down.\n",err, evutil_socket_error_to_string(err));
	event_base_loopexit(base, NULL);
}

/* 向所有client分发群聊消息的线程函数 */
static void *msg_dispatch(void *arg) {
  char msg [MSG_USERNAME_MAX_LEN + 1 +INET_ADDRSTRLEN + 1+ MSG_TIME_MAX_LEN + 1 + MSG_BODY_MAX_LEN];
  int total_len;
  struct client *client = NULL;
  struct evbuffer *evb = NULL;
  while(1) {
    /* 判断msg_buffer是否空 */
    pthread_mutex_lock(&msg_lock);
    while (evbuffer_get_length(msg_buffer) == 0) { // 若为空则阻塞等待
      pthread_cond_wait(&msg_cond, &msg_lock);
    }
    /* 从msg_buffer中移除一条消息到msg中 */
    evbuffer_remove(msg_buffer, &total_len, sizeof(int));
    evbuffer_remove(msg_buffer, msg, total_len);
    pthread_mutex_unlock(&msg_lock);

    printf("[debug] msg send:%s len %d\n",msg, total_len);
    fflush(NULL);

    /* 向所有client群发消息 */
    pthread_rwlock_rdlock(&client_lock);
    TAILQ_FOREACH(client, &client_tailq_head, entries) {
      evb = bufferevent_get_output(client->bev);
      evbuffer_add(evb, msg, total_len);
    }
    pthread_rwlock_unlock(&client_lock);
  }
}

int
main(int argc, char **argv)
{
	struct event_base *base;
	struct evconnlistener *listener;
	struct sockaddr_in sin;
	int port = 9875;
  pthread_t tid;

	if (argc >1 ){
		port = atoi(argv[1]);
	}
	if (port<=0 || port> 65535) {
		err(-1, "Invalid port");
	}
	base  = event_base_new();
	if (!base) {
		err(-1, "Couldn't open event base");
	}

	/* Initialize the tailq. */
  TAILQ_INIT(&client_tailq_head);
  /* msg buffer */
  msg_buffer = evbuffer_new();
  evbuffer_enable_locking(msg_buffer, NULL);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);

	listener =  evconnlistener_new_bind(base, accept_conn_cb,NULL,
			LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
			(struct sockaddr*)&sin, sizeof(sin));
	if(!listener){
		err(-1, "Couldn't create listener");
	}
	evconnlistener_set_error_cb(listener, accept_error_cb);

  if(0 != pthread_create(&tid, NULL, msg_dispatch, NULL)) {
    printf("error when create pthread,%d\n", errno);
    return 1;
  }

	event_base_dispatch(base);
	return 0;
}
