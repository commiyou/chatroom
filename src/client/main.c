#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include "../util/msg.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

static void
input_read_cb(evutil_socket_t fd, short what, void *bev)
{
	char buf[MSG_TYPE_LEN + MSG_USERNAME_MAX_LEN+MSG_BODY_MAX_LEN+1];
	struct evbuffer *output = bufferevent_get_output((struct bufferevent *)bev);
  char msg_type = MSG_ALL;
  int msg_len;
	if(fgets(buf, sizeof(buf), stdin)==NULL)
    exit(0);
	//printf("[debug] stdin get str %c%d$s",MSG_ALL,htonl(strlen(buf)),buf);
  msg_len = htonl(strlen(buf));
  evbuffer_add(output,&msg_type,sizeof(char));
  evbuffer_add(output,&msg_len,sizeof(int));
	evbuffer_add_printf(output,"%s",buf);
  //printf("[debug] send \"%s\" len %d,input len %d\n",buf,msg_len,evbuffer_get_length(output));
}
static void
sock_read_cb(struct bufferevent *bev, void *ptr)
{
	char buf[2000];
	int n;
	struct evbuffer *input = bufferevent_get_input(bev);
	while((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
		fwrite(buf, 1, n, stdout);
	}
}
static void
sock_event_cb(struct bufferevent *bev, short events, void *ptr)
{
	if (events & BEV_EVENT_CONNECTED)
	{
		//TODO
		printf("connect ok\n");
	} else if (events &(BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		//TODO
		printf("connect over\n client will terminated in 2s\n");
		//bufferevent_free(bev);
    sleep(2);
    exit(1);
	}
}


int
main(int argc, char **argv)
{
	int ret;
	struct event_base *base;
	struct bufferevent *bev;
	struct event *ev_stdin;
	struct sockaddr_in sin;
	evutil_socket_t sock;
	int port = 9875;

	if (3 != argc) {
		printf("Usage : %s <ipv4 address> <username>\n",argv[0]);
		return -1;
	}
	if (strlen(argv[2]) > MSG_USERNAME_MAX_LEN) {
    err(-1, "username should less than %d.\n",MSG_USERNAME_MAX_LEN);
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	ret = evutil_inet_pton(AF_INET,argv[1],(void *)&(sin.sin_addr));
	if (ret <= 0 ){
		printf("Usage : %s <ipv4 address> <username>\n",argv[0]);
		return -1;
	}

	base  = event_base_new();
  if (!base) {
		puts("Couldn't open event base");
		return -1;
	}
	bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(bev, sock_read_cb, NULL, sock_event_cb,NULL);
	bufferevent_enable(bev, EV_READ|EV_WRITE);

  evbuffer_add_printf(bufferevent_get_output(bev),"%c%s\n",MSG_CONNECT,argv[2]);

	if (bufferevent_socket_connect(bev,
				(struct sockaddr *)&sin,sizeof(sin))<0){
		bufferevent_free(bev);
		puts("Couldn't open new socket");
		return -1;
	}

	ev_stdin = event_new(base, fileno(stdin), EV_READ|EV_PERSIST,
		   	input_read_cb, bev);
	event_add(ev_stdin,NULL);
	event_base_dispatch(base);
	return 0;
}
