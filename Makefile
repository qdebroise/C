TARGET := a.out

SOURCES := \
	huffman.c \
	lz.c \
	package_merge.c \
	test/lz_test.c \

INCLUDES := \
	array.h \
	bitarray.h \
	huffman.h \
	lz.h \
	package_merge.h \

.PHONY: all
all: $(TARGET)

$(TARGET): $(SOURCES) $(INCLUDES)
	# clang -std=c11 -g3 -O0 -Wall -Wextra -I./ $(SOURCES)
	clang -std=c11 -g3 -O0 -fsanitize=address -Wall -Wextra -I./ $(SOURCES)
	# clang -std=c11 -O2 -DNDEBUG -Wall -Wextra -I./ $(SOURCES)

package-merge: package_merge.c test/package_merge_test.c
	# clang -std=c11 -Wall -Wextra -O2 -DNDEBUG -I./ $^
	clang -std=c11 -Wall -Wextra -O0 -g -fsanitize=address -I./ $^

.PHONY: clean
clean:
	rm $(TARGET)
