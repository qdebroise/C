TARGET := a.out

SOURCES := \
	lz.c \
	huffman.c \
	test/lz_test.c \

INCLUDES := \
	array.h \
	bitarray.h \
	lz.h \
	huffman.h \

.PHONY: all
all: $(TARGET)

$(TARGET): $(SOURCES) $(INCLUDES)
	# clang -std=c11 -g3 -O0 -Wall -Wextra -I./ $(SOURCES)
	clang -std=c11 -g3 -O0 -fsanitize=address -Wall -Wextra -I./ $(SOURCES)
	# clang -std=c11 -O2 -DNDEBUG -Wall -Wextra -I./ $(SOURCES)

pm: pm.c
	clang -std=c11 -Wall -Wextra -O0 -g -fsanitize=address $^

.PHONY: clean
clean:
	rm $(TARGET)
