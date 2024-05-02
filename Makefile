# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2023 Nicholas Chin

CC=cc
CFLAGS=-Wall -Wextra -Werror -O2 -pedantic
SRCS=dell_flash_unlock.c accessors.c

all: $(SRCS) accessors.h
	CFLAGS="$(CFLAGS)"; \
	if [ $$(uname) = OpenBSD ] || [ $$(uname) = NetBSD ]; then \
		CFLAGS="$$CFLAGS -l$$(uname -p)"; \
	fi; \
	$(CC) $$CFLAGS $(SRCS) -o dell_flash_unlock

clean:
	rm -f dell_flash_unlock
