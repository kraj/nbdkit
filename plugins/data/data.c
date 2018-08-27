/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_GNUTLS) && defined(HAVE_GNUTLS_BASE64_DECODE2)
#include <gnutls/gnutls.h>
#endif

#include <nbdkit-plugin.h>

#include "sparse.h"

/* If raw|base64|data parameter seen. */
static int data_seen = 0;

/* size= parameter on the command line, -1 if not set. */
static int64_t size = -1;

/* Size of data specified on the command line. */
static int64_t data_size = -1;

/* Sparse array. */
static struct sparse_array *sa;

/* Debug directory operations (-D data.dir=1). */
int data_debug_dir;

static void
data_load (void)
{
  sa = alloc_sparse_array (data_debug_dir);
  if (sa == NULL) {
    perror ("malloc");
    exit (EXIT_FAILURE);
  }
}

/* On unload, free the sparse array. */
static void
data_unload (void)
{
  free_sparse_array (sa);
}

/* Parse the base64 parameter. */
static int
read_base64 (const char *value)
{
#if defined(HAVE_GNUTLS) && defined(HAVE_GNUTLS_BASE64_DECODE2)
  gnutls_datum_t in, out;
  int err;

  in.data = (unsigned char *) value;
  in.size = strlen (value);
  err = gnutls_base64_decode2 (&in, &out);
  if (err != GNUTLS_E_SUCCESS) {
    nbdkit_error ("base64: %s", gnutls_strerror (err));
    return -1;
  }

  if (sparse_array_write (sa, out.data, out.size, 0) == -1)
    return -1;
  free (out.data);
  return 0;
#else
  nbdkit_error ("base64 is not supported in this build of the plugin");
  return -1;
#endif
}

/* Parse the data parameter. */
static int
read_data (const char *value)
{
  size_t offset = 0;
  size_t i, len = strlen (value);

  for (i = 0; i < len; ++i) {
    int j, n;
    char c;

    /* XXX Using the %i type specifier limits this plugin to creating
     * 32 bit data (even on 64 bit platforms).
     */
    if (sscanf (&value[i], " @%i%n", &j, &n) == 1) {
      if (j == -1) {
        nbdkit_error ("data parameter @OFFSET must not be negative");
        return -1;
      }
      i += n;
      offset = j;
    }
    else if (sscanf (&value[i], " %i%n", &j, &n) == 1) {
      if (j < 0 || j > 255) {
        nbdkit_error ("data parameter BYTE must be in the range 0..255");
        return -1;
      }
      i += n;

      if (data_size < offset+1)
        data_size = offset+1;

      /* Store the byte. */
      c = j;
      if (sparse_array_write (sa, &c, 1, offset) == -1)
        return -1;
      offset++;
    }
    else {
      nbdkit_error ("data parameter: parsing error at offset %zu", i);
      return -1;
    }
  }

  return 0;
}

static int
data_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    size = r;
  }
  else if (strcmp (key, "raw") == 0 ||
           strcmp (key, "base64") == 0 ||
           strcmp (key, "data") == 0) {
    if (data_seen) {
      nbdkit_error ("raw|base64|data parameter must be specified exactly once");
      return -1;
    }
    data_seen = 1;

    if (strcmp (key, "raw") == 0) {
      data_size = strlen (value);
      if (sparse_array_write (sa, value, data_size, 0) == -1)
        return -1;
    }
    else if (strcmp (key, "base64") == 0) {
      if (read_base64 (value) == -1)
        return -1;
    }
    else if (strcmp (key, "data") == 0) {
      if (read_data (value) == -1)
        return -1;
    }
    else
      abort (); /* cannot happen */
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Check the raw|base64|data was specified, and set the final size. */
static int
data_config_complete (void)
{
  if (!data_seen) {
    nbdkit_error ("raw|base64|data parameter was not specified");
    return -1;
  }

  nbdkit_debug ("implicit data size: %zu", data_size);

  /* If size == -1 it means the size= parameter was not given so we
   * must use the data size.
   */
  if (size == -1)
    size = data_size;
  nbdkit_debug ("final size: %zu", data_size);

  return 0;
}

#define data_config_help \
  "data|raw|base64=...     Specify disk data on the command line\n" \
  "size=<SIZE>  (required) Size of the backing disk"

/* Provide a way to detect if the base64 feature is supported. */
static void
data_dump_plugin (void)
{
#if defined(HAVE_GNUTLS) && defined(HAVE_GNUTLS_BASE64_DECODE2)
  printf ("data_base64=yes\n");
#endif
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* No meaning, just used as the address for the handle. */
static int dh;

/* Create the per-connection handle. */
static void *
data_open (int readonly)
{
  return &dh;
}

/* Get the disk size. */
static int64_t
data_get_size (void *handle)
{
  return size;
}

/* Read data. */
static int
data_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  sparse_array_read (sa, buf, count, offset);
  return 0;
}

/* Write data. */
static int
data_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  return sparse_array_write (sa, buf, count, offset);
}

/* Zero. */
static int
data_zero (void *handle, uint32_t count, uint64_t offset, int may_trim)
{
  sparse_array_zero (sa, count, offset);
  return 0;
}

/* Trim (same as zero). */
static int
data_trim (void *handle, uint32_t count, uint64_t offset)
{
  sparse_array_zero (sa, count, offset);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "data",
  .version           = PACKAGE_VERSION,
  .load              = data_load,
  .unload            = data_unload,
  .config            = data_config,
  .config_complete   = data_config_complete,
  .config_help       = data_config_help,
  .dump_plugin       = data_dump_plugin,
  .open              = data_open,
  .get_size          = data_get_size,
  .pread             = data_pread,
  .pwrite            = data_pwrite,
  .zero              = data_zero,
  .trim              = data_trim,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN(plugin)