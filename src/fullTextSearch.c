//#include <stdlib.h>
//#include <stdio.h>
//#include <stdint.h>
//#include <stdbool.h>
//#include <string.h>
#include <ctype.h>
//#include <time.h>
#include <assert.h>

#include <sqlite3.h>
#include <include/libstemmer.h>
#include <include/xorfilter.h>

#define FILTER_SEED 0xffffffff
#define MAX_TOKEN_SIZE 256
#define TOKEN_LIST_SIZE 500 // Initial size for token hash array

xor8_t* createFullTextSearchFilter(const char* language, const char* text);
uint8_t* createFilterArray(const char* language, const char* text);
int filterArraySize();
void deleteFilterArray();
void deleteFilter(xor8_t* filter);

//uint8_t _hashList[100];

typedef struct _hashList {
    int size;
	int numItems;
    uint64_t* buffer;
} hashList;

size_t utf8len(const void *str) {
 const unsigned char *s = (const unsigned char *)str;
 size_t length = 0;

 while ('\0' != *s) {
   if (0xf0 == (0xf8 & *s)) {
     // 4-byte utf8 code point (began with 0b11110xxx)
     s += 4;
   } else if (0xe0 == (0xf0 & *s)) {
     // 3-byte utf8 code point (began with 0b1110xxxx)
     s += 3;
   } else if (0xc0 == (0xe0 & *s)) {
     // 2-byte utf8 code point (began with 0b110xxxxx)
     s += 2;
   } else { // if (0x00 == (0x80 & *s)) {
     // 1-byte ascii (began with 0b0xxxxxxx)
     s += 1;
   }

   // no matter the bytes we marched s forward by, it was
   // only 1 utf8 codepoint
   length++;
 }

 return length;
}

static inline uint32_t
murmurhash (
  const void           *key,
  uint32_t              key_length_in_bytes,
  uint32_t              seed
) {
  uint32_t              c1 = 0xcc9e2d51;
  uint32_t              c2 = 0x1b873593;
  uint32_t              r1 = 15;
  uint32_t              r2 = 13;
  uint32_t              m = 5;
  uint32_t              n = 0xe6546b64;
  uint32_t              h = 0;
  uint32_t              k = 0;
  uint8_t              *d = (uint8_t *) key;
  const uint32_t       *chunks = NULL;
  const uint8_t        *tail = NULL;
  int                   i = 0;
  int                   l = (key_length_in_bytes / sizeof(uint32_t));

  h = seed;

  chunks = (const uint32_t *) (d + l * sizeof(uint32_t));
  tail = (const uint8_t *) (d + l * sizeof(uint32_t));

  for (i = -l; i != 0; ++i) {
    k = chunks[i];
    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;
    h ^= k;
    h = (h << r2) | (h >> (32 - r2));
    h = h * m + n;
  }

  k = 0;
  switch (key_length_in_bytes & 3) {
    case 3: k ^= (tail[2] << 16);
    case 2: k ^= (tail[1] << 8);
    case 1:
      k ^= tail[0];
      k *= c1;
      k = (k << r1) | (k >> (32 - r1));
      k *= c2;
      h ^= k;
  }

  h ^= key_length_in_bytes;
  h ^= (h >> 16);
  h *= 0x85ebca6b;
  h ^= (h >> 13);
  h *= 0xc2b2ae35;
  h ^= (h >> 16);

  return h;

}

struct sb_stemmer * stemmer = 0;

enum {
	TYPE_UINT8,
	TYPE_UINT16,
	TYPE_UINT32
};

void getBufferFromIndexString(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
	if(argc < 1 || sqlite3_value_type(argv[0]) != SQLITE_TEXT) {
		sqlite3_result_null(ctx);
		return;
	}

	char* indexString = (char*)sqlite3_value_text(argv[0]);

	int arraySize = 10;
	int numTokens = 0;

	char* token = strtok(indexString, ",");

	uint32_t* indexList = (uint32_t*)malloc(arraySize * sizeof(uint32_t));

	while(token) {
		if(arraySize > 10) {
			arraySize = arraySize * 2;
			indexList = (uint32_t*)realloc(indexList, arraySize * sizeof(uint32_t));
		}

		int index = atoi(token);
		indexList[numTokens] = index;
		++numTokens;

		token = strtok(NULL, ",");
	}

	uint8_t size = (numTokens * 5) + 1;

	uint8_t* buffer = (uint8_t*)malloc(size);

	if(buffer) {
		int offset = 0;

		buffer[offset++] = numTokens;

		for(int i=0; i<numTokens; ++i) {
			uint32_t val = indexList[i];

			if(val < 256) {
				buffer[offset++] = TYPE_UINT8;
				buffer[offset++] = val;
			}
			else if(val < 65536) {
				buffer[offset++] = TYPE_UINT16;
				buffer[offset++] = val >> 8;
				buffer[offset++] = val;
			}
			else {
				buffer[offset++] = TYPE_UINT32;
				buffer[offset++] = val >> 24;
				buffer[offset++] = val >> 16;
				buffer[offset++] = val >> 8;
				buffer[offset++] = val;
			}
		}
	}

	sqlite3_result_blob(ctx, buffer, size, SQLITE_TRANSIENT);

	free(indexList);
	free(buffer);
}

void getIndexArrayFromBuffer(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
	if(argc < 1 || sqlite3_value_type(argv[0]) != SQLITE_BLOB) {
		sqlite3_result_null(ctx);
		return;
	}

	int bufferSize = sqlite3_value_bytes(argv[0]);
	uint8_t* buffer = (uint8_t*)sqlite3_value_blob(argv[0]);

	if(bufferSize < 3 || buffer == NULL) {
		sqlite3_result_null(ctx);
		return;
	}

	uint32_t* indexList = 0;

	int offset = 0;
	int size = buffer[offset++];

	int listSize = size * sizeof(uint32_t);
	indexList = malloc(listSize);

	for(int i=0; i<size; ++i) {
		uint8_t type = buffer[offset++];

		uint32_t index = 0;

		switch(type) {
			case TYPE_UINT8:
				index = buffer[offset++];
				break;

			case TYPE_UINT16:
				index = (buffer[offset++] << 8);
				index += buffer[offset++];
				break;

			case TYPE_UINT32:
				index = (buffer[offset++] << 24);
				index += (buffer[offset++] << 16);
				index += (buffer[offset++] << 8);
				index += buffer[offset++];
				break;

		}

		indexList[i] = index;
	}

	sqlite3_result_blob(ctx, indexList, listSize, SQLITE_TRANSIENT);

	free(indexList);
}


const char* stemToken(const char* language, const char* token, int size) {
	if(stemmer == 0) {
		stemmer = sb_stemmer_new(language, NULL);
    	if(stemmer == 0) {
			puts("language '");
			puts(language);
			puts("' not available for stemming\n");
			return 0;
    	}
	}

    const sb_symbol* sym = sb_stemmer_stem(stemmer, (sb_symbol*)token, size == 0 ? strlen(token) : size);

    return (const char*)sym;
}


void sqlite_filterContains(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
	if(argc < 3) {
		sqlite3_result_null(ctx);
		return;
	}

	if(sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL || sqlite3_value_type(argv[2]) != SQLITE_BLOB){
    	sqlite3_result_null(ctx);
    	return;
  	}

	const char* language = (char*)sqlite3_value_text(argv[0]);
	const char* token = (char*)sqlite3_value_text(argv[1]);
	void* filterStream = (void*)sqlite3_value_blob(argv[2]);

	if(filterStream == NULL) {
		sqlite3_result_null(ctx);
		return;
	}

    if(utf8len(token) < 3) {
    	sqlite3_result_null(ctx);
        return;
    }

   xor8_t filter;
  filter.seed = *((uint64_t*)filterStream);
  filter.blockLength = *((uint64_t*)filterStream + 1);
  filter.fingerprints = (uint8_t*)(filterStream + sizeof(uint64_t) * 2);

	const char* sym = stemToken(language, token, 0);
	uint64_t hash = murmurhash(sym, sb_stemmer_length(stemmer), FILTER_SEED);

   if(xor8_contain(hash, &filter))
		sqlite3_result_int(ctx, 1);
   else
		sqlite3_result_null(ctx);
}

void sqlite_createFullTextFilter(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
	if(argc < 2) {
		sqlite3_result_null(ctx);
		return;
	}

	if(sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL){
    	sqlite3_result_null(ctx);
    	return;
  	}

	const char* language = (char*)sqlite3_value_text(argv[0]);
	const char* text = (char*)sqlite3_value_text(argv[1]);

	uint8_t* filter = createFilterArray(language, text);
//    xor8_t* filter = createFullTextSearchFilter(language, text);
//	bool res = createFullTextSearchFilter(language, text, &filter);

	if(filter) {
        size_t size = filterArraySize();
//        size_t sizeOfMeta = 2 * sizeof(uint64_t);
//        size_t sizeOfFingerprints = filter->blockLength * sizeof(uint8_t) * 3;
//        size_t bufferSize = sizeOfMeta + sizeOfFingerprints;

//        void* buffer = malloc(bufferSize);
//        ((uint64_t*)buffer)[0] = filter->seed;
//        ((uint64_t*)buffer)[1] = filter->blockLength;
//        memcpy(buffer + sizeOfMeta, filter->fingerprints, sizeOfFingerprints);

		sqlite3_result_blob(ctx, filter, size, SQLITE_TRANSIENT);

		deleteFilterArray();
//		free(filter);
	}
}

//void searchStep(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
//	printf("fts step: %i\n", argc);
//}

//void searchFinalize(sqlite3_context* ctx) {
//	printf("search finalize\n");
//}

int registerFullTextSearch(sqlite3* sql) {
	sqlite3_create_function(sql, "ftsFilter", 2, SQLITE_UTF8, 0, sqlite_createFullTextFilter, 0, 0);
	sqlite3_create_function(sql, "ftsContains", 3, SQLITE_UTF8, 0, sqlite_filterContains, 0, 0);

	// Register indexList functions
	sqlite3_create_function(sql, "indexBlob", 1, SQLITE_UTF8, 0, getBufferFromIndexString, 0, 0);
	sqlite3_create_function(sql, "indexList", 1, SQLITE_UTF8, 0, getIndexArrayFromBuffer, 0, 0);

	return 0;
}

// bool lookupToken(char* token) {
//    int pos = 0;

//	while(pos < MAX_TOKEN_SIZE && token[pos] != 0) {
//        char ch = token[pos];

//        if(ch == "")
//        	token[pos] = 'a';
//    }

//    return 1;
//}

bool containsToken(xor8_t* filter, const char* language, const char* token) {
	const char* sym = stemToken(language, token, 0);
	uint32_t hexVal = murmurhash(sym, sb_stemmer_length(stemmer), FILTER_SEED);

	return xor8_contain(hexVal, filter);
}

bool addTokenToHashList(const char* language, const char* token, int tokenSize, hashList* list) {
    // Don't hash tokens with less than 3 characters
    if(utf8len(token) < 3)
    	return false;

    if(tokenSize == -1)
    	tokenSize = strlen(token);

	const char* sym = stemToken(language, token, tokenSize);
	uint32_t hexVal = murmurhash(sym, sb_stemmer_length(stemmer), FILTER_SEED);

	for(int i=0; i<list->numItems; ++i) {
		if(hexVal == list->buffer[i]) {
			return false;
		}
	}

	if(list->numItems == list->size) {
        int bufferSize = (list->size) * 1.5;
		list->buffer = (uint64_t*)realloc(list->buffer, bufferSize * sizeof(uint64_t));

		if(list->buffer == 0) {
			puts("error relocating token hash array\n");
//			printf("%s/%d: error relocating token hash array\n", __func__, __LINE__);
			return false;
		}

		list->size = bufferSize;
	}

	list->buffer[list->numItems] = hexVal;
    ++list->numItems;
//	hashList[numItems] = hexVal;

	return true;
}

xor8_t* createFullTextSearchFilter(const char* language, const char* text) {
	int pos = 0;
	int index = 0;
//	int numItems = 0;
//	int listSize = TOKEN_LIST_SIZE;

	char token[MAX_TOKEN_SIZE];
//	int tokenSize = 0;

	size_t textSize = strlen(text);

	hashList list;
    list.size = TOKEN_LIST_SIZE;
    list.numItems = 0;
    list.buffer = (uint64_t*)malloc(sizeof(uint64_t) * TOKEN_LIST_SIZE);
//	uint64_t* hashList = (uint64_t*)malloc(sizeof(uint64_t) * TOKEN_LIST_SIZE);

	if(list.buffer == NULL) {
		puts("error creating token hash array\n");
//		printf("%s/%d: error creating token hash array\n", __func__, __LINE__);
		return 0;
	}

	while(pos < textSize) {
        unsigned int ch = text[pos];
//		int ch = tolower(text[pos]);
		pos += 1;

		if(ch < 0x30 ||
        	(ch > 0x39 && ch < 0x41) ||
            (ch > 0x5A && ch < 0x60) ||
            (ch > 0x7A && ch < 0xBE) ||
        	ch == ' ' ||
            ch == '\t'||
            ch == '\r'||
           	ch == '\n'||
           	ch == '=' ||
        	ch == '<' ||
            ch == '>' ||
            ch == '.' ||
            ch == ',' ||
            ch == '\'' ||
            ch == '$' ||
            ch == '/' ||
            ch == '\\' ||
            ch == '[' ||
            ch == ']' ||
            ch == '(' ||
            ch == ')' ||
            ch == '"' ||
            ch == '-' ||
            ch == '^' ||
            ch == '`' ||
            ch == '#' ||
            ch == '*' ||
            ch == '\t'
        ) {
			token[index] = 0;
			index = 0;

			addTokenToHashList(language, token, -1, &list);
//			size_t tokenSize = utf8len(token);
//			addTokenToHashList(language, token, tokenSize, &list);

//			++numItems;
//			tokenSize = 0;
		}
		else {
			token[index++] = ch;

//			if(ch < 0x80 || ch > 0xBF)
//				tokenSize += 1;
		}
	}

	if(index < textSize) {
		token[index] = 0;
		// Add remaining token
		addTokenToHashList(language, token, -1, &list);
//        size_t tokenSize = utf8len(token);
//		addTokenToHashList(language, token, tokenSize, &list);
	}

    xor8_t* filter = malloc(sizeof(xor8_t));

	xor8_allocate(list.numItems, filter);
	xor8_populate(list.buffer, list.numItems, filter);

//    int size = filter.blockLength * 3 * sizeof(uint8_t);

//    printf("%i\n", (int)filter.blockLength);

//    for(int i=0; i<size; ++i) {
//        _hashList[i] = filter.fingerprints[i];
//        printf("%11x\n", _hashList[i]);
//    }

	return filter;
//	return (size_t)&filter;
}

typedef struct _filterArray {
    size_t length;
    uint8_t* buffer;
} filterArray;

filterArray sFilterArray = {0};

uint8_t* createFilterArray(const char* language, const char* text) {
    xor8_t* filter = createFullTextSearchFilter(language, text);

    if(filter) {
        size_t sizeOfMeta = 2 * sizeof(uint64_t);
        size_t sizeOfFingerprints = filter->blockLength * sizeof(uint8_t) * 3;
        size_t bufferSize = sizeOfMeta + sizeOfFingerprints;

		uint8_t* buffer = malloc(bufferSize);

        if(buffer == NULL)
        	return 0;

        ((uint64_t*)buffer)[0] = filter->seed;
        ((uint64_t*)buffer)[1] = filter->blockLength;
        memcpy(buffer + sizeOfMeta, filter->fingerprints, sizeOfFingerprints);

        deleteFilter(filter);

//        *size = bufferSize;

        sFilterArray.buffer = buffer;
        sFilterArray.length = bufferSize;

        return buffer;
    }

    return 0;
}

int filterArraySize() {
    return sFilterArray.buffer ? sFilterArray.length : 0;
}

void deleteFilterArray() {
    if(sFilterArray.buffer) {
        free(sFilterArray.buffer);
        sFilterArray.length = 0;
    }
}

void deleteFilter(xor8_t* filter) {
    if(filter) {
    	xor8_free(filter);
        free(filter);
        filter = NULL;
    }

    sFilterArray.buffer = NULL;
    sFilterArray.length  = 0;
}

/*
// From Cuckoo Filter Library
// https://github.com/jonahharris/libcuckoofilter
typedef enum {
  CUCKOO_FILTER_OK = 0,
  CUCKOO_FILTER_NOT_FOUND,
  CUCKOO_FILTER_FULL,
  CUCKOO_FILTER_ALLOCATION_FAILED,
} CUCKOO_FILTER_RETURN;

typedef struct cuckoo_filter_t cuckoo_filter_t;

CUCKOO_FILTER_RETURN
cuckoo_filter_new (
  cuckoo_filter_t     **filter,
  size_t                max_key_count,
  size_t                max_kick_attempts,
  uint32_t              seed
);

CUCKOO_FILTER_RETURN
cuckoo_filter_free (
  cuckoo_filter_t     **filter
);

CUCKOO_FILTER_RETURN
cuckoo_filter_add (
  cuckoo_filter_t      *filter,
  void                 *key,
  size_t                key_length_in_bytes
);

CUCKOO_FILTER_RETURN
cuckoo_filter_remove (
  cuckoo_filter_t      *filter,
  void                 *key,
  size_t                key_length_in_bytes
);

CUCKOO_FILTER_RETURN
cuckoo_filter_contains (
  cuckoo_filter_t      *filter,
  void                 *key,
  size_t                key_length_in_bytes
);

#define CUCKOO_NESTS_PER_BUCKET     4

static inline uint32_t murmurhash (const void *, uint32_t, uint32_t);
static inline uint32_t hash (const void *, uint32_t, uint32_t, uint32_t,
  uint32_t);

typedef struct {
  uint16_t              fingerprint;
} __attribute__((packed)) cuckoo_nest_t;

typedef struct {
  uint32_t              fingerprint;
  uint32_t              h1;
  uint32_t              h2;
  uint32_t              padding;
} __attribute__((packed)) cuckoo_item_t;

typedef struct {
  bool                  was_found;
  cuckoo_item_t         item;
} cuckoo_result_t;

struct cuckoo_filter_t {
  uint32_t              bucket_count;
  uint32_t              nests_per_bucket;
  uint32_t              mask;
  uint32_t              max_kick_attempts;
  uint32_t              seed;
  uint32_t              padding;
  cuckoo_item_t         victim;
  cuckoo_item_t        *last_victim;
  cuckoo_nest_t         bucket[1];
} __attribute__((packed));


static inline size_t
next_power_of_two (size_t x) {
  --x;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;

  if (8 == sizeof(size_t)) {
    x |= x >> 32;
  }

  return ++x;
}


static inline CUCKOO_FILTER_RETURN
add_fingerprint_to_bucket (
  cuckoo_filter_t      *filter,
  uint32_t              fp,
  uint32_t              h
) {
  for (size_t ii = 0; ii < filter->nests_per_bucket; ++ii) {
    cuckoo_nest_t *nest =
      &filter->bucket[(h * filter->nests_per_bucket) + ii];
    if (0 == nest->fingerprint) {
      nest->fingerprint = fp;
      return CUCKOO_FILTER_OK;
    }
  }

  return CUCKOO_FILTER_FULL;

}


static inline CUCKOO_FILTER_RETURN
remove_fingerprint_from_bucket (
  cuckoo_filter_t      *filter,
  uint32_t              fp,
  uint32_t              h
) {
  for (size_t ii = 0; ii < filter->nests_per_bucket; ++ii) {
    cuckoo_nest_t *nest =
      &filter->bucket[(h * filter->nests_per_bucket) + ii];
    if (fp == nest->fingerprint) {
      nest->fingerprint = 0;
      return CUCKOO_FILTER_OK;
    }
  }

  return CUCKOO_FILTER_NOT_FOUND;

}


static inline CUCKOO_FILTER_RETURN
cuckoo_filter_move (
  cuckoo_filter_t      *filter,
  uint32_t              fingerprint,
  uint32_t              h1,
  int                   depth
) {
  uint32_t h2 = ((h1 ^ hash(&fingerprint, sizeof(fingerprint),
    filter->bucket_count, 900, filter->seed)) % filter->bucket_count);

  if (CUCKOO_FILTER_OK == add_fingerprint_to_bucket(filter,
    fingerprint, h1)) {
    return CUCKOO_FILTER_OK;
  }

  if (CUCKOO_FILTER_OK == add_fingerprint_to_bucket(filter,
    fingerprint, h2)) {
    return CUCKOO_FILTER_OK;
  }

//printf("depth = %u\n", depth);
  if (filter->max_kick_attempts == depth) {
    return CUCKOO_FILTER_FULL;
  }

  size_t row = (0 == (rand() % 2) ? h1 : h2);
  size_t col = (rand() % filter->nests_per_bucket);
  size_t elem =
    filter->bucket[(row * filter->nests_per_bucket) + col].fingerprint;
  filter->bucket[(row * filter->nests_per_bucket) + col].fingerprint =
    fingerprint;

  return cuckoo_filter_move(filter, elem, row, (depth + 1));

}


CUCKOO_FILTER_RETURN
cuckoo_filter_new (
  cuckoo_filter_t     **filter,
  size_t                max_key_count,
  size_t                max_kick_attempts,
  uint32_t              seed
) {
  cuckoo_filter_t      *new_filter;

  size_t bucket_count =
    next_power_of_two(max_key_count / CUCKOO_NESTS_PER_BUCKET);
  if (0.96 < (double) max_key_count / bucket_count / CUCKOO_NESTS_PER_BUCKET) {
    bucket_count <<= 1;
  }

  // FIXME: Should check for integer overflows here
  size_t allocation_in_bytes = (sizeof(cuckoo_filter_t)
    + (bucket_count * CUCKOO_NESTS_PER_BUCKET * sizeof(cuckoo_nest_t)));

  new_filter = calloc(allocation_in_bytes, 1);
  if (!new_filter) {
    return CUCKOO_FILTER_ALLOCATION_FAILED;
  }

  new_filter->last_victim = NULL;
  memset(&new_filter->victim, 0, sizeof(new_filter)->victim);
  new_filter->bucket_count = bucket_count;
  new_filter->nests_per_bucket = CUCKOO_NESTS_PER_BUCKET;
  new_filter->max_kick_attempts = max_kick_attempts;
//  new_filter->seed = seed;
 new_filter->seed = (size_t) time(NULL);
  new_filter->mask = (uint32_t) ((1U << (sizeof(cuckoo_nest_t) * 8)) - 1);

  *filter = new_filter;

  return CUCKOO_FILTER_OK;

}

CUCKOO_FILTER_RETURN
cuckoo_filter_free (
  cuckoo_filter_t     **filter
) {
  free(*filter);
  *filter = NULL;

  return CUCKOO_FILTER_OK;
}

static inline CUCKOO_FILTER_RETURN
cuckoo_filter_lookup (
  cuckoo_filter_t      *filter,
  cuckoo_result_t      *result,
  void                 *key,
  size_t                key_length_in_bytes
) {
  uint32_t fingerprint = hash(key, key_length_in_bytes, filter->bucket_count,
    1000, filter->seed);
  uint32_t h1 = hash(key, key_length_in_bytes, filter->bucket_count, 0,
    filter->seed);
  fingerprint &= filter->mask; fingerprint += !fingerprint;
  uint32_t h2 = ((h1 ^ hash(&fingerprint, sizeof(fingerprint),
    filter->bucket_count, 900, filter->seed)) % filter->bucket_count);

  result->was_found = false;
  result->item.fingerprint = 0;
  result->item.h1 = 0;
  result->item.h2 = 0;

  for (size_t ii = 0; ii < filter->nests_per_bucket; ++ii) {
    cuckoo_nest_t *n1 =
      &filter->bucket[(h1 * filter->nests_per_bucket) + ii];
    if (fingerprint == n1->fingerprint) {
      result->was_found = true;
      break;
    }

    cuckoo_nest_t *n2 =
      &filter->bucket[(h2 * filter->nests_per_bucket) + ii];
    if (fingerprint == n2->fingerprint) {
      result->was_found = true;
      break;
    }
  }

  result->item.fingerprint = fingerprint;
  result->item.h1 = h1;
  result->item.h2 = h2;

  return ((true == result->was_found)
    ? CUCKOO_FILTER_OK : CUCKOO_FILTER_NOT_FOUND);

}



CUCKOO_FILTER_RETURN
cuckoo_filter_add (
  cuckoo_filter_t      *filter,
  void                 *key,
  size_t                key_length_in_bytes
) {
  cuckoo_result_t   result;

  cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);
  if (true == result.was_found) {
    return CUCKOO_FILTER_OK;
  }

  if (NULL != filter->last_victim) {
    return CUCKOO_FILTER_FULL;
  }

  return cuckoo_filter_move(filter, result.item.fingerprint, result.item.h1,
    0);

}



CUCKOO_FILTER_RETURN
cuckoo_filter_remove (
  cuckoo_filter_t      *filter,
  void                 *key,
  size_t                key_length_in_bytes
) {
  cuckoo_result_t   result;
  bool              was_deleted = false;

  cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);
  if (false == result.was_found) {
    return CUCKOO_FILTER_NOT_FOUND;
  }

  if (CUCKOO_FILTER_OK == remove_fingerprint_from_bucket(filter,
    result.item.fingerprint, result.item.h1)) {
    was_deleted = true;
  } else if (CUCKOO_FILTER_OK == remove_fingerprint_from_bucket(filter,
    result.item.fingerprint, result.item.h2)) {
    was_deleted = true;
  }

  if ((true == was_deleted) & (NULL != filter->last_victim)) {

  }

  return ((true == was_deleted) ? CUCKOO_FILTER_OK : CUCKOO_FILTER_NOT_FOUND);

}



CUCKOO_FILTER_RETURN
cuckoo_filter_contains (
  cuckoo_filter_t      *filter,
  void                 *key,
  size_t                key_length_in_bytes
) {
  cuckoo_result_t   result;

  return cuckoo_filter_lookup(filter, &result, key, key_length_in_bytes);

}


static inline uint32_t
hash (
 const void           *key,
 uint32_t              key_length_in_bytes,
 uint32_t              size,
 uint32_t              n,
 uint32_t              seed
) {
 uint32_t h1 = murmurhash(key, key_length_in_bytes, seed);
 uint32_t h2 = murmurhash(key, key_length_in_bytes, h1);

 return ((h1 + (n * h2)) % size);

}

bool cuckooFilterAdd(cuckoo_filter_t* filter, const char* language, const char* key) {
	if(filter == NULL)
		return 0;

	int rc = -1;
	size_t size = strlen(key);

	const char* sym = stemToken(language, key, size);

	if(size)
		rc = cuckoo_filter_add(filter, (void*)sym, size);

	if(CUCKOO_FILTER_OK != rc) {
    	printf("%s/%d: %d\n", __func__, __LINE__, rc);
		return false;
  	}

	return true;
}

typedef struct _tokenEntry {
	char* index;
	size_t size;
} tokenEntry;

size_t createCuckooFilter(const char* language, char* text, int seed) {
	cuckoo_filter_t  *filter;
  	int              rc;

	int pos = 0;
	int index = 0;
	int tokenSize = 0;
	int numItems = 0;
	size_t textSize = strlen(text);

	tokenEntry* tokenList = malloc(sizeof(tokenEntry) * TOKEN_LIST_SIZE);

	if(tokenList == NULL) {
		printf("%s/%d: error creating token list array\n", __func__, __LINE__);
		return 0;
	}

	while(pos < textSize) {
		int ch = text[pos];
		text[pos] = tolower(ch);

		pos += 1;

		if(ch == ' ' || ch == '\n') {
			tokenEntry* entry = (tokenEntry*)&(tokenList[numItems]);
			entry->index = text + index;
			entry->size = tokenSize;

			++numItems;
			index = pos;
			tokenSize = 0;
		}
		else if(ch < 0x80 || ch > 0xBF) {
			tokenSize += 1;
		}
	}

	if(index < textSize) {
		// Add remaining token
		tokenEntry* entry = (tokenEntry*)&(tokenList[numItems]);
		entry->index = text + index;
		entry->size = tokenSize;

		++numItems;
	}

	// The filter needs to be created with at least 4 items (assert(filter->bucket_count != 0)) and have at max 95%
	int minNumItems = (numItems * 1.10) + 1;
	if(minNumItems < 5000)
		minNumItems = 5000;

	rc = cuckoo_filter_new(&filter, minNumItems, 100, (uint32_t)seed);

	 if(CUCKOO_FILTER_OK != rc) {
    	printf("%s/%d: %d\n", __func__, __LINE__, rc);
		return 0;
  	}

	if(filter->bucket_count) {
		for(int i=0; i<numItems; ++i) {
			tokenEntry* entry = &tokenList[i];
			const char* sym = stemToken(language, entry->index, entry->size);

			cuckooFilterAdd(filter, language, sym);
		}
	}

	return (size_t)filter;
}


int cuckooFilterContains(cuckoo_filter_t* filter, const char* language, const char* token) {
	if(filter == NULL || filter->bucket_count == 0)
		return 0;

	int rc = -1;
	size_t size = strlen(token);

	const char* sym = stemToken(language, token, size);
	size = strlen(sym);

	if(size)
		rc = cuckoo_filter_contains(filter, (void*)sym, size);

  	if(CUCKOO_FILTER_OK != rc) {
    	printf("%s/%d: %d\n", __func__, __LINE__, rc);
		return 0;
  	}
	else {
		return true;
	}
}
*/
