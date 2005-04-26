/*
 * $Id: scrandom.h 603 2002-05-13 12:23:28Z aet $
 *
 * Copyright (C) 2002
 *  Antti Tapaninen <aet@cc.hut.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _SC_RANDOM_H
#define _SC_RANDOM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Get entropy from /dev/[u]random or PRNGD/EGD
 *
 * This is mostly needed for OpenSSL on some
 * misconfigured systems, and also because
 * OpenSSL's build system is.. ahem, ugly?
 *
 * Returns -1 on error, else length of data
 */

extern int scrandom_get_data(unsigned char *buf, unsigned int len);

#ifdef __cplusplus
}
#endif
#endif
