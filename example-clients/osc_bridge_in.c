//#define HAS_JACK_METADATA_API
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include "meta/jackey.h"
#include "meta/jack_osc.h"
#include <lo/lo.h>

//tb/140112/140421/140509/140512
//receive osc messages from any osc sender and feed it into the jack ecosystem as osc events
//trying out new (as of LAC2014) jack osc port type, metadata api
//jack1
//gcc -o jack_osc_bridge_in osc_bridge_in.c -DHAS_JACK_METADATA_API `pkg-config --libs jack liblo`
//jack2
//gcc -o jack_osc_bridge_in osc_bridge_in.c `pkg-config --libs jack liblo`

//urls of interest:
//http://jackaudio.org/metadata
//https://github.com/drobilla/jackey
//https://github.com/ventosus/jack_osc

jack_client_t *client;
jack_port_t *port_out;
char const default_name[] = "osc_bridge_in";
char const * client_name;

void* buffer_out;

jack_ringbuffer_t *rb;
lo_server_thread lo_st;

char * default_port="3344";

#ifdef HAS_JACK_METADATA_API
jack_uuid_t osc_port_uuid;
#endif

void error(int num, const char *msg, const char *path)
{
	fprintf(stderr,"liblo server error %d: %s\n", num, msg);
	if(num==9904)
	{
		fprintf(stderr,"try starting with another port:\njack_osc_bridge_in <alternative port>\n");
	}
#ifdef HAS_JACK_METADATA_API
	jack_remove_property(client, osc_port_uuid, JACKEY_EVENT_TYPES);
#endif
	exit(1);
}

int default_msg_handler(const char *path, const char *types, lo_arg **argv, int argc,
	void *data, void *user_data)
{
	//get size of message incl. path
	size_t size;
	void* msg_ptr=lo_message_serialise(data,path,NULL,&size);

	size_t can_write=jack_ringbuffer_write_space(rb);
	//printf("size %lu (+%lu bytes delim) can write %lu\n",size,sizeof(size_t),can_write);

	jack_nframes_t frames_since_cycle_start=jack_frames_since_cycle_start(client);

	//printf("arrival at sample (since cycle start): %d\n",frames_since_cycle_start);

	if(size+sizeof(jack_nframes_t)+sizeof(size_t)<=can_write)
	{
		//write position in cycle as samples since start of cycle
		int cnt=jack_ringbuffer_write(rb, (char*) &frames_since_cycle_start, sizeof(jack_nframes_t));
		//write size of message
		cnt=jack_ringbuffer_write(rb, (char*) &size, sizeof(size_t));
		//write message
		cnt+=jack_ringbuffer_write(rb, (void *) msg_ptr, size );
		//printf("%i\n",cnt);
		printf("+");
	}
	else
	{
		printf("ringbuffer full, can't write! message is lost\n");
		return 1;
	}

	free(msg_ptr);

	return 0;
}

static void signal_handler(int sig)
{
#ifdef HAS_JACK_METADATA_API
	jack_remove_property(client, osc_port_uuid, JACKEY_EVENT_TYPES);
#endif
	jack_client_close(client);
	printf("signal received, exiting ...\n");
	exit(0);
}

static int process(jack_nframes_t frames, void *arg)
{
	buffer_out = jack_port_get_buffer(port_out, frames);
	assert (buffer_out);

	jack_osc_clear_buffer(buffer_out);

	while(jack_ringbuffer_read_space(rb)>sizeof(jack_nframes_t)+sizeof(size_t))
	{	
		//size_t can_read = jack_ringbuffer_read_space(rb);
		//printf("can read %lu\n",can_read);

		jack_nframes_t pos;
		jack_ringbuffer_read (rb, (char*) &pos, sizeof(jack_nframes_t));

		size_t msg_size;
		jack_ringbuffer_read (rb, (char*) &msg_size, sizeof(size_t));
		//printf("msg_size %lu\n",msg_size);

		void* buffer = malloc(msg_size);

		//read message from ringbuffer to msg_buffer
		jack_ringbuffer_read (rb, (void *)buffer, msg_size);
/*
		char* path=lo_get_path(buffer,msg_size);
		printf("path %s\n",path);

		lo_message m=lo_message_deserialise(buffer,msg_size,NULL);
		lo_arg** args=lo_message_get_argv(m);
		int argc=lo_message_get_argc(m);
		printf("argc %d\n",argc);
		printf("arg 0 %d\n",args[0]->i);
		printf("arg 1 %s\n",&args[1]->s);
		lo_message_free(m);
*/

		if(msg_size <= jack_osc_max_event_size(buffer_out))
		{
			//write osc message to output buffer
			jack_osc_event_write(buffer_out,pos,buffer,msg_size);
			printf("-");
		}
		else
		{
			fprintf(stderr,"available jack osc buffer size was too small! message lost\n");
		}
		free(buffer);
	}

	return 0;
}

int main(int argc, char* argv[])
{
	char cn[64];
	strcpy(cn,default_name);
	strcat(cn,"_");

	if(argc>1)
	{
		strcat(cn,argv[1]);
	}
	else
	{
		strcat(cn,default_port);
	}

	client_name=cn;

	client = jack_client_open (client_name, JackNullOption, NULL);
	if (client == NULL) 
	{
		fprintf (stderr, "could not create JACK client\n");
		return 1;
	}

	jack_set_process_callback (client, process, 0);

	port_out = jack_port_register (client, "out", JACK_DEFAULT_OSC_TYPE, JackPortIsOutput, 0);

	if (port_out == NULL) 
	{
		fprintf (stderr, "could not register port\n");
		return 1;
	}
	else
	{
		printf ("registered JACK port\n");

	}

#ifdef HAS_JACK_METADATA_API
	osc_port_uuid = jack_port_uuid(port_out);
	jack_set_property(client, osc_port_uuid, JACKEY_EVENT_TYPES, JACK_EVENT_TYPE__OSC, NULL);
#endif

	/* install a signal handler to properly quits jack client */
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	rb=jack_ringbuffer_create (100000);

	if(argc>1)
	{
		printf("trying to start osc server on port: %s\n",argv[1]);
		lo_st = lo_server_thread_new(argv[1], error);
	}
	else
	{
		printf("trying to start osc server on port: %s\n",default_port);
		lo_st = lo_server_thread_new(default_port, error);
	}

	//match any path, any types
	lo_server_thread_add_method(lo_st, NULL, NULL, default_msg_handler, NULL);
	lo_server_thread_start(lo_st);

	if (jack_activate(client))
	{
		fprintf (stderr, "cannot activate client");
		return 1;
	}

	printf("ready\n");

	/* run until interrupted */
	while (1) 
	{
		sleep(1);
	};

	jack_client_close(client);
	return 0;
}
