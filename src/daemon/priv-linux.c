/* -*- mode: c; c-file-style: "openbsd" -*- */
/*
 * Copyright (c) 2008 Vincent Bernat <bernat@luffy.cx>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <regex.h>
#include <sys/ioctl.h>
#include <netpacket/packet.h> /* For sockaddr_ll */
#include <linux/filter.h>     /* For BPF filtering */
#include <linux/ethtool.h>
#include <linux/sockios.h>

/* Proxy for open */
int
priv_open(char *file)
{
	int cmd, len, rc;
	cmd = PRIV_OPEN;
	must_write(&cmd, sizeof(int));
	len = strlen(file);
	must_write(&len, sizeof(int));
	must_write(file, len + 1);
	must_read(&rc, sizeof(int));
	if (rc == -1)
		return rc;
	return receive_fd();
}

/* Proxy for ethtool ioctl */
int
priv_ethtool(char *ifname, void *ethc, size_t length)
{
	int cmd, rc, len;
	cmd = PRIV_ETHTOOL;
	must_write(&cmd, sizeof(int));
	len = strlen(ifname);
	must_write(&len, sizeof(int));
	must_write(ifname, len + 1);
	must_read(&rc, sizeof(int));
	if (rc != 0)
		return rc;
	must_read(ethc, length);
	return rc;
}

void
asroot_open()
{
	const char* authorized[] = {
		"/proc/sys/net/ipv4/ip_forward",
		"/proc/net/bonding/[^.][^/]*",
		"/proc/self/net/bonding/[^.][^/]*",
		SYSFS_CLASS_NET "[^.][^/]*/brforward",
		SYSFS_CLASS_NET "[^.][^/]*/brport",
		SYSFS_CLASS_NET "[^.][^/]*/brif/[^.][^/]*/port_no",
		SYSFS_CLASS_DMI "product_version",
		SYSFS_CLASS_DMI "product_serial",
		SYSFS_CLASS_DMI "product_name",
		SYSFS_CLASS_DMI "bios_version",
		SYSFS_CLASS_DMI "sys_vendor",
		SYSFS_CLASS_DMI "chassis_asset_tag",
		NULL
	};
	const char **f;
	char *file;
	int fd, len, rc;
	regex_t preg;

	must_read(&len, sizeof(len));
	if ((file = (char *)malloc(len + 1)) == NULL)
		fatal("privsep", NULL);
	must_read(file, len);
	file[len] = '\0';

	for (f=authorized; *f != NULL; f++) {
		if (regcomp(&preg, *f, REG_NOSUB) != 0)
			/* Should not happen */
			fatal("privsep", "unable to compile a regex");
		if (regexec(&preg, file, 0, NULL, 0) == 0) {
			regfree(&preg);
			break;
		}
		regfree(&preg);
	}
	if (*f == NULL) {
		log_warnx("privsep", "not authorized to open %s", file);
		rc = -1;
		must_write(&rc, sizeof(int));
		free(file);
		return;
	}
	if ((fd = open(file, O_RDONLY)) == -1) {
		rc = -1;
		must_write(&rc, sizeof(int));
		free(file);
		return;
	}
	free(file);
	must_write(&fd, sizeof(int));
	send_fd(fd);
	close(fd);
}

void
asroot_ethtool()
{
	struct ifreq ifr = {};
	struct ethtool_cmd ethc = {};
	int len, rc, sock = -1;
	char *ifname;

	must_read(&len, sizeof(int));
	if ((ifname = (char*)malloc(len + 1)) == NULL)
		fatal("privsep", NULL);
	must_read(ifname, len);
	ifname[len] = '\0';
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	free(ifname);
	ifr.ifr_data = (caddr_t)&ethc;
	ethc.cmd = ETHTOOL_GSET;
	if (((rc = sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) ||
	    (rc = ioctl(sock, SIOCETHTOOL, &ifr)) != 0) {
		if (sock != -1) close(sock);
		must_write(&rc, sizeof(int));
		return;
	}
	close(sock);
	must_write(&rc, sizeof(int));
	must_write(&ethc, sizeof(struct ethtool_cmd));
}

int
asroot_iface_init_os(int ifindex, char *name, int *fd)
{
	int rc;
	/* Open listening socket to receive/send frames */
	if ((*fd = socket(PF_PACKET, SOCK_RAW,
		    htons(ETH_P_ALL))) < 0) {
		rc = errno;
		must_write(&rc, sizeof(rc));
		return rc;
	}

	struct sockaddr_ll sa = {
		.sll_family = AF_PACKET,
		.sll_ifindex = ifindex
	};
	if (bind(*fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
		rc = errno;
		log_warn("privsep",
		    "unable to bind to raw socket for interface %s",
		    name);
		return rc;
	}

	/* Set filter */
	log_debug("privsep", "set BPF filter for %s", name);
	static struct sock_filter lldpd_filter_f[] = { LLDPD_FILTER_F };
	struct sock_fprog prog = {
		.filter = lldpd_filter_f,
		.len = sizeof(lldpd_filter_f) / sizeof(struct sock_filter)
	};
	if (setsockopt(*fd, SOL_SOCKET, SO_ATTACH_FILTER,
                &prog, sizeof(prog)) < 0) {
		rc = errno;
		log_warn("privsep", "unable to change filter for %s", name);
		return rc;
	}

#ifdef SO_LOCK_FILTER
	int enable = 1;
	if (setsockopt(*fd, SOL_SOCKET, SO_LOCK_FILTER,
		&enable, sizeof(enable)) < 0) {
		if (errno != ENOPROTOOPT) {
			rc = errno;
			log_warn("privsep", "unable to lock filter for %s", name);
			return rc;
		}
	}
#endif

	return 0;
}
