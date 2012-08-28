/* -*- mode: c; c-file-style: "openbsd" -*- */
/*
 * Copyright (c) 2012 Vincent Bernat <bernat@luffy.cx>
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

#ifndef _CTL_H
#define _CTL_H

#define LLDPD_CTL_SOCKET	"/var/run/lldpd.socket"

#include <stdint.h>
#include "marshal.h"

enum hmsg_type {
	NONE,
	GET_INTERFACES,		/* Get list of interfaces */
	GET_INTERFACE,		/* Get all information related to an interface */
	SET_PORT,		/* Set port-related information (location, power, policy) */
};

/* ctl.c */
int	 ctl_create(char *);
int	 ctl_connect(char *);
void	 ctl_cleanup(char *);
int	 ctl_msg_send(int, enum hmsg_type, void *, size_t);
int	 ctl_msg_recv(int, enum hmsg_type *, void **);

int	 ctl_msg_send_unserialized(uint8_t **, size_t *,
				       enum hmsg_type,
				       void *, struct marshal_info *);
size_t	 ctl_msg_recv_unserialized(uint8_t **, size_t *,
				       enum hmsg_type,
				       void **, struct marshal_info *);

#endif
