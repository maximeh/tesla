// vim: set expandtab:set tabstop=2
/*
 * rrd_helpers
 *
 * Copyright (C) 2013 Maxime Hadjinlian <maxime.hadjinlian@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#pragma once

#include <time.h>
#include <curl/curl.h>

struct memoryStruct
{
	char *memory;
	size_t size;
};

int curl_update (unsigned int step, struct tm *date);
size_t write_memory_callback (void *ptr, size_t size, size_t nmemb, void *data);
static void* curl_realloc(void *ptr, size_t size);
