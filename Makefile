# Note: Last built with version 1.38.30 of Emscripten

# TODO: Emit a file showing which version of emcc and SQLite was used to compile the emitted output.
# TODO: Create a release on Github with these compiled assets rather than checking them in
# TODO: Consider creating different files based on browser vs module usage: https://github.com/vuejs/vue/tree/dev/dist

# I got this handy makefile syntax from : https://github.com/mandel59/sqlite-wasm (MIT License) Credited in LICENSE
# To use another version of Sqlite, visit https://www.sqlite.org/download.html and copy the appropriate values here:
SQLITE_AMALGAMATION = sqlite-amalgamation-3310100
SQLITE_AMALGAMATION_ZIP_URL = https://www.sqlite.org/2020/sqlite-amalgamation-3310100.zip
SQLITE_AMALGAMATION_ZIP_SHA1 = a58e91a39b7b4ab720dbc843c201fb6a18eaf32b
#SQLITE_AMALGAMATION = sqlite-amalgamation-3350000
#SQLITE_AMALGAMATION_ZIP_URL = https://www.sqlite.org/2021/sqlite-amalgamation-3350000.zip
#SQLITE_AMALGAMATION_ZIP_SHA1 = ba64bad885c9f51df765a9624700747e7bf21b79

# Note that extension-functions.c hasn't been updated since 2010-02-06, so likely doesn't need to be updated
#EXTENSION_FUNCTIONS = extension-functions.c
#EXTENSION_FUNCTIONS_URL = https://www.sqlite.org/contrib/download/extension-functions.c?get=25
#EXTENSION_FUNCTIONS_SHA1 = c68fa706d6d9ff98608044c00212473f9c14892f

EMCC=emcc

CFLAGS= \
	-Oz \
	-DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_DISABLE_LFS \
	-DSQLITE_ENABLE_FTS3 \
	-DSQLITE_ENABLE_FTS3_PARENTHESIS \
	-DSQLITE_THREADSAFE=0 \
	-Isqlite-src/$(SQLITE_AMALGAMATION)/ \
	-Ilibs/libstemmer_c \
	-Ilibs/xor_filter

# When compiling to WASM, enabling memory-growth is not expected to make much of an impact, so we enable it for all builds
# Since tihs is a library and not a standalone executable, we don't want to catch unhandled Node process exceptions
# So, we do : `NODEJS_CATCH_EXIT=0`, which fixes issue: https://github.com/kripken/sql.js/issues/173 and https://github.com/kripken/sql.js/issues/262
EMFLAGS = \
	--memory-init-file 0 \
	-s RESERVED_FUNCTION_POINTERS=64 \
	-s ALLOW_TABLE_GROWTH=1 \
	-s EXPORTED_FUNCTIONS=@src/exported_functions.json \
	-s EXTRA_EXPORTED_RUNTIME_METHODS=@src/exported_runtime_methods.json \
	-s SINGLE_FILE=0 \
	-s NODEJS_CATCH_EXIT=0 \
	-s ASSERTIONS=0 \
	-s FILESYSTEM=1 \
	-s MALLOC=emmalloc

#EMFLAGS_ASM = \
#	-s WASM=0

#EMFLAGS_ASM_MEMORY_GROWTH = \
#	-s WASM=0 \
#	-s ALLOW_MEMORY_GROWTH=1

EMFLAGS_WASM = \
	-s WASM=1 \
	-s ALLOW_MEMORY_GROWTH=1

EMFLAGS_OPTIMIZED= \
	-s INLINING_LIMIT=50 \
	-Oz \
	-flto \
	--closure 1


EMFLAGS_DEBUG = \
	-g \
	-s INLINING_LIMIT=0 \
	-s ASSERTIONS=1 \

SRC = sqlite-src/$(SQLITE_AMALGAMATION)/sqlite3.c src/fullTextSearch.c src/vfs.c libs/libstemmer_c/runtime/api.c libs/libstemmer_c/runtime/utilities.c libs/libstemmer_c/libstemmer/libstemmer_utf8.c libs/libstemmer_c/src_c/stem_UTF_8_english.c libs/libstemmer_c/src_c/stem_UTF_8_spanish.c
OBJ = $(SRC:.c=.bc)

RELEASE_DIR = build/release
RELEASE_OBJ = $(addprefix $(RELEASE_DIR)/, $(OBJ))

DEBUG_DIR = build/debug
DEBUG_OBJ = $(addprefix $(DEBUG_DIR)/, $(OBJ))

#BITCODE_FILES = out/sqlite3.bc
#BITCODE_FILES = out/sqlite3.bc out/extension-functions.bc

OUTPUT_WRAPPER_FILES = src/shell-pre.js src/shell-post.js

SOURCE_API_FILES = src/api.js

EMFLAGS_PRE_JS_FILES = \
	--pre-js src/api.js

EXPORTED_METHODS_JSON_FILES = src/exported_functions.json src/exported_runtime_methods.json

all: optimized debug worker

.PHONY: debug
debug: CFLAGS += -g
debug: dist/sql-wasm-debug.js

#dist/sql-asm-debug.js: $(BITCODE_FILES) $(OUTPUT_WRAPPER_FILES) $(SOURCE_API_FILES) $(EXPORTED_METHODS_JSON_FILES)
#	$(EMCC) $(EMFLAGS) $(EMFLAGS_DEBUG) $(EMFLAGS_ASM) $(BITCODE_FILES) $(EMFLAGS_PRE_JS_FILES) -o $@
#	mv $@ out/tmp-raw.js
#	cat src/shell-pre.js out/tmp-raw.js src/shell-post.js > $@
#	rm out/tmp-raw.js

dist/sql-wasm-debug.js: sqlite-src/$(SQLITE_AMALGAMATION) $(DEBUG_OBJ) $(OUTPUT_WRAPPER_FILES) $(SOURCE_API_FILES) $(EXPORTED_METHODS_JSON_FILES)
	EMCC_CLOSURE_ARGS="--externs src/fs-externs.js" $(EMCC) $(EMFLAGS) $(EMFLAGS_DEBUG) $(EMFLAGS_WASM) $(DEBUG_OBJ) $(EMFLAGS_PRE_JS_FILES) -o $@
	mv $@ build/debug/tmp-raw.js
	cat src/shell-pre.js build/debug/tmp-raw.js src/shell-post.js > $@
	rm build/debug/tmp-raw.js

$(DEBUG_DIR)/%.bc: %.c
	mkdir -p $(@D)
	$(EMCC) $(CFLAGS) -s LINKABLE=1 -c $< -o $@

.PHONY: optimized
optimized: dist/sql-wasm.js

#dist/sql-asm.js: $(BITCODE_FILES) $(OUTPUT_WRAPPER_FILES) $(SOURCE_API_FILES) $(EXPORTED_METHODS_JSON_FILES)
#	$(EMCC) $(EMFLAGS) $(EMFLAGS_OPTIMIZED) $(EMFLAGS_ASM) $(BITCODE_FILES) $(EMFLAGS_PRE_JS_FILES) -o $@
#	mv $@ out/tmp-raw.js
#	cat src/shell-pre.js out/tmp-raw.js src/shell-post.js > $@
#	rm out/tmp-raw.js

dist/sql-wasm.js: sqlite-src/$(SQLITE_AMALGAMATION) $(RELEASE_OBJ) $(OUTPUT_WRAPPER_FILES) $(SOURCE_API_FILES) $(EXPORTED_METHODS_JSON_FILES)
	EMCC_CLOSURE_ARGS="--externs src/fs-externs.js" $(EMCC) $(EMFLAGS) $(EMFLAGS_OPTIMIZED) $(EMFLAGS_WASM) $(RELEASE_OBJ) $(EMFLAGS_PRE_JS_FILES) -o $@
	mv $@ build/release/tmp-raw.js
	cat src/shell-pre.js build/release/tmp-raw.js src/shell-post.js > $@
	rm build/release/tmp-raw.js

$(RELEASE_DIR)/%.bc: %.c
	mkdir -p $(@D)
	$(EMCC) $(CFLAGS) -s LINKABLE=1 -c $< -o $@

#$(RELEASE_DIR)/sqlite-src/$(SQLITE_AMALGAMATION)/sqlite3.bc: sqlite-src/$(SQLITE_AMALGAMATION)/sqlite3.c
#	mkdir -p $(@D)
#	$(EMCC) $(CFLAGS) -s LINKABLE=1 -c $< -o $@

#dist/sql-asm-memory-growth.js: $(BITCODE_FILES) $(OUTPUT_WRAPPER_FILES) $(SOURCE_API_FILES) $(EXPORTED_METHODS_JSON_FILES)
#	$(EMCC) $(EMFLAGS) $(EMFLAGS_OPTIMIZED) $(EMFLAGS_ASM_MEMORY_GROWTH) $(BITCODE_FILES) $(EMFLAGS_PRE_JS_FILES) -o $@
#	mv $@ out/tmp-raw.js
#	cat src/shell-pre.js out/tmp-raw.js src/shell-post.js > $@
#	rm out/tmp-raw.js

# Web worker API
.PHONY: worker
worker: dist/worker.sql-wasm.js dist/worker.sql-wasm-debug.js

#dist/worker.sql-asm.js: dist/sql-asm.js src/worker.js
#	cat $^ > $@

#dist/worker.sql-asm-debug.js: dist/sql-asm-debug.js src/worker.js
#	cat $^ > $@

dist/worker.sql-wasm.js: dist/sql-wasm.js src/worker.js
	cat $^ > $@

dist/worker.sql-wasm-debug.js: dist/sql-wasm-debug.js src/worker.js
	cat $^ > $@

# Building it this way gets us a wrapper that _knows_ it's in worker mode, which is nice.
# However, since we can't tell emcc that we don't need the wasm generated, and just want the wrapper, we have to pay to have the .wasm generated
# even though we would have already generated it with our sql-wasm.js target above.
# This would be made easier if this is implemented: https://github.com/emscripten-core/emscripten/issues/8506
# dist/worker.sql-wasm.js: $(BITCODE_FILES) $(OUTPUT_WRAPPER_FILES) src/api.js src/worker.js $(EXPORTED_METHODS_JSON_FILES) dist/sql-wasm-debug.wasm
# 	$(EMCC) $(EMFLAGS) $(EMFLAGS_OPTIMIZED) -s ENVIRONMENT=worker -s $(EMFLAGS_WASM) $(BITCODE_FILES) --pre-js src/api.js -o out/sql-wasm.js
# 	mv out/sql-wasm.js out/tmp-raw.js
# 	cat src/shell-pre.js out/tmp-raw.js src/shell-post.js src/worker.js > $@
# 	#mv out/sql-wasm.wasm dist/sql-wasm.wasm
# 	rm out/tmp-raw.js

# dist/worker.sql-wasm-debug.js: $(BITCODE_FILES) $(OUTPUT_WRAPPER_FILES) src/api.js src/worker.js $(EXPORTED_METHODS_JSON_FILES) dist/sql-wasm-debug.wasm
# 	$(EMCC) -s ENVIRONMENT=worker $(EMFLAGS) $(EMFLAGS_DEBUG) -s ENVIRONMENT=worker -s WASM_BINARY_FILE=sql-wasm-foo.debug $(EMFLAGS_WASM) $(BITCODE_FILES) --pre-js src/api.js -o out/sql-wasm-debug.js
# 	mv out/sql-wasm-debug.js out/tmp-raw.js
# 	cat src/shell-pre.js out/tmp-raw.js src/shell-post.js src/worker.js > $@
# 	#mv out/sql-wasm-debug.wasm dist/sql-wasm-debug.wasm
# 	rm out/tmp-raw.js

#out/sqlite3.bc: sqlite-src/$(SQLITE_AMALGAMATION)
#	mkdir -p out
#	# Generate llvm bitcode
#	$(EMCC) $(CFLAGS) -c sqlite-src/$(SQLITE_AMALGAMATION)/sqlite3.c -o $@

#out/extension-functions.bc: sqlite-src/$(SQLITE_AMALGAMATION)/$(EXTENSION_FUNCTIONS)
#	mkdir -p out
#	$(EMCC) $(CFLAGS) -s LINKABLE=1 -c sqlite-src/$(SQLITE_AMALGAMATION)/extension-functions.c -o $@

# TODO: This target appears to be unused. If we re-instatate it, we'll need to add more files inside of the JS folder
# module.tar.gz: test package.json AUTHORS README.md dist/sql-asm.js
# 	tar --create --gzip $^ > $@

## cache
cache/$(SQLITE_AMALGAMATION).zip:
	mkdir -p cache
	curl -LsSf '$(SQLITE_AMALGAMATION_ZIP_URL)' -o $@

#cache/$(EXTENSION_FUNCTIONS):
#	mkdir -p cache
#	curl -LsSf '$(EXTENSION_FUNCTIONS_URL)' -o $@

## sqlite-src
.PHONY: sqlite-src
sqlite-src: sqlite-src/$(SQLITE_AMALGAMATION)
#sqlite-src: sqlite-src/$(SQLITE_AMALGAMATION) sqlite-src/$(EXTENSION_FUNCTIONS)

sqlite-src/$(SQLITE_AMALGAMATION): cache/$(SQLITE_AMALGAMATION).zip
	mkdir -p sqlite-src
	echo '$(SQLITE_AMALGAMATION_ZIP_SHA1)  ./cache/$(SQLITE_AMALGAMATION).zip' > cache/check.txt
	sha1sum -c cache/check.txt
	rm -rf $@
	unzip 'cache/$(SQLITE_AMALGAMATION).zip' -d sqlite-src/
	touch $@

#sqlite-src/$(SQLITE_AMALGAMATION)/$(EXTENSION_FUNCTIONS): cache/$(EXTENSION_FUNCTIONS)
#	mkdir -p sqlite-src
#	echo '$(EXTENSION_FUNCTIONS_SHA1)  ./cache/$(EXTENSION_FUNCTIONS)' > cache/check.txt
#	sha1sum -c cache/check.txt
#	cp 'cache/$(EXTENSION_FUNCTIONS)' $@


.PHONY: clean
clean:
	rm -f dist/* cache/*
	rm -rf sqlite-src/ build/* c/

