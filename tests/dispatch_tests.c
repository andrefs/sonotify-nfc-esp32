#include <stdio.h>
#include <string.h>

#include "dispatch.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_STR(actual, expected)                                            \
  do {                                                                         \
    g_checks++;                                                                \
    if (strcmp((actual), (expected)) != 0) {                                   \
      printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__,      \
             (actual), (expected));                                            \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

#define CHECK_TRUE(cond)                                                       \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      printf("FAIL %s:%d: expected true\n", __FILE__, __LINE__);               \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

#define CHECK_FALSE(cond)                                                      \
  do {                                                                         \
    g_checks++;                                                                \
    if (cond) {                                                                \
      printf("FAIL %s:%d: expected false\n", __FILE__, __LINE__);              \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

static const char *kValidJson = "["
    "{\"tagId\":\"04 AA BB CC DD EE FF\",\"contentId\":\"Spotify:t1:1\","
    "\"description\":\"play one\",\"contentType\":\"music\"},"
    "{\"tagId\":\"04 11 22 33 44 55 66\",\"contentId\":\"SQ:42\","
    "\"description\":\"queue\",\"contentType\":\"favorite_item_id\"},"
    "{\"tagId\":\"04 99 88 77 66 55 44\",\"contentId\":\"Spotify:p:3\"}"
    "]";

static void test_matching_uid(void) {
  char id[64] = {0};
  char type[64] = {0};

  CHECK_TRUE(dispatch_lookup(kValidJson, "04 AA BB CC DD EE FF", id, sizeof(id),
                             type, sizeof(type)));
  CHECK_STR(id, "Spotify:t1:1");
  CHECK_STR(type, "music");
}

static void test_matching_uid_queue(void) {
  char id[64] = {0};
  char type[64] = {0};

  CHECK_TRUE(dispatch_lookup(kValidJson, "04 11 22 33 44 55 66", id,
                             sizeof(id), type, sizeof(type)));
  CHECK_STR(id, "SQ:42");
  CHECK_STR(type, "favorite_item_id");
}

static void test_missing_content_type_defaults_to_music(void) {
  char id[64] = {0};
  char type[64] = {0};

  CHECK_TRUE(dispatch_lookup(kValidJson, "04 99 88 77 66 55 44", id,
                             sizeof(id), type, sizeof(type)));
  CHECK_STR(id, "Spotify:p:3");
  CHECK_STR(type, "music");
}

static void test_unknown_uid(void) {
  char id[64] = {0};
  char type[64] = {0};

  CHECK_FALSE(dispatch_lookup(kValidJson, "AA BB CC DD EE", id, sizeof(id),
                              type, sizeof(type)));
  CHECK_STR(id, "");
  CHECK_STR(type, "");
}

static void test_null_args(void) {
  char id[64] = {0};
  char type[64] = {0};

  CHECK_FALSE(dispatch_lookup(NULL, "04 AA BB CC DD EE FF", id, sizeof(id),
                              type, sizeof(type)));
  CHECK_FALSE(dispatch_lookup(kValidJson, NULL, id, sizeof(id), type,
                              sizeof(type)));
}

static void test_invalid_json(void) {
  char id[64] = {0};
  char type[64] = {0};
  const char *uid = "04 AA BB CC DD EE FF";

  CHECK_FALSE(dispatch_lookup("not json", uid, id, sizeof(id), type,
                              sizeof(type)));
  CHECK_FALSE(dispatch_lookup("{\"a\":1}", uid, id, sizeof(id), type,
                              sizeof(type)));
  CHECK_FALSE(dispatch_lookup("[]", uid, id, sizeof(id), type, sizeof(type)));
}

static void test_missing_content_id(void) {
  const char *json = "[{\"tagId\":\"04 AA BB CC DD EE FF\","
                     "\"description\":\"no content\"}]";
  char id[64] = {0};
  char type[64] = {0};

  CHECK_FALSE(dispatch_lookup(json, "04 AA BB CC DD EE FF", id, sizeof(id),
                              type, sizeof(type)));
}

static void test_small_buffers(void) {
  char id[4] = {0};
  char type[4] = {0};

  CHECK_TRUE(dispatch_lookup(kValidJson, "04 AA BB CC DD EE FF", id, sizeof(id),
                             type, sizeof(type)));
  CHECK_STR(id, "Spo");
  CHECK_STR(type, "mus");
}

int main(void) {
  test_matching_uid();
  test_matching_uid_queue();
  test_missing_content_type_defaults_to_music();
  test_unknown_uid();
  test_null_args();
  test_invalid_json();
  test_missing_content_id();
  test_small_buffers();

  printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  if (g_failures > 0) {
    printf("%d FAILURES\n", g_failures);
    return 1;
  }
  printf("All tests passed\n");
  return 0;
}