# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2023 Nicholas Chin

CC=cc
CFLAGS=-Wall -Wextra -Werror -O2 -pedantic
ifeq ($(shell uname), OpenBSD)
	CFLAGS += -l$(shell uname -p)
endif
SRCS=dell_flash_unlock.c accessors.c

all: $(SRCS) accessors.h
	$(CC) $(CFLAGS) $(SRCS) -o dell_flash_unlock

clean:
	rm -f dell_flash_unlock
