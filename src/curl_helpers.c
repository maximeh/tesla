// vim: set expandtab:set tabstop=2
/*
 * curl_helpers
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

#include "curl_helpers.h"
#include "tesla.h"

int
curl_update(unsigned int value, struct tm *date)
{
        char buffer[96];
        char keen_url[384];
        char tstamp[88];

        strftime(tstamp, 88, "%Y-%m-%dT%H:%M:%S%z", date);

        sprintf(buffer,
                "{\"keen\":{\"timestamp\":\"%s\"},\"consumption\":{\"value\":%d}}",
                tstamp, value);

        sprintf(keen_url,
                "https://api.keen.io/3.0/projects/%s/events/tesla?api_key=%s",
                keenio_project, keenio_key);

        CURLcode res;
        curl_global_init(CURL_GLOBAL_ALL);
        CURL *curl = curl_easy_init();
        if (!curl) {
                fprintf(stderr, "ERROR: curl_init failed.\n");
                return -1;
        }

        struct curl_slist *headers = NULL;
        struct memoryStruct mbuffer;

        mbuffer.memory = NULL;
        mbuffer.size = 0;

        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "charsets: utf-8");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
        curl_easy_setopt(curl, CURLOPT_POST, 1);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3);
        curl_easy_setopt(curl, CURLOPT_URL, keen_url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&mbuffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
                fprintf(stderr, "curl failed: %s\n", curl_easy_strerror(res));
                goto clean_curl;
                return -1;
        }

        debug("curl answer: %s\n", mbuffer.memory);

        goto clean_curl;
        return 0;

 clean_curl:
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        curl_slist_free_all(headers);
        free(mbuffer.memory);
}

static void *
curl_realloc(void *ptr, size_t size)
{
        /* There might be a realloc() out there that doesn't like reallocing
           NULL pointers, so we take care of it here */
        if (ptr)
                return realloc(ptr, size);
        else
                return malloc(size);
}

size_t
write_memory_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
        size_t realsize = size * nmemb;
        struct memoryStruct *mem = (struct memoryStruct *)data;

        mem->memory = (char *)
            curl_realloc(mem->memory, mem->size + realsize + 1);
        if (mem->memory) {
                memcpy(&(mem->memory[mem->size]), ptr, realsize);
                mem->size += realsize;
                mem->memory[mem->size] = 0;
        }
        return realsize;
}
