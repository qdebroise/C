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
	clang -std=c11 -Wall -Wextra -g3 -O0 -I./ $(SOURCES)
	# clang -std=c11 -Wall -Wextra -g3 -O0 -fsanitize=address -I./ $(SOURCES)
	# clang -std=c11 -Wall -Wextra -O2 -DNDEBUG -I./ $(SOURCES)

package-merge: package_merge.c test/package_merge_test.c
	# clang -std=c11 -Wall -Wextra -O2 -DNDEBUG -I./ $^
	clang -std=c11 -Wall -Wextra -O0 -g -fsanitize=address -I./ $^

deflate: test/deflate_test.c deflate.c deflate.h array.h bitarray.h package_merge.c
	clang -std=c11 -Wall -Wextra -O0 -g -fsanitize=address -I./ $^
	# clang -std=c11 -Wall -Wextra -O0 -g -I./ $^

bench: package_merge.c benchmark_package_merge.c timing.c pcg/pcg_basic.c
	clang -std=c11 -Wall -Wextra -O2 -I./ -lm $^

.PHONY: clean
clean:
	rm $(TARGET)
