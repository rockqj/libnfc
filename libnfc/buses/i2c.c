/*-
 * Free/Libre Near Field Communication (NFC) library
 *
 * Libnfc historical contributors:
 * Copyright (C) 2009      Roel Verdult
 * Copyright (C) 2009-2013 Romuald Conty
 * Copyright (C) 2010-2012 Romain Tarti?re
 * Copyright (C) 2010-2013 Philippe Teuwen
 * Copyright (C) 2012-2013 Ludovic Rousseau
 * Additional contributors of this file:
 * Copyright (C) 2013      Laurent Latil
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 */

/**
 * @file i2c.c
 * @brief I2C driver (implemented / tested for Linux only currently)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif // HAVE_CONFIG_H
#include "i2c.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include <nfc/nfc.h>
#include "nfc-internal.h"

#define LINUX_I2C_DRIVER_NAME "linux_i2c"

#define LOG_GROUP    NFC_LOG_GROUP_COM
#define LOG_CATEGORY "libnfc.bus.i2c"

#  if defined (__linux__)
const char *i2c_ports_device_radix[] =
{ "i2c-", NULL };
#  else
#    error "Can't determine I2C devices standard names for your system"
#  endif


struct i2c_device {
  int fd;             // I2C device file descriptor
};

#define I2C_DATA( X ) ((struct i2c_device *) X)

i2c_device
i2c_open(const char *pcI2C_busName, uint32_t devAddr)
{
  struct i2c_device *id = malloc(sizeof(struct i2c_device));

  if (id == 0)
    return INVALID_I2C_BUS ;

  id->fd = open(pcI2C_busName, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (id->fd == -1) {
    perror("Cannot open I2C bus");
    i2c_close(id);
    return INVALID_I2C_BUS ;
  }

  if (ioctl(id->fd, I2C_SLAVE, devAddr) < 0) {
    perror("Cannot select I2C device");
    i2c_close(id);
    return INVALID_I2C_ADDRESS ;
  }

  return id;
}

void
i2c_close(const i2c_device id)
{
  if (I2C_DATA(id) ->fd >= 0) {
    close(I2C_DATA(id) ->fd);
  }
  free(id);
}

/**
 * @brief Read a frame from the I2C device and copy data to \a pbtRx
 *
 * @param timeout Time out for data read  (in milliseconds). 0 for not timeout.
 * @return 0 on success, otherwise driver error code
 */
int
i2c_read(i2c_device id, uint8_t *pbtRx, const size_t szRx, void *abort_p,
         int timeout)
{
  int iAbortFd = abort_p ? *((int *) abort_p) : 0;

  int res;
  int done = 0;

  struct timeval start_tv, cur_tv;
  long long duration;

  ssize_t recCount = read(I2C_DATA(id) ->fd, pbtRx, szRx);

  if (recCount < 0) {
    res = NFC_EIO;
  } else {
    if (recCount < (ssize_t)szRx) {
      res = NFC_EINVARG;
    } else {
      res = recCount;
    }
  }
  return res;
}

/**
 * @brief Write a frame to I2C device containing \a pbtTx content
 *
 * @param timeout Time out for data read  (in milliseconds). 0 for not timeout.
 * @return 0 on success, otherwise a driver error is returned
 */
int
i2c_write(i2c_device id, const uint8_t *pbtTx, const size_t szTx, int timeout)
{
  LOG_HEX(LOG_GROUP, "TX", pbtTx, szTx);

  ssize_t writeCount;
  writeCount = write(I2C_DATA(id) ->fd, pbtTx, szTx);

  if ((const ssize_t) szTx == writeCount) {
    log_put(LOG_GROUP, LOG_CATEGORY, NFC_LOG_PRIORITY_DEBUG,
            "wrote %d bytes successfully.", szTx);
    return NFC_SUCCESS;
  } else {
    log_put(LOG_GROUP, LOG_CATEGORY, NFC_LOG_PRIORITY_ERROR,
            "Error: wrote only %d bytes (%d expected).", writeCount, (int) szTx);
    return NFC_EIO;
  }
}

char **
i2c_list_ports(void)
{
  char **res = malloc(sizeof(char *));
  if (!res) {
    perror("malloc");
    return res;
  }
  size_t szRes = 1;

  res[0] = NULL;
  DIR *dir;
  if ((dir = opendir("/dev")) == NULL) {
    perror("opendir error: /dev");
    return res;
  }
  struct dirent entry;
  struct dirent *result;

  while ((readdir_r(dir, &entry, &result) == 0) && (result != NULL)) {
    const char **p = i2c_ports_device_radix;
    while (*p) {
      if (!strncmp(entry.d_name, *p, strlen(*p))) {
        char **res2 = realloc(res, (szRes + 1) * sizeof(char *));
        if (!res2) {
          perror("malloc");
          goto oom;
        }
        res = res2;
        if (!(res[szRes - 1] = malloc(6 + strlen(entry.d_name)))) {
          perror("malloc");
          goto oom;
        }
        sprintf(res[szRes - 1], "/dev/%s", entry.d_name);

        szRes++;
        res[szRes - 1] = NULL;
      }
      p++;
    }
  }
oom:
  closedir(dir);

  return res;
}
