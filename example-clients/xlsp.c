/*
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

//origin file jackd2-1.9.8~dfsg.1/example-clients/lsp.c
//(jack_lsp binary in jackd packages)

//output info as xml, parse anything
//gcc -o jack_xlsp xlsp.c `pkg-config --libs jack`

//tb/130626
//131020 gist->git

#include <stdio.h>
#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <jack/jack.h>

char * my_name;

static void
show_version (void)
{
	fprintf(stderr,"131020\n");
}

static void
show_usage (void)
{
	//show_version ();
	fprintf (stderr, "\nUsage: %s [options] [filter string]\n", my_name);
	fprintf (stderr, "Dump active Jack ports, and optionally display extra information as XML.\n");
	fprintf (stderr, "Optionally filter ports which match ALL strings provided after any options.\n\n");
	fprintf (stderr, "Display options:\n");
	fprintf (stderr, "        -s, --server <name>   Connect to the jack server named <name>\n");
	fprintf (stderr, "        -A, --aliases         List aliases for each port\n");
	fprintf (stderr, "        -c, --connections     List connections to/from each port\n");
	fprintf (stderr, "        -l, --latency         Display per-port latency in frames at each port\n");
	fprintf (stderr, "        -L, --latency         Display total latency in frames at each port\n");
	fprintf (stderr, "        -p, --properties      Display port properties. Output may include:\n"
			 "                              input|output, can-monitor, physical, terminal\n\n");
	fprintf (stderr, "        -t, --type            Display port type\n");
	fprintf (stderr, "        -h, --help            Display this help message\n");
	fprintf (stderr, "        --version             Output version information and exit\n\n");
	fprintf (stderr, "For more information see http://jackaudio.org/\n");
	fprintf (stderr, "This is a modified version of <jackd2 source>/example-clients/lsp.c (jack_lsp)\n\n");
	fprintf (stderr, "Examples:\n");
	fprintf (stderr, "  jack_xlsp\n");
	fprintf (stderr, "  jack_xlsp -AclLpt | xmlstarlet fo\n");
	fprintf (stderr, "  | xmlstarlet sel -t -m '//port[position()=2]' -v @name -nl\n");
	fprintf (stderr, "  | xmlstarlet sel -t -m '//port/properties[@physical=\"1\"]' -c '../.' -nl\n");
	fprintf (stderr, "  | xmlstarlet sel -t -m '//port[starts-with(@name,\"fire\") and playback_latency_frames/@min>0]' -v 'alias' -o ': ' -v 'playback_latency_frames/@min' -nl\n");
	fprintf (stderr, "  | xmlstarlet sel -t -m '//port' -s D:T:L 'count(.//connection)' --if  'position()<10' -v '@name' -o ': ' -v 'count(.//connection)' -nl --else -b\n");

	fprintf (stderr, "\n");
	fprintf (stderr, "jack_xlsp source at https://github.com/7890/jack_tools\n\n");

	//jack_xlsp -AclLpt | xmlstarlet sel -t -m '//port' -s D:T:L 'count(.//connection)' --if  'position()<10' -o '==[' -v '@name' -o ': ' -v 'count(.//connection)' -nl -m ".//connection" -o '  |__' -v . -nl  -b -nl --else -b

}

int
main (int argc, char *argv[])
{
	jack_client_t *client;
	jack_status_t status;
	jack_options_t options = JackNoStartServer;
	const char **ports, **connections;
	unsigned int i, j, k;
	int skip_port;
	int show_aliases = 0;
	int show_con = 0;
	int show_port_latency = 0;
	int show_total_latency = 0;
	int show_properties = 0;
	int show_type = 0;
	int c;
	int option_index;
	char* aliases[2];
	char *server_name = NULL;
	jack_port_t *port;

	struct option long_options[] = {
	    { "server", 1, 0, 's' },
		{ "aliases", 0, 0, 'A' },
		{ "connections", 0, 0, 'c' },
		{ "port-latency", 0, 0, 'l' },
		{ "total-latency", 0, 0, 'L' },
		{ "properties", 0, 0, 'p' },
		{ "type", 0, 0, 't' },
		{ "help", 0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	my_name = strrchr(argv[0], '/');
	if (my_name == 0) {
		my_name = argv[0];
	} else {
		my_name ++;
	}

	while ((c = getopt_long (argc, argv, "s:AclLphvt", long_options, &option_index)) >= 0) {
		switch (c) {
		case 's':
            server_name = (char *) malloc (sizeof (char) * strlen(optarg));
            strcpy (server_name, optarg);
            options |= JackServerName;
            break;
		case 'A':
			aliases[0] = (char *) malloc (jack_port_name_size());
			aliases[1] = (char *) malloc (jack_port_name_size());
			show_aliases = 1;
			break;
		case 'c':
			show_con = 1;
			break;
		case 'l':
			show_port_latency = 1;
			break;
		case 'L':
			show_total_latency = 1;
			break;
		case 'p':
			show_properties = 1;
			break;
		case 't':
			show_type = 1;
			break;
		case 'h':
			show_usage ();
			return 1;
			break;
		case 'v':
			show_version ();
			return 1;
			break;
		default:
			show_usage ();
			return 1;
			break;
		}
	}

	/* Open a client connection to the JACK server.  Starting a
	 * new server only to list its ports seems pointless, so we
	 * specify JackNoStartServer. */
	//JOQ: need a new server name option

	client = jack_client_open ("xlsp", options, &status, server_name);
	if (client == NULL) {
		if (status & JackServerFailed) {
			fprintf (stderr, "JACK server not running\n");
		} else {
			fprintf (stderr, "jack_client_open() failed, "
				 "status = 0x%2.0x\n", status);
		}
		return 1;
	}

	ports = jack_get_ports (client, NULL, NULL, 0);
    if (!ports)
        goto error;

	printf("<jack_xlsp>\n");


	for (i = 0; ports && ports[i]; ++i) {


		// skip over any that don't match ALL of the strings presented at command line
		skip_port = 0;
		for(k = optind; k < argc; k++){
			if(strstr(ports[i], argv[k]) == NULL ){
				skip_port = 1;
			}
		}
		if (skip_port) continue;

		printf ("<port index=\"%d\" name=\"%s\">\n", (i+1), ports[i]);
		port = jack_port_by_name (client, ports[i]);

		if (show_aliases) {
			int cnt;
			int i;

			cnt = jack_port_get_aliases (port, aliases);
			for (i = 0; i < cnt; ++i) {
				printf ("<alias>%s</alias>\n", aliases[i]);
			}
		}

		if (show_properties) {
			if (port) {
				int flags = jack_port_flags (port);
				printf ("<properties ");
				if (flags & JackPortIsInput) {
					fputs ("input=\"1\" ", stdout);
				}
				else {
					fputs ("input=\"0\" ", stdout);
				}
				if (flags & JackPortIsOutput) {
					fputs ("output=\"1\" ", stdout);
				}
				else {
					fputs ("output=\"0\" ", stdout);
				}
				if (flags & JackPortCanMonitor) {
					fputs ("can_monitor=\"1\" ", stdout);
				}
				else {
					fputs ("can_monitor=\"0\" ", stdout);
				}
				if (flags & JackPortIsPhysical) {
					fputs ("physical=\"1\" ", stdout);
				}
				else {
					fputs ("physical=\"0\" ", stdout);
				}
				if (flags & JackPortIsTerminal) {
					fputs ("terminal=\"1\" ", stdout);
				}
				else {
					fputs ("terminal=\"0\"", stdout);
				}
				//end properties
				printf ("/>\n");
			}
		}
		if (show_type) {
			if (port) {
				//putc ('\t', stdout);
				//fputs (jack_port_type (port), stdout);
				//putc ('\n', stdout);
				printf("<description>%s</description>\n",jack_port_type (port));
			}
		}
		if (show_con) {
			printf("<connections>\n");

			if ((connections = jack_port_get_all_connections (client, jack_port_by_name(client, ports[i]))) != 0) {
				for (j = 0; connections[j]; j++) {
					printf ("<connection index=\"%d\">%s</connection>\n", (j+1), connections[j]);
				}
				free (connections);
			}
			printf("</connections>\n");
		}
		if (show_port_latency) {
			if (port) {
				jack_latency_range_t range;
				printf ("<latency_frames value=\"%" PRIu32 "\"/>\n",
					jack_port_get_latency (port));

				jack_port_get_latency_range (port, JackPlaybackLatency, &range);
				printf ("<playback_latency_frames min=\"%" PRIu32 "\" max=\"%" PRIu32 "\"/>\n",
					range.min, range.max);

				jack_port_get_latency_range (port, JackCaptureLatency, &range);
				printf ("<capture_latency min=\"%" PRIu32 "\" max=\"%" PRIu32 "\"/>\n",
					range.min, range.max);
			}
		}
		if (show_total_latency) {
			if (port) {
				printf ("<total_latency_frames value=\"%d\"/>\n",
					jack_port_get_total_latency (client, port));
			}
		}
		printf ("</port>\n");
	}
	printf("</jack_xlsp>\n");

error:
    if (ports)
        jack_free (ports);
	jack_client_close (client);
	exit (0);

}
