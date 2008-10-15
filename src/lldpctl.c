/*
 * Copyright (c) 2008 Vincent Bernat <bernat@luffy.cx>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "lldpd.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

void		 usage(void);

TAILQ_HEAD(interfaces, lldpd_interface);
TAILQ_HEAD(vlans, lldpd_vlan);

struct value_string {
	int value;
	char *string;
};

static const struct value_string operational_mau_type_values[] = {
	{ 1,	"AUI - no internal MAU, view from AUI" },
	{ 2,	"10Base5 - thick coax MAU" },
	{ 3,	"Foirl - FOIRL MAU" },
	{ 4,	"10Base2 - thin coax MAU" },
	{ 5,	"10BaseT - UTP MAU" },
	{ 6,	"10BaseFP - passive fiber MAU" },
	{ 7,	"10BaseFB - sync fiber MAU" },
	{ 8,	"10BaseFL - async fiber MAU" },
	{ 9,	"10Broad36 - broadband DTE MAU" },
	{ 10,	"10BaseTHD - UTP MAU, half duplex mode" },
	{ 11,	"10BaseTFD - UTP MAU, full duplex mode" },
	{ 12,	"10BaseFLHD - async fiber MAU, half duplex mode" },
	{ 13,	"10BaseFLDF - async fiber MAU, full duplex mode" },
	{ 14,	"10BaseT4 - 4 pair category 3 UTP" },
	{ 15,	"100BaseTXHD - 2 pair category 5 UTP, half duplex mode" },
	{ 16,	"100BaseTXFD - 2 pair category 5 UTP, full duplex mode" },
	{ 17,	"100BaseFXHD - X fiber over PMT, half duplex mode" },
	{ 18,	"100BaseFXFD - X fiber over PMT, full duplex mode" },
	{ 19,	"100BaseT2HD - 2 pair category 3 UTP, half duplex mode" },
	{ 20,	"100BaseT2DF - 2 pair category 3 UTP, full duplex mode" },
	{ 21,	"1000BaseXHD - PCS/PMA, unknown PMD, half duplex mode" },
	{ 22,	"1000BaseXFD - PCS/PMA, unknown PMD, full duplex mode" },
	{ 23,	"1000BaseLXHD - Fiber over long-wavelength laser, half duplex mode" },
	{ 24,	"1000BaseLXFD - Fiber over long-wavelength laser, full duplex mode" },
	{ 25,	"1000BaseSXHD - Fiber over short-wavelength laser, half duplex mode" },
	{ 26,	"1000BaseSXFD - Fiber over short-wavelength laser, full duplex mode" },
	{ 27,	"1000BaseCXHD - Copper over 150-Ohm balanced cable, half duplex mode" },
	{ 28,	"1000BaseCXFD - Copper over 150-Ohm balanced cable, full duplex mode" },
	{ 29,	"1000BaseTHD - Four-pair Category 5 UTP, half duplex mode" },
	{ 30,	"1000BaseTFD - Four-pair Category 5 UTP, full duplex mode" },
	{ 31,	"10GigBaseX - X PCS/PMA, unknown PMD." },
	{ 32,	"10GigBaseLX4 - X fiber over WWDM optics" },
	{ 33,	"10GigBaseR - R PCS/PMA, unknown PMD." },
	{ 34,	"10GigBaseER - R fiber over 1550 nm optics" },
	{ 35,	"10GigBaseLR - R fiber over 1310 nm optics" },
	{ 36,	"10GigBaseSR - R fiber over 850 nm optics" },
	{ 37,	"10GigBaseW - W PCS/PMA, unknown PMD." },
	{ 38,	"10GigBaseEW - W fiber over 1550 nm optics" },
	{ 39,	"10GigBaseLW - W fiber over 1310 nm optics" },
	{ 40,	"10GigBaseSW - W fiber over 850 nm optics" },
	{ 0, NULL }
};

void
usage(void)
{
	extern const char	*__progname;

	fprintf(stderr, "usage: %s [-d]\n", __progname);
	exit(1);
}

static char*
dump(void *data, int size, int max, char sep)
{
	int			 i;
	size_t			 len;
	static char		*buffer = NULL;
	static char		 truncation[] = "[...]";

	free(buffer);
	if (size > max)
		len = max * 3 + sizeof(truncation) + 1;
	else
		len = size * 3;

	if ((buffer = (char *)malloc(len)) == NULL)
		fatal(NULL);

	for (i = 0; (i < size) && (i < max); i++)
		sprintf(buffer + i * 3, "%02x%c", *(u_int8_t*)(data + i), sep);
	if (size > max)
		sprintf(buffer + i * 3, "%s", truncation);
	else
		*(buffer + i*3 - 1) = 0;
	return buffer;
}


void
get_interfaces(int s, struct interfaces *ifs)
{
	void *p;
	struct hmsg *h;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_INTERFACES);
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_interfaces: unable to send request");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_interfaces: unable to receive answer");
	if (h->hdr.type != HMSG_GET_INTERFACES)
		fatalx("get_interfaces: unknown answer type received");
	p = &h->data;
	if (ctl_msg_unpack_list(STRUCT_LLDPD_INTERFACE,
		ifs, sizeof(struct lldpd_interface), h, &p) == -1)
		fatalx("get_interfaces: unable to retrieve the list of interfaces");
}

int
get_vlans(int s, struct vlans *vls, char *interface)
{
	void *p;
	struct hmsg *h;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_VLANS);
	h->hdr.len += strlcpy((char *)&h->data, interface,
	    MAX_HMSGSIZE - sizeof(struct hmsg_hdr)) + 1;
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_vlans: unable to send request");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_vlans: unable to receive answer");
	if (h->hdr.type != HMSG_GET_VLANS)
		fatalx("get_vlans: unknown answer type received");
	p = &h->data;
	if (ctl_msg_unpack_list(STRUCT_LLDPD_VLAN,
		vls, sizeof(struct lldpd_vlan), h, &p) == -1)
		fatalx("get_vlans: unable to retrieve the list of vlans");
	return 1;
}

int
get_chassis(int s, struct lldpd_chassis *chassis, char *interface)
{
	struct hmsg *h;
	void *p;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_CHASSIS);
	h->hdr.len += strlcpy((char *)&h->data, interface,
	    MAX_HMSGSIZE - sizeof(struct hmsg_hdr)) + 1;
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_chassis: unable to send request to get chassis");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_chassis: unable to receive answer to get chassis");
	if (h->hdr.type == HMSG_NONE)
		/* No chassis */
		return -1;
	p = &h->data;
	if (ctl_msg_unpack_structure(STRUCT_LLDPD_CHASSIS,
		chassis, sizeof(struct lldpd_chassis), h, &p) == -1) {
		LLOG_WARNX("unable to retrieve chassis for %s", interface);
		fatalx("get_chassis: abort");
	}
	return 1;
}

int
get_port(int s, struct lldpd_port *port, char *interface)
{
	struct hmsg *h;
	void *p;

	if ((h = (struct hmsg *)malloc(MAX_HMSGSIZE)) == NULL)
		fatal(NULL);
	ctl_msg_init(h, HMSG_GET_PORT);
	h->hdr.len += strlcpy((char *)&h->data, interface,
	    MAX_HMSGSIZE - sizeof(struct hmsg_hdr)) + 1;
	if (ctl_msg_send(s, h) == -1)
		fatalx("get_port: unable to send request to get port");
	if (ctl_msg_recv(s, h) == -1)
		fatalx("get_port: unable to receive answer to get port");
	if (h->hdr.type == HMSG_NONE)
		/* No port */
		return -1;
	p = &h->data;
	if (ctl_msg_unpack_structure(STRUCT_LLDPD_PORT,
		port, sizeof(struct lldpd_port), h, &p) == -1) {
		LLOG_WARNX("unable to retrieve port information for %s",
		    interface);
		fatalx("get_chassis: abort");
	}
	return 1;
}

void
display_cap(struct lldpd_chassis *chassis, u_int8_t bit, char *symbol)
{
	if (chassis->c_cap_available & bit)
		printf("%s(%c) ", symbol,
		    (chassis->c_cap_enabled & bit)?'E':'d');
}

void
pretty_print(char *string)
{
	char *s = NULL;
	if (((s = index(string, '\n')) == NULL) && (strlen(string) < 60)) {
		printf("%s\n", string);
		return;
	} else
		printf("\n");
	while (s != NULL) {
		*s = '\0';
		printf("   %s\n", string);
		*s = '\n';
		string = s + 1;
		s = index(string, '\n');
	}
	printf("   %s\n", string);
}

void
display_chassis(struct lldpd_chassis *chassis)
{
	char *cid;
	if ((cid = (char *)malloc(chassis->c_id_len + 1)) == NULL)
		fatal(NULL);
	memcpy(cid, chassis->c_id, chassis->c_id_len);
	cid[chassis->c_id_len] = 0;
	switch (chassis->c_id_subtype) {
	case LLDP_CHASSISID_SUBTYPE_IFNAME:
		printf(" ChassisID: %s (ifName)\n", cid);
		break;
	case LLDP_CHASSISID_SUBTYPE_IFALIAS:
		printf(" ChassisID: %s (ifAlias)\n", cid);
		break;
	case LLDP_CHASSISID_SUBTYPE_LOCAL:
		printf(" ChassisID: %s (local)\n", cid);
		break;
	case LLDP_CHASSISID_SUBTYPE_LLADDR:
		printf(" ChassisID: %s (MAC)\n",
		    dump(chassis->c_id, chassis->c_id_len, ETH_ALEN, ':'));
		break;
	case LLDP_CHASSISID_SUBTYPE_ADDR:
		if (*(u_int8_t*)chassis->c_id == 1) {
			printf(" ChassisID: %s (IP)\n",
			    inet_ntoa(*(struct in_addr*)(chassis->c_id +
				    1)));
			break;
		}
	case LLDP_CHASSISID_SUBTYPE_PORT:
	case LLDP_CHASSISID_SUBTYPE_CHASSIS:
	default:
		printf(" ChassisID: %s (unhandled type)\n",
		    dump(chassis->c_id, chassis->c_id_len, 16, ' '));
	}
	printf(" SysName:   %s\n", chassis->c_name);
	printf(" SysDescr:  "); pretty_print(chassis->c_descr);
	printf(" MgmtIP:    %s\n", inet_ntoa(chassis->c_mgmt));
	printf(" Caps:      ");
	display_cap(chassis, LLDP_CAP_OTHER, "Other");
	display_cap(chassis, LLDP_CAP_REPEATER, "Repeater");
	display_cap(chassis, LLDP_CAP_BRIDGE, "Bridge");
	display_cap(chassis, LLDP_CAP_WLAN, "Wlan");
	display_cap(chassis, LLDP_CAP_TELEPHONE, "Tel");
	display_cap(chassis, LLDP_CAP_DOCSIS, "Docsis");
	display_cap(chassis, LLDP_CAP_STATION, "Station");
	printf("\n");
}

void
display_autoneg(struct lldpd_port *port, int bithd, int bitfd, char *desc)
{
	if (!((port->p_autoneg_advertised & bithd) ||
		(port->p_autoneg_advertised & bitfd)))
		return;
	printf("%s ", desc);
	if (port->p_autoneg_advertised & bithd) {
		printf("(HD");
		if (port->p_autoneg_advertised & bitfd) {
			printf(", FD) ");
			return;
		}
		printf(") ");
		return;
	}
	printf("(FD) ");
}

void
display_port(struct lldpd_port *port)
{
	char *pid;
	int i;

	if ((pid = (char *)malloc(port->p_id_len + 1)) == NULL)
		fatal(NULL);
	memcpy(pid, port->p_id, port->p_id_len);
	pid[port->p_id_len] = 0;
	switch (port->p_id_subtype) {
	case LLDP_PORTID_SUBTYPE_IFNAME:
		printf(" PortID:    %s (ifName)\n", pid);
		break;
	case LLDP_PORTID_SUBTYPE_IFALIAS:
		printf(" PortID:    %s (ifAlias)\n", pid);
		break;
	case LLDP_PORTID_SUBTYPE_LOCAL:
		printf(" PortID:    %s (local)\n", pid);
		break;
	case LLDP_PORTID_SUBTYPE_LLADDR:
		printf(" PortID:    %s (MAC)\n",
		    dump(port->p_id, port->p_id_len, ETH_ALEN, ':'));
		break;
	case LLDP_PORTID_SUBTYPE_ADDR:
		if (*(u_int8_t*)port->p_id == 1) {
			printf(" PortID:    %s (IP)\n",
			    inet_ntoa(*(struct in_addr*)(port->p_id +
				    1)));
			break;
		}
	case LLDP_PORTID_SUBTYPE_PORT:
	case LLDP_PORTID_SUBTYPE_AGENTCID:
	default:
		printf(" ChassisID: %s (unhandled type)\n",
		    dump(port->p_id, port->p_id_len, 16, ' '));
	}
	printf(" PortDescr: "); pretty_print(port->p_descr);
	if (port->p_aggregid)
		printf("\n   Port is aggregated. PortAggregID:  %d\n",
		    port->p_aggregid);

	printf("\n   Autoneg: %ssupported/%senabled\n",
	    port->p_autoneg_support?"":"not ",
	    port->p_autoneg_enabled?"":"not ");
	if (port->p_autoneg_enabled) {
		printf("   PMD autoneg: ");
		display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_10BASE_T,
		    LLDP_DOT3_LINK_AUTONEG_10BASET_FD,
		    "10Base-T");
		display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_100BASE_TX,
		    LLDP_DOT3_LINK_AUTONEG_100BASE_TXFD,
		    "100Base-T");
		display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_100BASE_T2,
		    LLDP_DOT3_LINK_AUTONEG_100BASE_T2FD,
		    "100Base-T2");
		display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_1000BASE_X,
		    LLDP_DOT3_LINK_AUTONEG_1000BASE_XFD,
		    "100Base-X");
		display_autoneg(port, LLDP_DOT3_LINK_AUTONEG_1000BASE_T,
		    LLDP_DOT3_LINK_AUTONEG_1000BASE_TFD,
		    "1000Base-T");
		printf("\n");
	}
	printf("   MAU oper type: ");
	for (i = 0; operational_mau_type_values[i].value != 0; i++) {
		if (operational_mau_type_values[i].value ==
		    port->p_mau_type) {
			printf("%s\n", operational_mau_type_values[i].string);
			break;
		}
	}
	if (operational_mau_type_values[i].value == 0)
		printf("unknown (%d)\n", port->p_mau_type);
}

void
display_vlans(struct lldpd_port *port)
{
	int i = 0;
	struct lldpd_vlan *vlan;
	TAILQ_FOREACH(vlan, &port->p_vlans, v_entries)
		printf("   VLAN %4d: %-20s%c", vlan->v_vid, vlan->v_name,
		    (i++ % 2) ? '\n' : ' ');
	if (i % 2)
		printf("\n");
}

int
main(int argc, char *argv[])
{
	int s;
	int ch, debug = 1;
	struct interfaces ifs;
	struct vlans vls;
	struct lldpd_interface *iff;
	struct lldpd_chassis chassis;
	struct lldpd_port port;
	char sep[80];
	
	/*
	 * Get and parse command line options
	 */
	while ((ch = getopt(argc, argv, "d")) != -1) {
		switch (ch) {
		case 'd':
			debug++;
			break;
		default:
			usage();
		}
	}
	
	log_init(debug);
	memset(sep, '-', 79);
	sep[79] = 0;
	
	if ((s = ctl_connect(LLDPD_CTL_SOCKET)) == -1)
		fatalx("unable to connect to socket " LLDPD_CTL_SOCKET);
	get_interfaces(s, &ifs);

	printf("%s\n", sep);
	printf("    LLDP neighbors\n");
	printf("%s\n", sep);	
	TAILQ_FOREACH(iff, &ifs, next) {
		if ((get_chassis(s, &chassis, iff->name) != -1) &&
		    (get_port(s, &port, iff->name) != -1)) {
			printf("Interface: %s\n", iff->name);
			display_chassis(&chassis);
			printf("\n");
			display_port(&port);
			if (get_vlans(s, &vls, iff->name) != -1) {
				memcpy(&port.p_vlans, &vls, sizeof(struct vlans));
				if (!TAILQ_EMPTY(&port.p_vlans)) {
					printf("\n");
					display_vlans(&port);
				}
			}
			printf("%s\n", sep);
		}
	}
	
	close(s);
	
	return 0;
}
